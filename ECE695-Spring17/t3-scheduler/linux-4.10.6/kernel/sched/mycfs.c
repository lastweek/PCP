/*
 * MyCFS, modified after CFS
 * For Spring 2017 ECE695 task 3
 *
 * Yizhou Shan, March 2017
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
#include <linux/syscalls.h>

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

static inline struct mycfs_rq *task_mycfs_rq(struct task_struct *p)
{
	return &task_rq(p)->mycfs;
}

static inline struct mycfs_rq *mycfs_rq_of(struct sched_entity *se)
{
	struct task_struct *p = task_of(se);
	struct rq *rq = task_rq(p);

	return &rq->mycfs;
}

static inline u64 max_vruntime(u64 max_vruntime, u64 vruntime)
{
	s64 delta = (s64)(vruntime - max_vruntime);
	if (delta > 0)
		max_vruntime = vruntime;

	return max_vruntime;
}

static inline u64 min_vruntime(u64 min_vruntime, u64 vruntime)
{
	s64 delta = (s64)(vruntime - min_vruntime);
	if (delta < 0)
		min_vruntime = vruntime;

	return min_vruntime;
}

static inline int entity_before(struct sched_entity *a,
				struct sched_entity *b)
{
	return (s64)(a->vruntime - b->vruntime) < 0;
}

/*
 * delta /= w
 */
static inline u64 calc_delta_mycfs(u64 delta, struct sched_entity *se)
{
#if 0
	if (unlikely(se->load.weight != NICE_0_LOAD))
		delta = __calc_delta(delta, NICE_0_LOAD, &se->load);
#endif
	return delta;
}

static void update_min_vruntime(struct mycfs_rq *mycfs_rq)
{
	struct sched_entity *curr = mycfs_rq->curr;

	u64 vruntime = mycfs_rq->min_vruntime;

	if (curr) {
		if (curr->on_rq)
			vruntime = curr->vruntime;
		else
			curr = NULL;
	}

	if (mycfs_rq->rb_leftmost) {
		struct sched_entity *se = rb_entry(mycfs_rq->rb_leftmost,
						   struct sched_entity,
						   run_node);

		if (!curr)
			vruntime = se->vruntime;
		else
			vruntime = min_vruntime(vruntime, se->vruntime);
	}

	/* ensure we never gain time by being placed backwards. */
	mycfs_rq->min_vruntime = max_vruntime(mycfs_rq->min_vruntime, vruntime);
}

static void account_entity_enqueue(struct mycfs_rq *mycfs_rq,
				   struct sched_entity *se)
{
	mycfs_rq->nr_running++;
}

static void account_entity_dequeue(struct mycfs_rq *mycfs_rq,
				   struct sched_entity *se)
{
	mycfs_rq->nr_running--;
}

static void place_entity(struct mycfs_rq *mycfs_rq, struct sched_entity *se,
			 int initial)
{
	u64 vruntime = mycfs_rq->min_vruntime;

	/* sleeps up to a single latency don't count. */
	if (!initial)
		vruntime -= sysctl_sched_latency;

	/* ensure we never gain time by being placed backwards. */
	se->vruntime = max_vruntime(se->vruntime, vruntime);
}

/*
 * Enqueue an entity into the rb-tree:
 */
static void __enqueue_entity(struct mycfs_rq *mycfs_rq, struct sched_entity *se)
{
	struct rb_node **link = &mycfs_rq->tasks_timeline.rb_node;
	struct rb_node *parent = NULL;
	struct sched_entity *entry;
	int leftmost = 1;

	/* Find the right place in the rbtree: */
	while (*link) {
		parent = *link;
		entry = rb_entry(parent, struct sched_entity, run_node);
		/*
		 * We dont care about collisions. Nodes with
		 * the same key stay together.
		 */
		if (entity_before(se, entry)) {
			link = &parent->rb_left;
		} else {
			link = &parent->rb_right;
			leftmost = 0;
		}
	}

	/*
	 * Maintain a cache of leftmost tree entries
	 * (it is frequently used):
	 */
	if (leftmost)
		mycfs_rq->rb_leftmost = &se->run_node;

	rb_link_node(&se->run_node, parent, link);
	rb_insert_color(&se->run_node, &mycfs_rq->tasks_timeline);
}

static void __dequeue_entity(struct mycfs_rq *mycfs_rq, struct sched_entity *se)
{
	if (mycfs_rq->rb_leftmost == &se->run_node) {
		struct rb_node *next_node;

		next_node = rb_next(&se->run_node);
		mycfs_rq->rb_leftmost = next_node;
	}

	rb_erase(&se->run_node, &mycfs_rq->tasks_timeline);
}

struct sched_entity *__pick_first_entity_mycfs(struct mycfs_rq *mycfs_rq)
{
	struct rb_node *left = mycfs_rq->rb_leftmost;

	if (!left)
		return NULL;

	return rb_entry(left, struct sched_entity, run_node);
}

