// SPDX-License-Identifier: GPL-2.0
/*
 * Trace Run Queue Latency
 *
 * Copyright (C) 2020 Bytedance, Inc., Muchun Song
 *
 * The main authors of the trace qunqueue latency code are:
 *
 * Muchun Song <songmuchun@bytedance.com>
 */
#define pr_fmt(fmt) "runqlat: " fmt

#include <linux/hrtimer.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/percpu.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/sizes.h>
#include <linux/stacktrace.h>
#include <linux/timer.h>
#include <linux/tracepoint.h>
#include <linux/version.h>
#include <linux/vmalloc.h>
#include <trace/events/sched.h>
#ifdef CONFIG_CGROUP_SCHED
#include <linux/cgroup.h>
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
#include <linux/sched.h>
#else
#include <linux/sched/clock.h>
#include <linux/sched/task.h>
#endif

/*
 * 16 should be enough for the current production environment
 * in most normal case
 */
#define MAX_CGRP_HIERARCHY	16

#define RECORD_TASK_SCHED_SWITCH	0
#define RECORD_TASK_MIGRATION		1

/*
 * With the max size set, the memory allocated by this module is around
 * 32M.
 */
#define MAX_TRACE_ENTRIES		256
#define PER_TRACE_ENTRY_TASKS		32
#define MAX_TRACE_ENTRY_TASKS		\
	(MAX_TRACE_ENTRIES * PER_TRACE_ENTRY_TASKS)

#define THRESHOLD_DEFAULT		(20 * 1000 * 1000UL)

#define INVALID_PID			-1
#define INVALID_CPU			-1
#define PROBE_TRACEPOINTS		 4

#define LATENCY_HISTOGRAM_ENTRY		12

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
#define DEFINE_PROC_ATTRIBUTE(name, __write)				\
	static int name##_open(struct inode *inode, struct file *file)	\
	{								\
		return single_open(file, name##_show, PDE_DATA(inode));	\
	}								\
									\
	static const struct file_operations name##_fops = {		\
		.owner		= THIS_MODULE,				\
		.open		= name##_open,				\
		.read		= seq_read,				\
		.write		= __write,				\
		.llseek		= seq_lseek,				\
		.release	= single_release,			\
	}
#else
#define DEFINE_PROC_ATTRIBUTE(name, __write)				\
	static int name##_open(struct inode *inode, struct file *file)	\
	{								\
		return single_open(file, name##_show, PDE_DATA(inode));	\
	}								\
									\
	static const struct proc_ops name##_fops = {			\
		.proc_open	= name##_open,				\
		.proc_read	= seq_read,				\
		.proc_write	= __write,				\
		.proc_lseek	= seq_lseek,				\
		.proc_release	= single_release,			\
	}
#endif /* LINUX_VERSION_CODE */


