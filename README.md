5DOS-OS Kernel Module v2.0
=========================
"An Operating System with Hooks and K-Value Priority"

Author: Guiru Zhao &lt;zhaoguiru@gmail.com&gt;
Date: 2026-06-07
License: GPL

1. Architecture Overview
------------------------
5DOS-OS is a five-dimensional operating system prototype built on the Linux kernel, consisting of three layers:

1.1 Five-Dimensional Diagnostic Layer
   - Periodically scans all processes and computes the five-dimensional synergy coefficient κ
   - Outputs: /proc/5dos/top, /proc/5dos/status

1.2 Five-Dimensional Scheduling Layer
   - Kernel thread "5dos-sched" adjusts process nice values every 2 seconds based on κ
   - K-value priority mapping:
       STABLE  → nice = -20 (highest priority)
       GROWTH  → nice = -10
       BIRTH   → nice = 0
       IDLE    → nice = 10
       LATENT  → nice = 19 (lowest priority)
   - Control switch: echo 1 &gt; /proc/5dos/control

1.3 Five-Dimensional Hook Layer
   - kprobe intercepts kernel_clone (fork) and __schedule
   - Fork hook: triggers log when a new process is created
   - Schedule hook: forces rescheduling for low-synergy processes (LATENT/IDLE)
   - Control switch: echo 1 &gt; /proc/5dos/hook

2. Key Features of v2.0
-----------------------
2.1 Read-Write Lock Protection (rwlock)
   - The update timer acquires a write lock when writing to the global g structure
   - The scheduling thread and proc read functions acquire read locks when accessing g
   - Completely eliminates concurrent κ=0 misclassifications

2.2 Kernel Thread Protection (PF_KTHREAD)
   - Both the scheduling layer and hook layer skip kernel threads (irq, migration, watchdog, etc.)
   - Never modifies the nice value of kernel threads to avoid system crashes

2.3 Inner Synergy Coefficient (σ)
   - Computes the product of ten pairwise ratio matching degrees
   - Provides decomposition-level diagnosis of dimensional decoupling

3. /proc Interface
------------------
/proc/5dos/top       Top 20 entities ranked by κ (existing)
/proc/5dos/status    System-wide status (shows scheduling/hook switches)
/proc/5dos/schedmap  Scheduling map (shows only user-space process adjustments)
/proc/5dos/control   Write 1 to enable 5D scheduling, 0 to disable
/proc/5dos/hook      Write 1 to enable hook system, 0 to disable

4. Build Environment
--------------------
Test platform: VMware Workstation 17 + Ubuntu 22.04 LTS
Kernel version: Linux 6.8.0-111-generic
Compiler: gcc-12

5. Build and Load
-----------------
cd 5dos-os/src
make clean
make
sudo rmmod 5dos_os   # Unload old version if present
sudo insmod 5dos_os.ko

6. Enable 5D Scheduling
------------------------
echo 1 | sudo tee /proc/5dos/control
cat /proc/5dos/schedmap    # View priority adjustment records

7. Enable Hook System
---------------------
echo 1 | sudo tee /proc/5dos/hook
# View hook logs in dmesg: sudo dmesg | grep 5DOS-HOOK

8. View Output
--------------
cat /proc/5dos/top
cat /proc/5dos/status
cat /proc/5dos/schedmap

9. Unload
---------
sudo rmmod 5dos_os

10. File List
-------------
5dos-os.c   Kernel module source (diagnostic + scheduling + hook + lock protection, ~560 lines)
Makefile    Build script (with enable/disable/hook-on/hook-off shortcuts)
README      This document

11. Notes
---------
1. Scheduling intervention only modifies SCHED_NORMAL/BATCH/IDLE user-space processes; real-time processes and kernel threads are never touched.
2. The hook system depends on kprobe. If kernel_clone or __schedule symbols are not resolvable, hooks will gracefully degrade (print a warning without affecting diagnostic and scheduling functions).
3. The kernel thread 5dos-sched executes every 2 seconds; observe its effects via /proc/5dos/schedmap.
4. If an old version was previously loaded, be sure to run `sudo rmmod 5dos_os` before loading the new version.
5. The built-in edition uses subsys_initcall for automatic loading during kernel boot; for loadable module mode, use insmod/rmmod as shown above.