static struct sched_entity *__pick_next_entity(struct sched_entity *se)
{
	struct rb_node *next = rb_next(&se->run_node);

	if (!next)
		return NULL;

	return rb_entry(next, struct sched_entity, run_node);
}

/*
 * Update the current task's runtime statistics.
 */
static void update_curr(struct mycfs_rq *mycfs_rq)
{
	struct sched_entity *curr = mycfs_rq->curr;
	u64 now = rq_clock_task(rq_of(mycfs_rq));
	u64 delta_exec;

	if (unlikely(!curr))
		return;

	delta_exec = now - curr->exec_start;
	if (unlikely((s64)delta_exec <= 0))
		return;

	curr->exec_start = now;

	curr->sum_exec_runtime += delta_exec;

	curr->vruntime += calc_delta_mycfs(delta_exec, curr);
	update_min_vruntime(mycfs_rq);
}

static void update_curr_mycfs(struct rq *rq)
{
	update_curr(mycfs_rq_of(&rq->curr->se));
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
	bool renorm = !(flags & ENQUEUE_WAKEUP) || (flags & ENQUEUE_MIGRATED);
	bool curr = mycfs_rq->curr == se;

	/*
	 * If we're the current task, we must renormalise before calling
	 * update_curr().
	 */
	if (renorm && curr)
		se->vruntime += mycfs_rq->min_vruntime;

	update_curr(mycfs_rq);

	/*
	 * Otherwise, renormalise after, such that we're placed at the current
	 * moment in time, instead of some random moment in the past. Being
	 * placed in the past could significantly boost this task to the
	 * fairness detriment of existing tasks.
	 */
	if (renorm && !curr)
		se->vruntime += mycfs_rq->min_vruntime;

	account_entity_enqueue(mycfs_rq, se);

	if (flags & ENQUEUE_WAKEUP)
		place_entity(mycfs_rq, se, 0);

	if (!curr)
		__enqueue_entity(mycfs_rq, se);
	se->on_rq = 1;
}

static void dequeue_entity(struct mycfs_rq *mycfs_rq, struct sched_entity *se,
			   int flags)
{
	/*
	 * Update run-time statistics of the 'current'.
	 */
	update_curr(mycfs_rq);

	if (se != mycfs_rq->curr)
		__dequeue_entity(mycfs_rq, se);
	se->on_rq = 0;
	account_entity_dequeue(mycfs_rq, se);

	/*
	 * Normalize after update_curr(); which will also have moved
	 * min_vruntime if @se is the one holding it back. But before doing
	 * update_min_vruntime() again, which will discount @se's position and
	 * can move min_vruntime forward still more.
	 */
	if (!(flags & DEQUEUE_SLEEP))
		se->vruntime -= mycfs_rq->min_vruntime;

	/*
	 * Now advance min_vruntime if @se was the entity holding it back,
	 * except when: DEQUEUE_SAVE && !DEQUEUE_MOVE, in this case we'll be
	 * put back on, and if we advance min_vruntime, we'll be placed back
	 * further than we started -- ie. we'll be penalized.
	 */
	if ((flags & (DEQUEUE_SAVE | DEQUEUE_MOVE)) == DEQUEUE_SAVE)
		update_min_vruntime(mycfs_rq);
}

static void put_prev_entity(struct mycfs_rq *mycfs_rq, struct sched_entity *prev)
{
	if (prev->on_rq) {
		/*
		 * If still on the runqueue then deactivate_task()
		 * was not called and update_curr() has to be done:
		 */
		update_curr(mycfs_rq);

		/* Put 'current' back into the tree. */
		__enqueue_entity(mycfs_rq, prev);
	}
	mycfs_rq->curr = NULL;
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

	enqueue_entity(mycfs_rq, se, flags);
	mycfs_rq->h_nr_running++;
	add_nr_running(rq, 1);
}

/*
 * The dequeue_task method is called before nr_running is
 * decreased. We remove the task from the rbtree and
 * update the mycfs scheduling stats:
 */
static void dequeue_task_mycfs(struct rq *rq, struct task_struct *p, int flags)
{
	struct sched_entity *se = &p->se;
	struct mycfs_rq *mycfs_rq = mycfs_rq_of(se);

	dequeue_entity(mycfs_rq, se, flags);
	mycfs_rq->h_nr_running--;
	sub_nr_running(rq, 1);
}

#ifdef CONFIG_SMP
/*
 * idle_balance_mycfs is called by schedule() if this_cpu is about to become
 * idle. Attempts to pull tasks from other CPUs.
 */
static int idle_balance_mycfs(struct rq *this_rq)
{
	return 0;
}
#else
static int idle_balance(struct rq *this_rq) { return 0; }
#endif

/*
 * Pick next process to run:
 */
static struct sched_entity *pick_next_entity(struct mycfs_rq *mycfs_rq,
					     struct sched_entity *curr)
{
	struct sched_entity *left = __pick_first_entity_mycfs(mycfs_rq);
	struct sched_entity *se;

