/*
 * MyCFS, Copyright (c) 2017 Yizhou Shan
 * For Spring 2017 ECE696 task 3
 */

#include <linux/sched.h>
#include <linux/latencytop.h>
#include <linux/cpumask.h>
#include <linux/cpuidle.h>
#include <linux/slab.h>
#include <linux/profile.h>
#include <linux/interrupt.h>
#include <linux/mempolicy.h>
#include <linux/migrate.h>
#include <linux/task_work.h>

#include <trace/events/sched.h>

#include "sched.h"

static inline struct task_struct *task_of(struct sched_entity *se)
{
	return container_of(se, struct task_struct, se);
}

static inline struct rq *rq_of(struct mycfs_rq *mycfs_rq)
{
	return container_of(mycfs_rq, struct rq, mycfs);
}

static inline struct mycfs_rq *mycfs_rq_of(struct sched_entity *se)
{
	struct task_struct *p = task_of(se);
	struct rq *rq = task_rq(p);

	return &rq->mycfs;
}

/*
 * MIGRATION
 *
 *	dequeue
 *	  update_curr()
 *	    update_min_vruntime()
 *	  vruntime -= min_vruntime
 *
 *	enqueue
 *	  update_curr()
 *	    update_min_vruntime()
 *	  vruntime += min_vruntime
 *
 * this way the vruntime transition between RQs is done when both
 * min_vruntime are up-to-date.
 *
 * WAKEUP (remote)
 *
 *	->migrate_task_rq_fair() (p->state == TASK_WAKING)
 *	  vruntime -= min_vruntime
 *
 *	enqueue
 *	  update_curr()
 *	    update_min_vruntime()
 *	  vruntime += min_vruntime
 *
 * this way we don't have the most up-to-date min_vruntime on the originating
 * CPU and an up-to-date min_vruntime on the destination CPU.
 */

static void enqueue_entity(struct mycfs_rq *mycfs_rq, struct sched_entity *se, int flags)
{
}

/*
 * The enqueue_task method is called before nr_running is
 * increased. Here we update the mycfs scheduling stats and
 * then put the task into the rbtree:
 */
static void enqueue_task_mycfs(struct rq *rq, struct task_struct *p, int flags)
{
	struct sched_entity *se = &p->se;
	struct mycfs_rq *mycfs_rq = mycfs_rq_of(se);

	/*
	 * If in_iowait is set, the code below may not trigger any cpufreq
	 * utilization updates, so do it here explicitly with the IOWAIT flag
	 * passed.
	 */
	if (p->in_iowait)
		cpufreq_update_this_cpu(rq, SCHED_CPUFREQ_IOWAIT);

	if (!se->on_rq) {
		enqueue_entity(mycfs_rq, se, flags);
		mycfs_rq->h_nr_running++;
	}

	/* Update rq->nr_running */
	add_nr_running(rq, 1);

	pr_crit("%s task: %d/%s\n", __func__, p->pid, p->comm);
}

/*
 * The dequeue_task method is called before nr_running is
 * decreased. We remove the task from the rbtree and
 * update the mycfs scheduling stats:
 */
static void dequeue_task_mycfs(struct rq *rq, struct task_struct *p, int flags)
{
}

/*
 * sched_yield() is very simple
 *
 * The magic of dealing with the ->skip buddy is in pick_next_entity.
 */
static void yield_task_mycfs(struct rq *rq)
{
}

static bool yield_to_task_mycfs(struct rq *rq, struct task_struct *p, bool preempt)
{
	return false;
}

/*
 * Preempt the current task with a newly woken task if needed:
 */
static void check_preempt_wakeup(struct rq *rq, struct task_struct *p, int wake_flags)
{
}

static struct task_struct *pick_next_task_mycfs(struct rq *rq,
						struct task_struct *prev,
						struct pin_cookie cookie)
{
	return NULL;
}

/*
 * Account for a descheduled task:
 */
static void put_prev_task_mycfs(struct rq *rq, struct task_struct *prev)
{
}

