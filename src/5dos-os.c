// 5dos-os.c - Five-Dimensional Operating System Kernel Module
// v1.0 "Ratio Method" (Lock-Safe + Kthread-Protect + Inner Synergy)
// Author: Guiru Zhao <zhaoguiru@gmail.com>
// License: GPL

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/mm.h>
#include <linux/jiffies.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/version.h>
#include <linux/ktime.h>
#include <linux/kthread.h>
#include <linux/kprobes.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/rwlock.h>
#include <asm/div64.h>

#define UPDATE_MS       2000
#define MAX_ENTITIES    1024
#define SCALE           1000000ULL
#define I_ONTO_FLOOR    100ULL

/* 存在论状态 */
enum {
    ST_BIRTH = 0, ST_GROWTH, ST_STABLE, ST_DECAY,
    ST_IDLE, ST_DEATH, ST_LATENT, ST_MAX
};
static const char * const st_name[] = {
    "BIRTH", "GROWTH", "STABLE", "DECAY", "IDLE", "DEATH", "LATENT"
};

struct entity {
    pid_t pid;
    char comm[TASK_COMM_LEN];
    unsigned long B;
    u64 R, D, I, kappa, sigma;
    int S, state, first;
    u64 prev_cpu, prev_ns;
};

struct sched_map {
    pid_t pid;
    char comm[TASK_COMM_LEN];
    int old_nice;
    int new_nice;
    u64 kappa;
    int state;
};

/* 全局数据 - 受 rwlock 保护 */
static struct {
    struct entity e[MAX_ENTITIES];
    int n;
    unsigned long max_B;
    u64 max_R, max_I, avg_kappa, avg_sigma;
    int max_S, cnt[ST_MAX];
} g;

static rwlock_t g_rwlock;

static struct {
    struct sched_map m[MAX_ENTITIES];
    int n;
} sm;

static struct entity *sort_ptr[MAX_ENTITIES];

static struct timer_list g_timer;
static struct task_struct *sched_thread;

static int sched_5d_enabled = 0;
static int hook_enabled = 0;
static DEFINE_SPINLOCK(ctrl_lock);

/* ========== 基础工具 ========== */
static unsigned long get_task_rss(struct task_struct *t)
{
    struct mm_struct *mm = get_task_mm(t);
    unsigned long r = 0;
    if (mm) {
#if defined(get_mm_rss)
        r = get_mm_rss(mm);
#else
        r = get_mm_counter(mm, MM_FILEPAGES) + get_mm_counter(mm, MM_ANONPAGES);
#endif
        mmput(mm);
    }
    return r;
}

static u64 get_cpu_ticks(struct task_struct *t)
{
    if (thread_group_leader(t) && t->signal)
        return t->signal->utime + t->signal->stime;
    return t->utime + t->stime;
}

static int get_task_threads(struct task_struct *t)
{
    return t->signal ? t->signal->nr_threads : 1;
}

static unsigned int get_task_state_val(struct task_struct *t)
{
    return READ_ONCE(t->__state);
}

static void norm_onto_u64(u64 raw, u64 global_max, u64 floor, u64 *out)
{
    if (global_max <= floor) { *out = SCALE; return; }
    u64 eff = raw < floor ? floor : raw;
    *out = div64_u64(eff * SCALE, global_max + floor);
}

/* 比值法匹配度：γ(a,b) = min(a,b) / max(a,b) ，定点数版本 */
static u64 ratio_match_u64(u64 a, u64 b)
{
    u64 mn = min(a, b);
    u64 mx = max(a, b);
    if (mx == 0) return SCALE;
    return div64_u64(mn * SCALE, mx);
}