	/*
	 * If curr is set we have to see if its left of the leftmost entity
	 * still in the tree, provided there was anything in the tree at all.
	 */
	if (!left || (curr && entity_before(curr, left)))
		left = curr;

	se = left; /* ideally we run the leftmost entity */

	return se;
}

/*
 * Optional action to be done while updating the load average
 */
#define UPDATE_TG	0x1
#define SKIP_AGE_LOAD	0x2

/* Update task and its mycfs_rq load average */
static inline void update_load_avg(struct sched_entity *se, int flags)
{
}

/*
 * update_stats_curr_start
 * We are picking a new current task - update its stats:
 */
static inline void
update_stats_curr_start(struct mycfs_rq *mycfs_rq, struct sched_entity *se)
{
	/* We are starting a new run period: */
	se->exec_start = rq_clock_task(rq_of(mycfs_rq));
}

static void set_next_entity(struct mycfs_rq *mycfs_rq, struct sched_entity *se)
{
	/* 'current' is not kept within the tree. */
	if (se->on_rq) {
		__dequeue_entity(mycfs_rq, se);
		update_load_avg(se, UPDATE_TG);
	}

	update_stats_curr_start(mycfs_rq, se);
	mycfs_rq->curr = se;

	se->prev_sum_exec_runtime = se->sum_exec_runtime;
}

static struct task_struct *pick_next_task_mycfs(struct rq *rq,
						struct task_struct *prev,
						struct pin_cookie cookie)
{
	struct mycfs_rq *mycfs_rq = &rq->mycfs;
	struct sched_entity *se;
	struct task_struct *p;
	int new_tasks;

again:
	if (!mycfs_rq->nr_running)
		goto idle;
	
	put_prev_task(rq, prev);

	se = pick_next_entity(mycfs_rq, NULL);
	set_next_entity(mycfs_rq, se);

	p = task_of(se);

	return p;

idle:
	/*
	 * This is OK, because current is on_cpu, which avoids it being picked
	 * for load-balance and preemption/IRQs are still disabled avoiding
	 * further scheduler activity on it and we're being very careful to
	 * re-start the picking loop.
	 */
	lockdep_unpin_lock(&rq->lock, cookie);
	new_tasks = idle_balance_mycfs(rq);
	lockdep_repin_lock(&rq->lock, cookie);
	/*
	 * Because idle_balance() releases (and re-acquires) rq->lock, it is
	 * possible for any higher priority task to appear. In that case we
	 * must re-start the pick_next_entity() loop.
	 */
	if (new_tasks < 0)
		return RETRY_TASK;

	if (new_tasks > 0)
		goto again;

	return NULL;
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
 * Called from wake_up_new_task().
 * Preempt the current task with a newly woken task if needed:
 */
static void check_preempt_wakeup_mycfs(struct rq *rq, struct task_struct *p,
				       int wake_flags)
{
	/*
	 * Just do nothing, we do not want any preemption here
	 */
}

/*
 * put_prev_task_mycfs: account for a descheduled task.
 */
static void put_prev_task_mycfs(struct rq *rq, struct task_struct *prev)
{
	struct sched_entity *se;
	struct mycfs_rq *mycfs_rq;

	se = &prev->se;
	mycfs_rq = mycfs_rq_of(se);
	put_prev_entity(mycfs_rq, se);
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
 * mycfs_rq_of(p) references at time of call are still valid and identify the
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
 * This routine is mostly called to set mycfs_rq->curr field when a task
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
 * Called on fork with the child task as argument from the parent's context
 *  - child not yet on the tasklist
 *  - preemption disabled
 */
static void task_fork_mycfs(struct task_struct *p)
{
	struct mycfs_rq *mycfs_rq;
	struct sched_entity *curr, *se = &p->se;
	struct rq *rq = this_rq();

	raw_spin_lock(&rq->lock);
	update_rq_clock(rq);

	mycfs_rq = task_mycfs_rq(current);
	curr = mycfs_rq->curr;
	if (curr) {
		update_curr(mycfs_rq);
		se->vruntime = curr->vruntime;
	}
	place_entity(mycfs_rq, se, 1);

	se->vruntime -= mycfs_rq->min_vruntime;
	raw_spin_unlock(&rq->lock);
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

const struct sched_class mycfs_sched_class = {
	.next			= &idle_sched_class,
	.enqueue_task		= enqueue_task_mycfs,
	.dequeue_task		= dequeue_task_mycfs,
	.yield_task		= yield_task_mycfs,
	.yield_to_task		= yield_to_task_mycfs,

	.check_preempt_curr	= check_preempt_wakeup_mycfs,

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

SYSCALL_DEFINE2(sched_setlimit, pid_t, pid, int, limit)
{
	pr_info("sched_setlimit (pid: %d, limit: %d)\n",
		pid, limit);

	return 0;
}