#ifdef CONFIG_SMP

/*
 * select_task_rq_mycfs: Select target runqueue for the waking task in domains
 * that have the 'sd_flag' flag set. In practice, this is SD_BALANCE_WAKE,
 * SD_BALANCE_FORK, or SD_BALANCE_EXEC.
 *
 * Balances load by selecting the idlest cpu in the idlest group, or under
 * certain conditions an idle sibling cpu if the domain has SD_WAKE_AFFINE set.
 *
 * Returns the target cpu number.
 *
 * preempt must be disabled.
 */
static int
select_task_rq_mycfs(struct task_struct *p, int prev_cpu, int sd_flag, int wake_flags)
{
	return smp_processor_id();
}

/*
 * Called immediately before a task is migrated to a new cpu; task_cpu(p) and
 * cfs_rq_of(p) references at time of call are still valid and identify the
 * previous cpu. The caller guarantees p->pi_lock or task_rq(p)->lock is held.
 */
static void migrate_task_rq_mycfs(struct task_struct *p)
{
}

static void rq_online_mycfs(struct rq *rq)
{
}

static void rq_offline_mycfs(struct rq *rq)
{
}

static void task_dead_mycfs(struct task_struct *p)
{
}

#endif /* CONFIG_SMP */

/*
 * Account for a task changing its policy or group.
 *
 * This routine is mostly called to set cfs_rq->curr field when a task
 * migrates between groups/classes.
 */
static void set_curr_task_mycfs(struct rq *rq)
{
}

/*
 * scheduler tick hitting a task of our scheduling class:
 */
static void task_tick_mycfs(struct rq *rq, struct task_struct *curr, int queued)
{
}

/*
 * called on fork with the child task as argument from the parent's context
 *  - child not yet on the tasklist
 *  - preemption disabled
 */
static void task_fork_mycfs(struct task_struct *p)
{
}

/*
 * Priority of the task has changed. Check to see if we preempt
 * the current task.
 */
static void prio_changed_mycfs(struct rq *rq, struct task_struct *p, int oldprio)
{
}

static void switched_from_mycfs(struct rq *rq, struct task_struct *p)
{
}

static void switched_to_mycfs(struct rq *rq, struct task_struct *p)
{
}

static unsigned int get_rr_interval_mycfs(struct rq *rq, struct task_struct *task)
{
	unsigned int rr_interval = 0;


	return rr_interval;
}

static void update_curr_mycfs(struct rq *rq)
{
}

const struct sched_class mycfs_sched_class = {
	.next			= &idle_sched_class,
	.enqueue_task		= enqueue_task_mycfs,
	.dequeue_task		= dequeue_task_mycfs,
	.yield_task		= yield_task_mycfs,
	.yield_to_task		= yield_to_task_mycfs,

	.check_preempt_curr	= check_preempt_wakeup,

	.pick_next_task		= pick_next_task_mycfs,
	.put_prev_task		= put_prev_task_mycfs,

#ifdef CONFIG_SMP
	.select_task_rq		= select_task_rq_mycfs,
	.migrate_task_rq	= migrate_task_rq_mycfs,

	.rq_online		= rq_online_mycfs,
	.rq_offline		= rq_offline_mycfs,

	.task_dead		= task_dead_mycfs,
	.set_cpus_allowed	= set_cpus_allowed_common,
#endif

	.set_curr_task          = set_curr_task_mycfs,
	.task_tick		= task_tick_mycfs,
	.task_fork		= task_fork_mycfs,

	.prio_changed		= prio_changed_mycfs,
	.switched_from		= switched_from_mycfs,
	.switched_to		= switched_to_mycfs,

	.get_rr_interval	= get_rr_interval_mycfs,

	.update_curr		= update_curr_mycfs,
};

void init_mycfs_rq(struct mycfs_rq *mycfs_rq)
{
	mycfs_rq->tasks_timeline = RB_ROOT;
	mycfs_rq->min_vruntime = (u64)(-(1LL << 20));
}
