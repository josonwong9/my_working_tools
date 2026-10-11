#include "vmlinux.h"
#include "common.h"
#include "bpf_helpers.h"
#include "bpf_tracing.h"

#define NSEC_PER_SEC 1000000000UL
#define SYS_THRESHOLD 50
#define TASK_COMM_LEN 16

char __license[] SEC("license") = "Dual MIT/GPL";

struct stat_t {
	u64 ts;
	u64 curr_sys;
	u64 last_sys;
};

struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __type(key, u32);
    __type(value, struct stat_t);
    __uint(max_entries, 1);
} systime SEC(".maps");

#define PERF_MAX_STACK_DEPTH         127

struct {
        __uint(type, BPF_MAP_TYPE_STACK_TRACE);
        __uint(key_size, sizeof(u32));
        __uint(value_size, PERF_MAX_STACK_DEPTH * sizeof(u64));
	__uint(pinning, LIBBPF_PIN_BY_NAME);
        __uint(max_entries, 10000);
} stackmap SEC(".maps");

struct task_t {
	u32 stackid;
	char comm[TASK_COMM_LEN];
	u32 cpu;
	u64 ts;
	/* use u32 for sys here would trigger an weird error */
	u64 sys;
};

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __type(key, u32);
    __type(value, struct task_t);
    __uint(pinning, LIBBPF_PIN_BY_NAME);
    __uint(max_entries, 10000);
} taskinfo SEC(".maps");

SEC("kprobe/account_system_index_time")
int account_system_index_time_entry(struct pt_regs *ctx)
{
	u32 key = 0, rate, pid, stackid, cpu;
        u64 ts, interval;
	struct stat_t *stat;
	struct task_t task;

	/* void account_system_index_time(struct task_struct *p, u64 cputime, enum cpu_usage_stat index) */
	struct task_struct *p = (struct task_struct *) PT_REGS_PARM1(ctx);
	u64 cputime 	      = (u64) PT_REGS_PARM2(ctx);
	int index 	      = (int) PT_REGS_PARM3(ctx);


	stat = bpf_map_lookup_elem(&systime, &key);
	if (!stat)
		return 0;

	ts = bpf_ktime_get_ns();
	if (stat->ts == 0) {
		/* first record */
		stat->ts = ts;
		stat->curr_sys = 0;
		stat->last_sys = 0;
		return 0;
	}

	stat->curr_sys += cputime;

	interval = ts - stat->ts;
	/* sample per second */
	if (interval > NSEC_PER_SEC) {
		/* get sys percentage */
		rate = (stat->curr_sys - stat->last_sys) * 100 / interval;
		if (rate > SYS_THRESHOLD) {
			cpu = bpf_get_smp_processor_id();

			/* htop runs on CPU0, filter it */
			if (cpu != 0) {
				/* record stack info */
				stackid = bpf_get_stackid(ctx, &stackmap, 0);

				/* record task info */
				pid = bpf_get_current_pid_tgid();
				bpf_get_current_comm(&task.comm, sizeof(task.comm));
				task.stackid = stackid;
				task.cpu = cpu;
				task.ts = ts;
				task.sys = rate;
				bpf_map_update_elem(&taskinfo, &pid, &task, BPF_ANY);
			}
		}

		/* update stat */
		stat->last_sys = stat->curr_sys;
		stat->ts = ts;
	}

	return 0;
}