#define DEFINE_PROC_ATTRIBUTE_RW(name)					\
	static ssize_t name##_write(struct file *file,			\
				    const char __user *buf,		\
				    size_t count, loff_t *ppos)		\
	{								\
		return name##_store(PDE_DATA(file_inode(file)), buf,	\
				    count);				\
	}								\
	DEFINE_PROC_ATTRIBUTE(name, name##_write)

#define DEFINE_PROC_ATTRIBUTE_RO(name)	\
	DEFINE_PROC_ATTRIBUTE(name, NULL)

static int max_entries_set(const char *val, const struct kernel_param *kp)
{
	int ret;
	uint size;

	ret = kstrtouint(val, 0, &size);
	if (ret)
		return ret;

	/*
	 * Must limit the size of the memory pool.
	 * At least one buffer should be available for new record.
	 */
	if (size < 2 || size > MAX_TRACE_ENTRIES)
		return -EINVAL;

	*((uint *)kp->arg) = size;

	return 0;
}

static const struct kernel_param_ops max_entries_ops = {
	.set = max_entries_set,
	.get = param_get_uint,
};

unsigned int max_trace_entries = 128;
module_param_cb(max_trace_entries, &max_entries_ops, &max_trace_entries, 0444);
MODULE_PARM_DESC(max_trace_entries, "Max trace entries in runqlat(max: 256)");

static int per_trace_tasks_set(const char *val, const struct kernel_param *kp)
{
	int ret;
	uint size;

	ret = kstrtouint(val, 0, &size);
	if (ret)
		return ret;

	/* Must limit the size of the memory pool. */
	if (size < 2 || size > PER_TRACE_ENTRY_TASKS)
		return -EINVAL;

	*((uint *)kp->arg) = size;

	return 0;
}

static const struct kernel_param_ops per_trace_ops = {
	.set = per_trace_tasks_set,
	.get = param_get_uint,
};

/*
 * struct cfs_rq is not exported, we define my_cfs_rq here,
 * hence we can get the nr_running member.
 *
 * the offset is correct in 3.10 and 4.18 kernel, havn't
 * verified other kernel versions yet.
 */
#if LINUX_VERSION_CODE <= KERNEL_VERSION(3, 10, 0)
/* For CentOS 7 3.10 kernel*/
struct my_cfs_rq {
	const char padding0[16];
	const unsigned int nr_running;
	const unsigned int h_nr_running;
	const char padding1[200];
};
#else
/* For CentOS 8 4.18 kernel*/
struct my_cfs_rq {
	const char padding0[24];
	const unsigned int nr_running;
	const unsigned int h_nr_running;
	const char padding1[200];
};
#endif

/*
 * In order to use the memory pool as a ring, we have to set the
 * per_trace_tasks with a fixed size, then the old trace can be reused
 * by the new trace.
 */
unsigned int per_trace_tasks = 16;
module_param_cb(per_trace_tasks, &per_trace_ops, &per_trace_tasks, 0444);
MODULE_PARM_DESC(per_trace_tasks, "max tasks in each trace in runqlat (max: 32)");

unsigned int max_trace_entry_tasks;

/**
 * If we call register_trace_sched_{wakeup,wakeup_new,switch,migrate_task}()
 * directly in a kernel module, the compiler will complain about undefined
 * symbol of __tracepoint_sched_{wakeup, wakeup_new, switch, migrate_task}
 * because the kernel do not export the tracepoint symbol. Here is a workaround
 * via for_each_kernel_tracepoint() to lookup the tracepoint and save.
 */
struct tracepoints_probe {
	struct tracepoint *tps[PROBE_TRACEPOINTS];
	const char *tp_names[PROBE_TRACEPOINTS];
	void *tp_probes[PROBE_TRACEPOINTS];
	void *priv;
	int num_initalized;
};

struct task_entry {
	u64 ts_nsec;
	u64 runtime;
	pid_t pid;
	int cpu;
	char comm[TASK_COMM_LEN];
	unsigned int nr_running[MAX_CGRP_HIERARCHY];
	int nr_cgrp_hierarchy;
	long state;
#ifdef CONFIG_CGROUP_SCHED
	/*
	 * Note, we have to store the cgroup information here, because if
	 * we get the cgroup_path when we print the process info, this
	 * process may already exit.
	 * But the PATH_MAX is 4096, which consumes too much memory, so
	 * we must limit the number of the tasks. Otherwise this module
	 * may consume too much memory, which is very dangerous.
	 * We may optimize the size future, but must be very careful.
	 */
	char cgroup[PATH_MAX]; /* The cpu cgroup of this task */
#endif
};

struct trace_entry {
	u64 ts_nsec; /* Timestamp of this trace happens */
	u64 latency;
	u64 runtime;
	unsigned int nr_tasks;
	unsigned int task_idx;
	int cpu;
	int migrations;
	struct task_entry *entries;
};

struct runqlat_info {
	int cpu;		/* The target CPU */
	pid_t pid;		/* Trace this pid only */
	u64 rq_start;
	u64 run_start;
	u64 target_run_start;
	u64 target_run_time;
	u64 threshold;
	struct task_struct *curr;
	unsigned long latency_hist[LATENCY_HISTOGRAM_ENTRY];

	unsigned int nr_trace;
	unsigned int trace_idx;
	struct trace_entry *trace_entries;

	unsigned int nr_task;
	struct task_entry *task_entries;

	int migrations;
#ifdef CONFIG_CGROUP_SCHED
	char cgroup[PATH_MAX]; /* The cpu cgroup of the target task */
#endif

	spinlock_t lock;
};

static struct runqlat_info runqlat_info = {
	.pid		= INVALID_PID,
	.cpu		= INVALID_CPU,
	.threshold	= THRESHOLD_DEFAULT,
	.lock		= __SPIN_LOCK_UNLOCKED(runqlat_info.lock),
};

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
static void probe_sched_wakeup(void *priv, struct task_struct *p, int success)
#else
static void probe_sched_wakeup(void *priv, struct task_struct *p)
#endif
{
	struct runqlat_info *info = priv;

	if (p->pid != info->pid)
		return;

	/* interrupts should be off from try_to_wake_up() */
	spin_lock(&info->lock);
	if (unlikely(p->pid != info->pid)) {
		spin_unlock(&info->lock);
		return;
	}

	info->rq_start = local_clock();
	info->run_start = info->rq_start;
	info->cpu = task_cpu(p);
	spin_unlock(&info->lock);
}

static inline void runqlat_info_reset(struct runqlat_info *info)
{
	info->rq_start = 0;
	info->run_start = 0;
	info->target_run_start = 0;
	info->migrations = 0;
	info->cpu = INVALID_CPU;
	info->curr = NULL;
}

static struct task_entry *index_to_task(struct task_entry *entry,
					unsigned int trace_idx,
					unsigned int task_idx)
{
	return entry + trace_idx * per_trace_tasks + task_idx;
}

/* Must be called with @info->lock held */
static void record_task(struct runqlat_info *info, struct task_struct *p,
			u64 runtime, int flag)
	__must_hold(&info->lock)
{
	struct task_entry *task;
	struct trace_entry *trace;
	struct cgroup *cgrp;
	struct sched_entity *se = NULL;
	int i = 0;

	trace = info->trace_entries + info->trace_idx;
	task = index_to_task(info->task_entries, info->trace_idx, trace->task_idx);

	if (trace->nr_tasks == 0)
		trace->entries = task;

	task->runtime = runtime;

	if (p) {
		se = &p->se;
		task->pid = p->pid;
		task->cpu = task_cpu(p);
		task->ts_nsec = cpu_clock(task_cpu(p));
		task->state = p->state;
		strncpy(task->comm, p->comm, TASK_COMM_LEN);

#ifdef CONFIG_CGROUP_SCHED
		rcu_read_lock();
#if LINUX_VERSION_CODE <  KERNEL_VERSION(3, 15, 0)
		cgrp = task_cgroup(p, cpu_cgroup_subsys_id);
#else
		/*
		 * Cgroup name changed in commit 073219e995b4
		 * ("cgroup: clean up cgroup_subsys names and initialization")
		 */
		cgrp = task_cgroup(p, cpu_cgrp_id);
#endif
		cgroup_path(cgrp, task->cgroup, sizeof(task->cgroup));
		rcu_read_unlock();
#endif
		for (; se; se = se->parent) {
			struct my_cfs_rq *cfs_rq = (struct my_cfs_rq *)se->cfs_rq;
			if (i < MAX_CGRP_HIERARCHY) {
				task->nr_running[i] = cfs_rq->nr_running | (flag << 31);
				i++;
			}
		}
		task->nr_cgrp_hierarchy = i;
	} else {
		task->pid = 0;	/* Idle task */
		task->cpu = 0;	/* Don't know the prev cpu here, ignore it */
		/* Don't know the prev cpu here, use local clock instead */
		task->ts_nsec = local_clock();
		task->state = TASK_RUNNING;
		strncpy(task->comm, "swapper", TASK_COMM_LEN);

		strncpy(task->cgroup, "/", TASK_COMM_LEN);
		memset(task->nr_running, 0, sizeof(task->nr_running));
		task->nr_running[0] = (flag << 31);
		task->nr_cgrp_hierarchy = 1;
	}

	trace->nr_tasks++;
	trace->task_idx++;

	/* May better override the minimum task */
	if (trace->task_idx >= per_trace_tasks)
		trace->task_idx = 0;
}

static inline void latency_histogram_store(struct runqlat_info *info, u64 delta)
{
	int index = -1;

	delta /= 1000000UL;
	do {
		index++;
		delta >>= 1;
	} while (delta > 0);

	if (unlikely(index >= LATENCY_HISTOGRAM_ENTRY))
		index = LATENCY_HISTOGRAM_ENTRY - 1;

	info->latency_hist[index]++;
}

/* Must be called with @info->lock held */
static bool record_task_commit(struct runqlat_info *info, u64 latency,
		u64 runtime, int cpu)
	__must_hold(&info->lock)
{
	struct trace_entry *trace;

	latency_histogram_store(info, latency);
	trace = info->trace_entries + info->trace_idx;
	if (trace->nr_tasks == 0)
		return false;

	if (latency >= info->threshold) {
		trace->ts_nsec = local_clock();
		trace->cpu = cpu;
		trace->latency = latency;
		trace->runtime = runtime;
		trace->migrations = info->migrations;
		info->nr_trace++;
		info->trace_idx++;
		info->migrations = 0;
		if (info->trace_idx == max_trace_entries)
			info->trace_idx = 0;

		/* At least one buffer should be available for new record */
		if (info->nr_trace >= max_trace_entries) {
			trace = info->trace_entries + info->trace_idx;
			trace->nr_tasks = 0;
			trace->task_idx = 0;
			trace->latency = 0;
			trace->runtime = 0;
			trace->ts_nsec = 0;
			trace->cpu = INVALID_CPU;
		}
	} else {
		trace->nr_tasks = 0;
		trace->task_idx = 0;
		trace->entries = NULL;
	}

	return true;
}

/* interrupts should be off from __schedule() */
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
static void probe_sched_switch(void *priv,
			       struct task_struct *prev,
			       struct task_struct *next)
#else
static void probe_sched_switch(void *priv, bool preempt,
			       struct task_struct *prev,
			       struct task_struct *next)
#endif
{
	struct runqlat_info *info = priv;
	int cpu = smp_processor_id();
	spinlock_t *lock = &info->lock;
	u64 now;

	if (info->pid == INVALID_PID)
		return;

	if (info->cpu != INVALID_CPU && info->cpu != cpu)
		return;

	now = cpu_clock(cpu);

	if (READ_ONCE(info->cpu) == INVALID_CPU) {
		if (READ_ONCE(info->pid) != prev->pid ||
		    prev->state != TASK_RUNNING)
			return;

		spin_lock(lock);
		/* We could race with grabbing lock */
		if (unlikely(info->cpu != INVALID_CPU ||
			     info->pid != prev->pid)) {
			spin_unlock(lock);
			return;
		}
		info->rq_start = cpu_clock(cpu);
		info->run_start = info->rq_start;
		info->cpu = task_cpu(prev);

		if (info->target_run_start != 0) {
			info->target_run_time = now - info->target_run_start;
		}

		/* update curr for migrate task probe using*/
		if (!is_idle_task(next))
			info->curr = next;
		else
			info->curr = NULL;

		spin_unlock(lock);
	} else {
		if (unlikely(READ_ONCE(info->cpu) != cpu ||
			     READ_ONCE(info->pid) == INVALID_PID))
			return;

		spin_lock(lock);
		/* We could race with grabbing lock */
		if (unlikely(info->cpu != cpu || info->pid == INVALID_PID)) {
			spin_unlock(lock);
			return;
		}

		/* update curr for migrate task probe using*/
		if (!is_idle_task(next))
			info->curr = next;
		else
			info->curr = NULL;

		if (info->pid == next->pid) {
			info->target_run_start = now;
			if (info->run_start)
				record_task(info, prev, now - info->run_start,
						RECORD_TASK_SCHED_SWITCH);
			record_task_commit(info, now - info->rq_start,
						info->target_run_time, cpu);
		} else if (info->pid == prev->pid) {
			if (info->target_run_start != 0) {
				info->target_run_time = now -
						info->target_run_start;
			}

			if (prev->state == TASK_RUNNING) {
				info->rq_start = now;
				info->run_start = now;
			} else {
				runqlat_info_reset(info);
			}
		} else {
			if (info->run_start)
				record_task(info, prev, now - info->run_start,
						RECORD_TASK_SCHED_SWITCH);
			info->run_start = now;
		}
		spin_unlock(lock);
	}
}

static void probe_sched_migrate_task(void *priv, struct task_struct *p, int cpu)
{
	u64 now;
	struct runqlat_info *info = priv;
	struct task_struct *curr;

	if (p->pid != info->pid || info->cpu == INVALID_CPU)
		return;

	/* interrupts should be off from set_task_cpu() */
	spin_lock(&info->lock);
	if (unlikely(p->pid != info->pid || info->cpu == INVALID_CPU))
		goto unlock;

	now = local_clock();
	curr = info->curr;
	if (curr) {
		get_task_struct(curr);
		if (info->run_start)
			record_task(info, curr, now - info->run_start,
					RECORD_TASK_MIGRATION);
		put_task_struct(curr);
	} else {
		if (info->run_start)
			record_task(info, NULL, now - info->run_start,
					RECORD_TASK_MIGRATION);
	}

	info->cpu = cpu;
	info->run_start = now;
	info->migrations++;
unlock:
	spin_unlock(&info->lock);
}

static struct tracepoints_probe tps_probe = {
	.tp_names = {
		"sched_wakeup",
		"sched_wakeup_new",
		"sched_switch",
		"sched_migrate_task",
	},
	.tp_probes = {
		probe_sched_wakeup,
		probe_sched_wakeup,
		probe_sched_switch,
		probe_sched_migrate_task,
	},
	.priv = &runqlat_info,
};

static inline bool is_tracepoint_lookup_success(struct tracepoints_probe *tps)
{
	return tps->num_initalized == PROBE_TRACEPOINTS;
}

static void __init tracepoint_lookup(struct tracepoint *tp, void *priv)
{
	int i;
	struct tracepoints_probe *tps = priv;

	if (is_tracepoint_lookup_success(tps))
		return;

	for (i = 0; i < ARRAY_SIZE(tps->tp_names); i++) {
		if (tps->tps[i] || strcmp(tp->name, tps->tp_names[i]))
			continue;
		tps->tps[i] = tp;
		tps->num_initalized++;
	}
}

static int trace_pid_show(struct seq_file *m, void *ptr)
{
	struct runqlat_info *info = m->private;

	seq_printf(m, "%d\n", info->pid);

	return 0;
}

static ssize_t trace_pid_store(void *priv, const char __user *buf, size_t count)
{
	struct runqlat_info *info = priv;
	unsigned long flags;
	int i, pid;
	struct task_struct *p;
	struct cgroup *cgrp;

	if (kstrtoint_from_user(buf, count, 0, &pid))
		return -EINVAL;

	if (info->pid != INVALID_PID && pid != INVALID_PID)
		return -EPERM;

	spin_lock_irqsave(&info->lock, flags);
	if (info->pid == pid)
		goto unlock;

	info->nr_trace = 0;
	info->trace_idx = 0;
	info->nr_task = 0;
	memset(info->trace_entries, 0,
	       max_trace_entries * sizeof(struct trace_entry) +
	       max_trace_entry_tasks * sizeof(struct task_entry));

	for (i = 0; i < LATENCY_HISTOGRAM_ENTRY; i++)
		info->latency_hist[i] = 0;
	runqlat_info_reset(info);
	smp_wmb();
	info->pid = pid;

#ifdef CONFIG_CGROUP_SCHED
	rcu_read_lock();
	/*
	 * We trace the process in the host enviroment, so we can assume
	 * the target PID is in the same namespace with 'current'.
	 */
	p = pid_task(find_vpid(pid), PIDTYPE_PID);
	if (!p) {
		rcu_read_unlock();
		goto unlock;
	}

#if LINUX_VERSION_CODE <  KERNEL_VERSION(3, 15, 0)
	cgrp = task_cgroup(p, cpu_cgroup_subsys_id);
#else
	/*
	 * Cgroup name changed in commit 073219e995b4
	 * ("cgroup: clean up cgroup_subsys names and initialization")
	 */
	cgrp = task_cgroup(p, cpu_cgrp_id);
#endif
	cgroup_path(cgrp, info->cgroup, sizeof(info->cgroup));
	rcu_read_unlock();
#endif

unlock:
	spin_unlock_irqrestore(&info->lock, flags);

	return count;
}

DEFINE_PROC_ATTRIBUTE_RW(trace_pid);

static int threshold_show(struct seq_file *m, void *ptr)
{
	struct runqlat_info *info = m->private;

	seq_printf(m, "%llu\n", info->threshold);

	return 0;
}

static ssize_t threshold_store(void *priv, const char __user *buf, size_t count)
{
	unsigned long threshold;
	struct runqlat_info *info = priv;

	if (kstrtoul_from_user(buf, count, 0, &threshold))
		return -EINVAL;

	info->threshold = threshold;

	return count;
}

DEFINE_PROC_ATTRIBUTE_RW(threshold);

static int runqlat_show(struct seq_file *m, void *ptr)
{
	int i, j;
	struct runqlat_info *info = m->private;
	unsigned long flags;

#if 0
	if (info->pid != INVALID_CPU)
		return -EPERM;
#endif

	/* TODO: It may be harmful when there're lots of output if disable the irq. */
	spin_lock_irqsave(&info->lock, flags);
	for (i = info->trace_idx; i < info->trace_idx + max_trace_entries; i++) {
		struct trace_entry *entry;
		unsigned long rem_nsec;
		unsigned long ts;
		int trace_idx;
		int nr_print;

		trace_idx = i < max_trace_entries ? i : (i - max_trace_entries);
		entry = info->trace_entries + trace_idx;

		nr_print = min_t(unsigned int, per_trace_tasks, entry->nr_tasks);
		if (!nr_print)
			continue;

		/* The format of timestamp is same with the output in dmesg */
		ts = entry->ts_nsec;
		rem_nsec = do_div(ts, 1000000000);
		seq_printf(m, "[%5lu.%06lu] [%03d] PID: %-8d %3d %*clatency(us): %llu "
			      "runtime(us): %llu runqlen: %d migrations: %d, cgroup: %s\n",
			   (unsigned long)ts, rem_nsec / 1000, entry->cpu, info->pid,
			   trace_idx, 2, ' ', entry->latency / 1000,
			   entry->runtime / 1000, entry->nr_tasks,
			   entry->migrations, info->cgroup);

		for (j = nr_print - 1; j >= 0; j--) {
			struct task_entry *task = index_to_task(info->task_entries, trace_idx, j);
			int nr;
			char state;

			switch (task->state) {
			case TASK_RUNNING:
				state = 'R';
				break;
			case TASK_INTERRUPTIBLE:
				state = 'S';
				break;
			case TASK_UNINTERRUPTIBLE:
				state = 'D';
				break;
			default:
				state = 'O';
				break;
			}

			ts = task->ts_nsec;
			rem_nsec = do_div(ts, 1000000000);
			/*
			 * The CGROUP is none if cpu cgroup is not enabled.
			 * Let's keep the format of the output consistent.
			 */
			seq_printf(m, "[%5lu.%06lu] [%03d] %*cCOMM: %-16s PID: %-8d "
				      "RUNTIME(us): %6llu CGROUP: %s state: %c nr_running(at %s):",
				   (unsigned long)ts, rem_nsec / 1000, task->cpu,
				   6, ' ', task->comm, task->pid,
				   task->runtime / 1000, task->cgroup, state,
				   (task->nr_running[0] & (1 << 31)) ? "migrate_task" : "sched_switch" );
			for (nr = 0; nr < task->nr_cgrp_hierarchy - 1; nr++) {
				seq_printf(m, "%u/", task->nr_running[nr] & ~(1 << 31));
			}
			seq_printf(m, "%u\n", task->nr_running[nr] & ~(1 << 31));
		}

		seq_putc(m, '\n');
	}

	spin_unlock_irqrestore(&info->lock, flags);

	return 0;
}

static ssize_t runqlat_store(void *priv, const char __user *buf, size_t count)
{
	struct runqlat_info *info = priv;
	unsigned long flags;
	int i, clear;

	if (kstrtoint_from_user(buf, count, 0, &clear))
		return -EINVAL;

	if (clear != 0)
		return -EPERM;

	spin_lock_irqsave(&info->lock, flags);

	info->nr_trace = 0;
	info->trace_idx = 0;
	info->nr_task = 0;
	info->target_run_time = 0;
	memset(info->trace_entries, 0,
	       max_trace_entries * sizeof(struct trace_entry) +
	       max_trace_entry_tasks * sizeof(struct task_entry));

	for (i = 0; i < LATENCY_HISTOGRAM_ENTRY; i++)
		info->latency_hist[i] = 0;
	runqlat_info_reset(info);

	spin_unlock_irqrestore(&info->lock, flags);

	return count;
}
DEFINE_PROC_ATTRIBUTE_RW(runqlat);

#define NUMBER_CHARACTER	40

static bool histogram_show(struct seq_file *m, const char *header,
			   const unsigned long *hist, unsigned long size,
			   unsigned int factor)
{
	int i, zero_index = 0;
	unsigned long count_max = 0;

	for (i = 0; i < size; i++) {
		unsigned long count = hist[i];

		if (count > count_max)
			count_max = count;

		if (count)
			zero_index = i + 1;
	}
	if (count_max == 0)
		return false;

	/* print header */
	if (header)
		seq_printf(m, "%s\n", header);
	seq_printf(m, "%*c%s%*c : %-9s %s\n", 9, ' ', "msecs", 10, ' ', "count",
		   "distribution");

	for (i = 0; i < zero_index; i++) {
		int num;
		int scale_min, scale_max;
		char str[NUMBER_CHARACTER + 1];

		scale_max = 2 << i;
		scale_min = unlikely(i == 0) ? 1 : scale_max / 2;

		num = hist[i] * NUMBER_CHARACTER / count_max;
		memset(str, '*', num);
		memset(str + num, ' ', NUMBER_CHARACTER - num);
		str[NUMBER_CHARACTER] = '\0';

		seq_printf(m, "%10d -> %-10d : %-8lu |%s|\n",
			   scale_min * factor, scale_max * factor - 1,
			   hist[i], str);
	}

	return true;
}

static int distribution_show(struct seq_file *m, void *ptr)
{
	int i;
	unsigned long hist[LATENCY_HISTOGRAM_ENTRY];
	struct runqlat_info *info = m->private;

	for (i = 0; i < LATENCY_HISTOGRAM_ENTRY; i++)
		hist[i] = info->latency_hist[i];

	histogram_show(m, NULL, hist, LATENCY_HISTOGRAM_ENTRY, 1);

	return 0;
}

DEFINE_PROC_ATTRIBUTE_RO(distribution);

static int __init trace_runqlat_init(void)
{
	int i;
	void *buf;
	int ret = -ENOMEM;
	struct tracepoints_probe *tps = &tps_probe;
	struct proc_dir_entry *parent_dir;
	struct runqlat_info *info = &runqlat_info;

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
	struct tracepoint_iter ti;
#endif

	max_trace_entry_tasks = max_trace_entries * per_trace_tasks;
	buf = vzalloc(max_trace_entries * sizeof(struct trace_entry) +
		      max_trace_entry_tasks * sizeof(struct task_entry));
	if (!buf)
		return -ENOMEM;
	info->trace_entries = buf;
	info->task_entries = (void *)(info->trace_entries + max_trace_entries);

	parent_dir = proc_mkdir("trace_runqlat", NULL);
	if (!parent_dir)
		goto free_buf;

	if (!proc_create_data("pid", 0644, parent_dir, &trace_pid_fops, info))
		goto remove_proc;

	if (!proc_create_data("threshold", 0644, parent_dir, &threshold_fops,
			      info))
		goto remove_proc;

	if (!proc_create_data("runqlat", 0, parent_dir, &runqlat_fops, info))
		goto remove_proc;

	if (!proc_create_data("distribution", 0, parent_dir, &distribution_fops,
			      info))
		goto remove_proc;

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
	tracepoint_iter_reset(&ti);
	for (tracepoint_iter_start(&ti);
		 ti.tracepoint; tracepoint_iter_next(&ti)) {
		tracepoint_lookup((struct tracepoint *)(*ti.tracepoint), tps);
	}
#else
	/* Lookup for the tracepoint that we needed */
	for_each_kernel_tracepoint(tracepoint_lookup, tps);
#endif

	if (!is_tracepoint_lookup_success(tps))
		goto remove_proc;

	for (i = 0; i < PROBE_TRACEPOINTS; i++) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 15, 0)
		ret = tracepoint_probe_register((const char *)tps->tps[i]->name, tps->tp_probes[i],
						tps->priv);
#else
		/*
		 * Function tracepoint_probe_register() was changed in commit
		 * de7b2973903c ("racepoint: Use struct pointer instead of name hash for reg/unreg tracepoint")
		 *
		 */
		ret = tracepoint_probe_register(tps->tps[i], tps->tp_probes[i], tps->priv);
#endif
		if (ret) {
			pr_err("sched trace: can not activate tracepoint "
			       "probe to %s\n", tps->tp_names[i]);
			while (i--)
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 15, 0)
				tracepoint_probe_unregister((const char *)tps->tps[i]->name,
							    tps->tp_probes[i],
							    tps->priv);
#else
				tracepoint_probe_unregister(tps->tps[i], tps->tp_probes[i], tps->priv);
#endif
			goto remove_proc;
		}
	}

	return 0;
remove_proc:
	remove_proc_subtree("trace_runqlat", NULL);
free_buf:
	vfree(buf);

	return ret;
}

static void __exit trace_runqlat_exit(void)
{
	int i;
	struct tracepoints_probe *tps = &tps_probe;
	struct runqlat_info *info = &runqlat_info;

	for (i = 0; i < PROBE_TRACEPOINTS; i++)
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 15, 0)
		tracepoint_probe_unregister((const char *)tps->tps[i]->name, tps->tp_probes[i],
					    tps->priv);
#else
		tracepoint_probe_unregister(tps->tps[i], tps->tp_probes[i], tps->priv);
#endif

	tracepoint_synchronize_unregister();
	remove_proc_subtree("trace_runqlat", NULL);
	vfree(info->trace_entries);
}

module_init(trace_runqlat_init);
module_exit(trace_runqlat_exit);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Muchun Song <songmuchun@bytedance.com>");