/* ========== 五维诊断层（写锁保护） ========== */
static void update(struct timer_list *tm)
{
    struct task_struct *t;
    struct entity *ent;
    int i = 0;
    u64 now_ns = ktime_get_ns(), nB, nR, nS, nD, nI, temp;
    unsigned int s;

    write_lock(&g_rwlock);
    memset(&g, 0, sizeof(g));
    rcu_read_lock();
    for_each_process(t) {
        if (i >= MAX_ENTITIES) break;
        ent = &g.e[i];
        ent->pid = t->pid;
        get_task_comm(ent->comm, t);
        ent->B = get_task_rss(t);
        ent->R = get_cpu_ticks(t);
        ent->S = get_task_threads(t);

        s = get_task_state_val(t);
        if (t->exit_state & (EXIT_ZOMBIE | EXIT_DEAD)) ent->D = SCALE/20;
        else if (s == TASK_RUNNING) ent->D = SCALE;
        else if (s == TASK_INTERRUPTIBLE) ent->D = SCALE*7/10;
        else if (s == TASK_UNINTERRUPTIBLE) ent->D = SCALE/2;
        else if (s == TASK_STOPPED || s == TASK_TRACED) ent->D = SCALE/5;
        else ent->D = SCALE*6/10;

        if (!ent->first) {
            u64 curr = ent->R, dns = now_ns - ent->prev_ns;
            if (dns && curr >= ent->prev_cpu) {
                u64 diff = curr - ent->prev_cpu;
                u64 I_val = diff * 100;
                do_div(I_val, dns);
                ent->I = I_val * SCALE;
            } else ent->I = 0;
        } else { ent->I = 0; ent->first = 0; }
        ent->prev_cpu = ent->R; ent->prev_ns = now_ns;

        if (ent->B > g.max_B) g.max_B = ent->B;
        if (ent->R > g.max_R) g.max_R = ent->R;
        if (ent->S > g.max_S) g.max_S = ent->S;
        if (ent->I > g.max_I) g.max_I = ent->I;
        i++;
    }
    rcu_read_unlock();
    g.n = i;

    if (g.max_B < 1) g.max_B = 1;
    if (g.max_R < 1) g.max_R = 1;
    if (g.max_S < 1) g.max_S = 1;
    if (g.max_I < I_ONTO_FLOOR) g.max_I = I_ONTO_FLOOR;

    u64 sum_k = 0, sum_s = 0;
    for (int j = 0; j < g.n; j++) {
        ent = &g.e[j];
        norm_onto_u64((u64)ent->B, (u64)g.max_B, 0, &nB);
        norm_onto_u64(ent->R, g.max_R, 0, &nR);
        norm_onto_u64((u64)ent->S, (u64)g.max_S, 0, &nS);
        nD = ent->D;
        norm_onto_u64(ent->I, g.max_I, I_ONTO_FLOOR, &nI);

        /* 外协同系数 κ = ∏ n_d （乘积形式，与比值法一致） */
        temp = nB * nR; do_div(temp, SCALE);
        temp = temp * nS; do_div(temp, SCALE);
        temp = temp * nD; do_div(temp, SCALE);
        temp = temp * nI; do_div(temp, SCALE);
        ent->kappa = temp;

        /* 内协同系数 σ = ∏_{i<j} γ(n_i, n_j) */
        u64 g_br = ratio_match_u64(nB, nR);
        u64 g_bs = ratio_match_u64(nB, nS);
        u64 g_bd = ratio_match_u64(nB, nD);
        u64 g_bi = ratio_match_u64(nB, nI);
        u64 g_rs = ratio_match_u64(nR, nS);
        u64 g_rd = ratio_match_u64(nR, nD);
        u64 g_ri = ratio_match_u64(nR, nI);
        u64 g_sd = ratio_match_u64(nS, nD);
        u64 g_si = ratio_match_u64(nS, nI);
        u64 g_di = ratio_match_u64(nD, nI);

        temp = g_br * g_bs; do_div(temp, SCALE);
        temp = temp * g_bd; do_div(temp, SCALE);
        temp = temp * g_bi; do_div(temp, SCALE);
        temp = temp * g_rs; do_div(temp, SCALE);
        temp = temp * g_rd; do_div(temp, SCALE);
        temp = temp * g_ri; do_div(temp, SCALE);
        temp = temp * g_sd; do_div(temp, SCALE);
        temp = temp * g_si; do_div(temp, SCALE);
        temp = temp * g_di; do_div(temp, SCALE);
        ent->sigma = temp;

        /* 存在论状态判断（κ 与 σ 共同参考） */
        if (ent->I < I_ONTO_FLOOR && ent->B > 0) ent->state = ST_IDLE;
        else if (ent->kappa > SCALE/2) ent->state = ST_STABLE;
        else if (ent->kappa > SCALE/20) ent->state = ST_GROWTH;
        else if (ent->kappa < SCALE/1000 || ent->sigma < SCALE/1000) ent->state = ST_LATENT;
        else ent->state = ST_BIRTH;

        g.cnt[ent->state]++;
        sum_k += ent->kappa;
        sum_s += ent->sigma;
    }
    g.avg_kappa = g.n ? div64_u64(sum_k, (u64)g.n) : 0;
    g.avg_sigma = g.n ? div64_u64(sum_s, (u64)g.n) : 0;
    write_unlock(&g_rwlock);

    mod_timer(&g_timer, jiffies + msecs_to_jiffies(UPDATE_MS));
}

