# filecache

一个独立编译的 Linux 内核模块（out-of-tree kernel module）。加载后在 `/proc` 下创建
`/proc/filecache`，读取该文件时会遍历所有已挂载文件系统的所有 inode，列出仍然驻留在
page cache（页缓存）中、页数大于 0 的文件。

用途：排查"内存被哪些文件的页缓存占用了"。典型场景是宿主机上运行大量容器、产生海量
日志文件时，快速找到占用 page cache 最多的文件。

模块只有一个源文件：`filecache.c`，GPL v2 许可。

## 工作原理

```
insmod
  └─ filecache_init()
       ├─ 通过 kallsyms_lookup_name() 获取内核函数 iterate_supers 的地址
       ├─ （老内核）再获取全局 inode_sb_list_lock 的地址
       └─ 创建 /proc/filecache

cat /proc/filecache
  └─ iterate_supers(iterate_pagecache_sb, m)   # 逐个 superblock 遍历
       └─ 对每个 superblock:
            ├─ 加锁（sb->s_inode_list_lock，或老内核的全局 inode_sb_list_lock）
            ├─ 遍历 sb->s_inodes 链表
            │    ├─ 加锁 inode->i_lock
            │    ├─ nrpages = inode->i_mapping->nrpages
            │    ├─ nrpages > 0 时：
            │    │    ├─ 能取到 dentry → dentry_path_raw() → 打印 "页数 路径"
            │    │    └─ dentry 已被回收 → 打印 "页数 inode号 No Dentry"
            │    └─ 解锁
            └─ 解锁

rmmod
  └─ filecache_exit() → remove_proc_entry()
```

核心指标是 `inode->i_mapping->nrpages`，即该文件在页缓存中的物理页数量（通常 1 页 = 4KB）。
文件路径由 dentry 解析得到；dentry 可能已被内核回收，此时只能显示 inode 号。

## 输出格式

```
  240983 /data/logs/example-app/app.log.2025010108
  269973 /data/logs/example-app/app.access.2025010107
  347370 123456789 No Dentry
 1673086 /data/logs/example-app/app.log.2025010109
```

- 第一列：该文件在内存（page cache）中的页数，页数 × 4KB ≈ 占用的内存。
- 第二列：通过 dentry 解析出的文件路径。
- dentry 已被回收时输出 `页数 inode号 No Dentry`，此时拿不到文件名，只能用 inode 号定位。

## 编译

只能在 Linux 上编译，且需要与运行内核匹配的头文件：

```bash
# CentOS/RHEL
yum install kernel-devel-$(uname -r) gcc make
# Ubuntu/Debian
apt install linux-headers-$(uname -r) build-essential

make                                    # 产出 filecache.ko
make KERNELDIR=/path/to/kernel/build    # 为其他内核编译（默认 /lib/modules/$(uname -r)/build）
make clean                              # 清理构建产物
```

内核构建系统会在源码目录产生 `*.ko *.o .*.cmd modules.order .tmp_versions/` 等中间文件，
`make clean` 只能清掉一部分，注意不要把这些文件提交到 git。

## 使用

```bash
sudo insmod filecache.ko        # 需要 root 权限；加载后 /proc/filecache 才存在
cat /proc/filecache             # 原始输出（未排序）
cat /proc/filecache | sort -rn | head -20    # 按页数从大到小看 Top 20

# 换算成 MB（按 4KB/页）
awk '{printf "%8.1f MB  %s\n", $1*4/1024, $2}' /proc/filecache | sort -rn | head -20

sudo rmmod filecache            # 卸载，/proc/filecache 随之消失
```

## 注意事项与局限

1. **只统计 page cache**：数据来自 `i_mapping->nrpages`，不包含匿名内存、slab（dentry/inode cache）等；
   `nrpages == 0` 的 inode 会被跳过。tmpfs/shm 中占用内存的文件同样基于页缓存，也会出现在结果中。
2. **路径语义**：`dentry_path_raw()` 返回的是相对于该文件系统根的路径，不含挂载点前缀；容器
   overlay 场景下，显示的路径可能和进程实际使用的路径不一致，这是正常的。存在硬链接时只显示
   `i_dentry` 链上的第一个别名，不一定是进程实际使用的文件名。
3. **性能敏感**：读取时会全程持有 `sb->s_inode_list_lock` 和每个 inode 的 `i_lock` 逐一解析路径。
   inode 数量大时（容器宿主机动辄百万级）`cat` 会很慢，期间还会阻塞相关内核路径的操作。
   **不要在线上频繁执行**，本质是一次全局扫描。
4. **Linux 5.7 及以上内核无法加载**：代码依赖 `kallsyms_lookup_name()` 获取内核符号，而该符号从
   5.7 起不再导出，`insmod` 会报 `Unknown symbol kallsyms_lookup_name`。这是已知限制，需要改造代码
   （例如通过 kprobe 取符号地址）才能在新内核上使用。
5. **无测试、无 CI**：验证方式就是在目标 Linux 机器上 `insmod` → `cat /proc/filecache` → `rmmod`。

## 内核兼容性

同一份代码通过 `LINUX_VERSION_CODE` 分叉，同时支持老 vendor 内核（3.10）和 4.x 内核：

| 项目 | `< 4.0` | `> 4.0` |
|---|---|---|
| dentry 别名字段 | `dentry->d_alias` | `dentry->d_u.d_alias` |
| inode 链表锁 | 全局 `inode_sb_list_lock`（通过 kallsyms 找到内核现成的锁，模块不自己初始化） | `sb->s_inode_list_lock` |
| proc 接口 | `proc_create` + 手写 `single_open` 的 fops | `proc_create_single` |

注意版本判断用的是 `>`：恰好 4.0.0 的内核走老分支，这是为了让 3.10-based 的 vendor 内核可用，
不要随意改成 `>=`。两个分支都需要保持可编译。