/* ========== 五维调度层 ========== */
static int kappa_to_nice(u64 kappa, int state)
{
    if (state == ST_STABLE) return -20;
    if (state == ST_GROWTH) return -10;
    if (state == ST_BIRTH)  return 0;
    if (state == ST_IDLE)   return 10;
    if (state == ST_LATENT) return 19;
    return 19;
}

static void apply_5d_priority(struct task_struct *t, int nice)
{
    /* 绝对不碰内核线程 */
    if (t->flags & PF_KTHREAD) return;
    /* 只修改普通用户态进程 */
    if (t->policy != SCHED_NORMAL && t->policy != SCHED_BATCH && t->policy != SCHED_IDLE)
        return;

    int new_prio = NICE_TO_PRIO(nice);
    t->static_prio = new_prio;
    t->prio = new_prio;
    t->normal_prio = new_prio;
    set_tsk_need_resched(t);
}

static int sched_5d_fn(void *data)
{
    struct task_struct *t;
    struct entity *ent;
    int nice, idx;

    while (!kthread_should_stop()) {
        if (sched_5d_enabled) {
            memset(&sm, 0, sizeof(sm));
            idx = 0;

            read_lock(&g_rwlock);
            if (g.n > 0) {
                rcu_read_lock();
                for_each_process(t) {
                    if (idx >= MAX_ENTITIES) break;
                    /* 跳过内核线程 */
                    if (t->flags & PF_KTHREAD) continue;
                    for (int j = 0; j < g.n; j++) {
                        ent = &g.e[j];
                        if (ent->pid == t->pid) {
                            nice = kappa_to_nice(ent->kappa, ent->state);
                            int old_nice = PRIO_TO_NICE(t->static_prio);
                            if (old_nice != nice) {
                                apply_5d_priority(t, nice);
                                sm.m[idx].pid = t->pid;
                                get_task_comm(sm.m[idx].comm, t);
                                sm.m[idx].old_nice = old_nice;
                                sm.m[idx].new_nice = nice;
                                sm.m[idx].kappa = ent->kappa;
                                sm.m[idx].state = ent->state;
                                idx++;
                            }
                            break;
                        }
                    }
                }
                rcu_read_unlock();
            }
            read_unlock(&g_rwlock);
            sm.n = idx;
        }
        msleep(UPDATE_MS);
    }
    return 0;
}

/* ========== 五维钩子层 ========== */
static struct kprobe kp_fork, kp_sched;

static int handler_fork_pre(struct kprobe *p, struct pt_regs *regs)
{
    if (hook_enabled)
        pr_info("5DOS-HOOK: fork intercepted\n");
    return 0;
}

static int handler_sched_pre(struct kprobe *p, struct pt_regs *regs)
{
    if (hook_enabled) {
        struct task_struct *t = current;
        /* 只干预用户态进程 */
        if (t->flags & PF_KTHREAD) return 0;
        read_lock(&g_rwlock);
        for (int j = 0; j < g.n; j++) {
            if (g.e[j].pid == t->pid) {
                if (g.e[j].state == ST_LATENT || g.e[j].state == ST_IDLE) {
                    set_tsk_need_resched(t);
                }
                break;
            }
        }
        read_unlock(&g_rwlock);
    }
    return 0;
}

static int hook_init(void)
{
    int ret;
    kp_fork.symbol_name = "kernel_clone";
    kp_fork.pre_handler = handler_fork_pre;
    ret = register_kprobe(&kp_fork);
    if (ret < 0) {
        pr_warn("5DOS-HOOK: fork hook failed (%d), continuing\n", ret);
    } else {
        pr_info("5DOS-HOOK: fork hook registered\n");
    }

    kp_sched.symbol_name = "__schedule";
    kp_sched.pre_handler = handler_sched_pre;
    ret = register_kprobe(&kp_sched);
    if (ret < 0) {
        pr_warn("5DOS-HOOK: schedule hook failed (%d), continuing\n", ret);
    } else {
        pr_info("5DOS-HOOK: schedule hook registered\n");
    }
    return 0;
}

static void hook_exit(void)
{
    unregister_kprobe(&kp_fork);
    unregister_kprobe(&kp_sched);
    pr_info("5DOS-HOOK: all hooks unregistered\n");
}

/* ========== /proc 接口（读锁保护） ========== */
static int show_top(struct seq_file *m, void *v)
{
    int c = 0;
    read_lock(&g_rwlock);
    for (int i = 0; i < g.n; i++) sort_ptr[i] = &g.e[i];
    for (int i = 0; i < g.n - 1; i++)
        for (int j = 0; j < g.n - 1 - i; j++)
            if (sort_ptr[j]->kappa < sort_ptr[j+1]->kappa) {
                struct entity *t = sort_ptr[j];
                sort_ptr[j] = sort_ptr[j+1];
                sort_ptr[j+1] = t;
            }

    seq_printf(m, "Top Active Entities by Kappa (Ratio Method, Ontological Floor Applied)\n");
    seq_printf(m, "==========================================================================\n");
    seq_printf(m, "RANK PID     COMM                      KAPPA    SIGMA    STATE   I(%%)  TH\n");
    seq_printf(m, "--------------------------------------------------------------------------\n");
    for (int i = 0; i < g.n && c < 20; i++) {
        if (sort_ptr[i]->kappa <= 0) continue;
        seq_printf(m, "%-4d %-7d %-20s %2llu.%06llu %2llu.%06llu %-7s %3llu.%02llu %3d\n",
            ++c, sort_ptr[i]->pid, sort_ptr[i]->comm,
            sort_ptr[i]->kappa / SCALE, sort_ptr[i]->kappa % SCALE,
            sort_ptr[i]->sigma / SCALE, sort_ptr[i]->sigma % SCALE,
            st_name[sort_ptr[i]->state],
            sort_ptr[i]->I / SCALE, (sort_ptr[i]->I / (SCALE/100)) % 100,
            sort_ptr[i]->S);
    }
    read_unlock(&g_rwlock);
    return 0;
}
static int open_top(struct inode *i, struct file *f) { return single_open(f, show_top, NULL); }
static const struct proc_ops top_ops = {
    .proc_open=open_top, .proc_read=seq_read, .proc_lseek=seq_lseek, .proc_release=single_release,
};

static int show_status(struct seq_file *m, void *v)
{
    read_lock(&g_rwlock);
    seq_printf(m, "5DOS-OS Kernel Module v1.0 (Ratio Method)\n");
    seq_printf(m, "============================================================\n");
    seq_printf(m, "System Average Kappa:  %llu.%06llu\n", g.avg_kappa/SCALE, g.avg_kappa%SCALE);
    seq_printf(m, "System Average Sigma:  %llu.%06llu\n", g.avg_sigma/SCALE, g.avg_sigma%SCALE);
    seq_printf(m, "Total Entities:        %d\n", g.n);
    seq_printf(m, "Update Interval:       %d ms\n\n", UPDATE_MS);
    seq_printf(m, "5D-Scheduling:       %s\n", sched_5d_enabled ? "ENABLED" : "DISABLED");
    seq_printf(m, "5D-Hooks:            %s\n\n", hook_enabled ? "ENABLED" : "DISABLED");
    seq_printf(m, "Global Maximums:\n");
    seq_printf(m, "  B (RSS):           %lu pages\n", g.max_B);
    seq_printf(m, "  R (CPU):           %llu ticks\n", g.max_R);
    seq_printf(m, "  S (Threads):       %d\n", g.max_S);
    seq_printf(m, "  I (Intensity max): %llu.%02llu %%\n\n", g.max_I/SCALE, (g.max_I/(SCALE/100))%100);
    seq_printf(m, "Ontological State Distribution:\n");
    for (int i = 0; i < ST_MAX; i++)
        seq_printf(m, "  %-7s : %4d\n", st_name[i], g.cnt[i]);
    seq_printf(m, "\nCore Formula (Ratio Method):\n");
    seq_printf(m, "  Matching: γ(a,b) = min(a,b) / max(a,b)\n");
    seq_printf(m, "  κ (External) = ∏ γ(x_d, 1.0)\n");
    seq_printf(m, "  σ (Internal) = ∏_{i<j} γ(x_i, x_j)\n");
    seq_printf(m, "Axiom: \"Intensity dormancy does NOT equal Boundary death\"\n");
    read_unlock(&g_rwlock);
    return 0;
}
static int open_status(struct inode *i, struct file *f) { return single_open(f, show_status, NULL); }
static const struct proc_ops status_ops = {
    .proc_open=open_status, .proc_read=seq_read, .proc_lseek=seq_lseek, .proc_release=single_release,
};

static int show_schedmap(struct seq_file *m, void *v)
{
    seq_printf(m, "5D Scheduling Map (last update, kthread excluded)\n");
    seq_printf(m, "============================================================\n");
    seq_printf(m, "PID     COMM                 OLD_NICE  NEW_NICE  KAPPA    STATE\n");
    seq_printf(m, "------------------------------------------------------------\n");
    for (int i = 0; i < sm.n && i < 50; i++) {
        seq_printf(m, "%-7d %-20s %3d       %3d       %2llu.%06llu %-7s\n",
            sm.m[i].pid, sm.m[i].comm,
            sm.m[i].old_nice, sm.m[i].new_nice,
            sm.m[i].kappa / SCALE, sm.m[i].kappa % SCALE,
            st_name[sm.m[i].state]);
    }
    if (sm.n == 0)
        seq_printf(m, "(No priority adjustments in last cycle)\n");
    return 0;
}
static int open_schedmap(struct inode *i, struct file *f) { return single_open(f, show_schedmap, NULL); }
static const struct proc_ops schedmap_ops = {
    .proc_open=open_schedmap, .proc_read=seq_read, .proc_lseek=seq_lseek, .proc_release=single_release,
};

/* control 接口 */
static ssize_t write_control(struct file *f, const char __user *buf, size_t len, loff_t *off)
{
    char kbuf[4];
    if (len > 3) len = 3;
    if (copy_from_user(kbuf, buf, len)) return -EFAULT;
    kbuf[len] = 0;
    if (kbuf[0] == '1') {
        spin_lock(&ctrl_lock);
        sched_5d_enabled = 1;
        spin_unlock(&ctrl_lock);
        pr_info("5DOS-OS: 5D scheduling ENABLED\n");
    } else if (kbuf[0] == '0') {
        spin_lock(&ctrl_lock);
        sched_5d_enabled = 0;
        spin_unlock(&ctrl_lock);
        pr_info("5DOS-OS: 5D scheduling DISABLED\n");
    }
    return len;
}
static const struct proc_ops control_ops = {
    .proc_write = write_control,
};

/* hook 接口 */
static ssize_t write_hook(struct file *f, const char __user *buf, size_t len, loff_t *off)
{
    char kbuf[4];
    if (len > 3) len = 3;
    if (copy_from_user(kbuf, buf, len)) return -EFAULT;
    kbuf[len] = 0;
    if (kbuf[0] == '1') {
        spin_lock(&ctrl_lock);
        hook_enabled = 1;
        spin_unlock(&ctrl_lock);
        pr_info("5DOS-OS: hooks ENABLED\n");
    } else if (kbuf[0] == '0') {
        spin_lock(&ctrl_lock);
        hook_enabled = 0;
        spin_unlock(&ctrl_lock);
        pr_info("5DOS-OS: hooks DISABLED\n");
    }
    return len;
}
static const struct proc_ops hook_proc_ops = {
    .proc_write = write_hook,
};

/* ========== 模块加载/卸载 ========== */
static int __init init_5dos_os(void)
{
    struct proc_dir_entry *d;
    rwlock_init(&g_rwlock);

    d = proc_mkdir("5dos", NULL);
    if (!d) return -ENOMEM;
    if (!proc_create("top", 0444, d, &top_ops)) return -ENOMEM;
    if (!proc_create("status", 0444, d, &status_ops)) return -ENOMEM;
    if (!proc_create("schedmap", 0444, d, &schedmap_ops)) return -ENOMEM;
    if (!proc_create("control", 0222, d, &control_ops)) return -ENOMEM;
    if (!proc_create("hook", 0222, d, &hook_proc_ops)) return -ENOMEM;

    timer_setup(&g_timer, update, 0);
    mod_timer(&g_timer, jiffies + msecs_to_jiffies(100));

    sched_thread = kthread_run(sched_5d_fn, NULL, "5dos-sched");
    if (IS_ERR(sched_thread))
        pr_warn("5DOS-OS: sched thread failed to start\n");
    else
        pr_info("5DOS-OS: sched thread started\n");

    hook_init();

    pr_info("5DOS-OS: loaded v1.0 (ratio method + inner synergy)\n");
    return 0;
}

static void __exit exit_5dos_os(void)
{
    hook_exit();
    if (sched_thread) kthread_stop(sched_thread);
    del_timer_sync(&g_timer);
    remove_proc_subtree("5dos", NULL);
    pr_info("5DOS-OS: unloaded\n");
}

module_init(init_5dos_os);
module_exit(exit_5dos_os);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Guiru Zhao <zhaoguiru@gmail.com>");
MODULE_DESCRIPTION("5DOS Operating System Kernel Module v1.0 (Ratio Method)");
