/*
 * MyCFS, modified after CFS
 * For Spring 2017 ECE695 task 3
 *
 * Yizhou Shan, March 2017
 */

/*
 * Note:
 *
 * 1) When a task call sched_setscheduler() to switch to MyCFS,
 *    the task will be dequeued and put from previous class first
 *    after that it will call enqueue_task() and set_curr_task()
 *    to put the task into MyCFS class. After all this, the
 *    switched_to_mycfs() callback will be invoked.
 *
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

static inline void set_last_buddy(struct sched_entity *se)
{
	mycfs_rq_of(se)->last = se;
}

static inline void set_next_buddy(struct sched_entity *se)
{
	mycfs_rq_of(se)->next = se;
}

static inline void set_skip_buddy(struct sched_entity *se)
{
	mycfs_rq_of(se)->skip = se;
}

static inline void __clear_buddies_last(struct sched_entity *se)
{
	mycfs_rq_of(se)->last = NULL;
}

static inline void __clear_buddies_next(struct sched_entity *se)
{
	mycfs_rq_of(se)->next = NULL;
}

static inline void __clear_buddies_skip(struct sched_entity *se)
{
	mycfs_rq_of(se)->skip = NULL;
}

static void clear_buddies(struct mycfs_rq *mycfs_rq, struct sched_entity *se)
{
	if (mycfs_rq->last == se)
		__clear_buddies_last(se);

	if (mycfs_rq->next == se)
		__clear_buddies_next(se);

	if (mycfs_rq->skip == se)
		__clear_buddies_skip(se);
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

#define WMULT_CONST	(~0U)
#define WMULT_SHIFT	32

static void __update_inv_weight(struct load_weight *lw)
{
	unsigned long w;

	if (likely(lw->inv_weight))
		return;

	w = scale_load_down(lw->weight);

	if (BITS_PER_LONG > 32 && unlikely(w >= WMULT_CONST))
		lw->inv_weight = 1;
	else if (unlikely(!w))
		lw->inv_weight = WMULT_CONST;
	else
		lw->inv_weight = WMULT_CONST / w;
}

/*
 * delta_exec * weight / lw.weight
 *   OR
 * (delta_exec * (weight * lw->inv_weight)) >> WMULT_SHIFT
 *
 * Either weight := NICE_0_LOAD and lw \e sched_prio_to_wmult[], in which case
 * we're guaranteed shift stays positive because inv_weight is guaranteed to
 * fit 32 bits, and NICE_0_LOAD gives another 10 bits; therefore shift >= 22.
 *
 * Or, weight =< lw.weight (because lw.weight is the runqueue weight), thus
 * weight/lw.weight <= 1, and therefore our shift will also be positive.
 */
static u64 __calc_delta(u64 delta_exec, unsigned long weight, struct load_weight *lw)
{
	u64 fact = scale_load_down(weight);
	int shift = WMULT_SHIFT;

	__update_inv_weight(lw);

	if (unlikely(fact >> 32)) {
		while (fact >> 32) {
			fact >>= 1;
			shift--;
		}
	}

	/* hint to use a 32x32->64 mul */
	fact = (u64)(u32)fact * lw->inv_weight;

	while (fact >> 32) {
		fact >>= 1;
		shift--;
	}

	return mul_u64_u32_shr(delta_exec, fact, shift);
}

/**
 * calc_delta_mycfs	-	delta /= weight
 * @delta: physical time in nanoseconads
 * @se: the schedule entity
 *
 * Calculate vruntime base on physical time and weight
 */
static inline u64 calc_delta_mycfs(u64 delta, struct sched_entity *se)
{
	if (unlikely(se->load.weight != NICE_0_LOAD))
		delta = __calc_delta(delta, NICE_0_LOAD, &se->load);
	return delta;
}

/*
 * update_min_vruntime
 *
 * Find the minimum vruntime of current task and leftmost task in runqueue.
 * Set this runtime as min_vruntime if it is greater than current value of
 * min_vruntime:
 */
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

/*
 * Update the current task's runtime statistics, including
 *	- exec_start
 *	- sum_exec_runtime
 *	- vruntime
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
 * The enqueue_task method is called before nr_running is increased.
 * Here we update the mycfs scheduling stats and then put the task
 * into the rbtree:
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

	/* Someone is not doing his job: */
	WARN_ON_ONCE(se->on_rq);

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

static unsigned long wakeup_gran(struct sched_entity *curr,
				 struct sched_entity *se)
{
	unsigned long gran = sysctl_sched_wakeup_granularity;

	/*
	 * Since its curr running now, convert the gran from real-time
	 * to virtual-time in his units.
	 *
	 * By using 'se' instead of 'curr' we penalize light tasks, so
	 * they get preempted easier. That is, if 'se' < 'curr' then
	 * the resulting gran will be larger, therefore penalizing the
	 * lighter, if otoh 'se' > 'curr' then the resulting gran will
	 * be smaller, again penalizing the lighter task.
	 *
	 * This is especially important for buddies when the leftmost
	 * task is higher priority than the buddy.
	 */
	return calc_delta_mycfs(gran, se);
}

/*
 * Should 'se' preempt 'curr'.
 *
 *             |s1
 *        |s2
 *   |s3
 *         g
 *      |<--->|c
 *
 *  w(c, s1) = -1
 *  w(c, s2) =  0
 *  w(c, s3) =  1
 *
 */
static int wakeup_preempt_entity(struct sched_entity *curr,
				 struct sched_entity *se)
{
	s64 gran, vdiff = curr->vruntime - se->vruntime;

	if (vdiff <= 0)
		return -1;

	gran = wakeup_gran(curr, se);
	if (vdiff > gran)
		return 1;

	return 0;
}

/*
 * Pick the next process, keeping these things in mind, in this order:
 * 1) keep things fair between processes/task groups
 * 2) pick the "next" process, since someone really wants that to run
 * 3) pick the "last" process, for cache locality
 * 4) do not run the "skip" process, if something else is available
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

	/* ideally we run the leftmost entity */
	se = left;

	/*
	 * Avoid running the skip buddy, if running something else can
	 * be done without getting too unfair.
	 */
	if (mycfs_rq->skip == se) {
		struct sched_entity *second;

		if (se == curr) {
			second = __pick_first_entity_mycfs(mycfs_rq);
		} else {
			second = __pick_next_entity(se);
			if (!second || (curr && entity_before(curr, second)))
				second = curr;
		}

		if (second && wakeup_preempt_entity(second, left) < 1)
			se = second;
	}

	/*
	 * Prefer last buddy, try to return the CPU to a preempted task.
	 */
	if (mycfs_rq->last && wakeup_preempt_entity(mycfs_rq->last, left) < 1)
		se = mycfs_rq->last;

	/*
	 * Someone really wants this to run. If it's not unfair, run it.
	 */
	if (mycfs_rq->next && wakeup_preempt_entity(mycfs_rq->next, left) < 1)
		se = mycfs_rq->next;

	clear_buddies(mycfs_rq, se);

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
static inline void update_stats_curr_start(struct mycfs_rq *mycfs_rq,
					   struct sched_entity *se)
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
	struct task_struct *curr = rq->curr;
	struct mycfs_rq *mycfs_rq = task_mycfs_rq(curr);
	struct sched_entity *se = &curr->se;

	/* Are we the only task in the tree? */
	if (unlikely(rq->nr_running == 1))
		return;

	clear_buddies(mycfs_rq, se);

	update_rq_clock(rq);

	/* Update run-time statistics of the 'current' */
	update_curr(mycfs_rq);

	/*
	 * Tell update_rq_clock() that we've just updated,
	 * so we don't do microscopic update in schedule()
	 * and double the fastpath cost.
	 */
	rq_clock_skip_update(rq, true);

	set_skip_buddy(se);
}

static bool yield_to_task_mycfs(struct rq *rq, struct task_struct *p, bool preempt)
{
	struct sched_entity *se = &p->se;

	if (!se->on_rq)
		return false;

	/* Tell the scheduler that we'd really like pse to run next. */
	set_next_buddy(se);

	yield_task_mycfs(rq);

	return true;
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
 * Called periodically by scheduler tick in HZ frequency
 * Check if we need to preempt current task:
 */
static void check_preempt_tick(struct mycfs_rq *mycfs_rq, struct sched_entity *curr)
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
static int select_task_rq_mycfs(struct task_struct *p, int prev_cpu,
				int sd_flag, int wake_flags)
{
	/* No migration now */
	return task_cpu(p);
}

/*
 * Called immediately before a task is migrated to a new cpu; task_cpu(p) and
 * mycfs_rq_of(p) references at time of call are still valid and identify the
 * previous cpu. The caller guarantees p->pi_lock or task_rq(p)->lock is held.
 */
static void migrate_task_rq_mycfs(struct task_struct *p)
{
	/*
	 * As blocked tasks retain absolute vruntime the migration needs to
	 * deal with this by subtracting the old and adding the new
	 * min_vruntime -- the latter is done by enqueue_entity() when placing
	 * the task on the new runqueue.
	 */
	if (p->state == TASK_WAKING) {
		struct sched_entity *se = &p->se;
		struct mycfs_rq *mycfs_rq = mycfs_rq_of(se);
		u64 min_vruntime;

		min_vruntime = mycfs_rq->min_vruntime;
		se->vruntime -= min_vruntime;
	}

	/* Tell new CPU we are migrated */
	p->se.avg.last_update_time = 0;

	/* We have migrated, no longer consider this task hot */
	p->se.exec_start = 0;
}

/*
 * task_dead_mycfs
 * Callback from finish_task_switch, after a task set its state to TASK_DEAD,
 * which happens after a task do_exit():
 */
static void task_dead_mycfs(struct task_struct *p)
{
}
#endif /* CONFIG_SMP */

/*
 * Account for a task changing its policy
 *
 * This routine is mostly called to set mycfs_rq->curr field when a task
 * migrates between sched classes.
 */
static void set_curr_task_mycfs(struct rq *rq)
{
	struct sched_entity *se;
	struct mycfs_rq *mycfs_rq;

	se = &rq->curr->se;
	mycfs_rq = mycfs_rq_of(se);
	set_next_entity(mycfs_rq, se);
}

/*
 * task_tick_mycfs
 * scheduler tick hitting a task of our scheduling class:
 */
static void task_tick_mycfs(struct rq *rq, struct task_struct *curr, int queued)
{
	struct mycfs_rq *mycfs_rq;
	struct sched_entity *se;

	se = &curr->se;
	mycfs_rq = mycfs_rq_of(se);

	/* Update run-time statistics of the 'current' */
	update_curr(mycfs_rq);

	if (mycfs_rq->nr_running > 1)
		check_preempt_tick(mycfs_rq, &curr->se);
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
	if (!task_on_rq_queued(p))
		return;

	/*
	 * Reschedule if we are currently running on this runqueue and
	 * our priority decreased, or if we are not currently running on
	 * this runqueue and our priority is higher than the current's
	 */
	if (rq->curr == p) {
		if (p->prio > oldprio)
			resched_curr(rq);
	} else
		check_preempt_curr(rq, p, 0);
}

static inline bool vruntime_normalized(struct task_struct *p)
{
	struct sched_entity *se = &p->se;

	/*
	 * In both the TASK_ON_RQ_QUEUED and TASK_ON_RQ_MIGRATING cases,
	 * the dequeue_entity(.flags=0) will already have normalized the
	 * vruntime.
	 */
	if (p->on_rq)
		return true;

	/*
	 * When !on_rq, vruntime of the task has usually NOT been normalized.
	 * But there are some cases where it has already been normalized:
	 *
	 * - A forked child which is waiting for being woken up by
	 *   wake_up_new_task().
	 * - A task which has been woken up by try_to_wake_up() and
	 *   waiting for actually being woken up by sched_ttwu_pending().
	 */
	if (!se->sum_exec_runtime || p->state == TASK_WAKING)
		return true;

	return false;
}

/*
 * switched_from_mycfs
 * Callback function when task @p changed its sched class from MyCFS
 */
static void switched_from_mycfs(struct rq *rq, struct task_struct *p)
{
	struct sched_entity *se = &p->se;
	struct mycfs_rq *mycfs_rq = mycfs_rq_of(se);

	if (!vruntime_normalized(p)) {
		/*
		 * Fix up our vruntime so that the current sleep doesn't
		 * cause 'unlimited' sleep bonus.
		 */
		place_entity(mycfs_rq, se, 0);
		se->vruntime -= mycfs_rq->min_vruntime;
	}
}

/*
 * switched_to_mycfs
 * Callback function when task @p changed its sched class to MyCFS
 */
static void switched_to_mycfs(struct rq *rq, struct task_struct *p)
{
	struct sched_entity *se = &p->se;
	struct mycfs_rq *mycfs_rq = mycfs_rq_of(se);

	if (!vruntime_normalized(p))
		se->vruntime += mycfs_rq->min_vruntime;

	if (task_on_rq_queued(p)) {
		/*
		 * We were most likely switched from sched_rt, so
		 * kick off the schedule if running, otherwise just see
		 * if we can still preempt the current task.
		 */
		if (rq->curr == p)
			resched_curr(rq);
		else
			check_preempt_curr(rq, p, 0);
	}
}

static unsigned int get_rr_interval_mycfs(struct rq *rq,
					  struct task_struct *task)
{
	return 0;
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
	/* task migration callback: */
	.select_task_rq		= select_task_rq_mycfs,
	.migrate_task_rq	= migrate_task_rq_mycfs,

	/* task exit point callback: */
	.task_dead		= task_dead_mycfs,
	.set_cpus_allowed	= set_cpus_allowed_common,
#endif

	.set_curr_task          = set_curr_task_mycfs,

	/* Called with HZ frequency by scheduler tick */
	.task_tick		= task_tick_mycfs,

	/* fock()-time callback: */
	.task_fork		= task_fork_mycfs,

	/*
	 * Called when sched class changed
	 * (via check_class_changed() only):
	 */
	.prio_changed		= prio_changed_mycfs,
	.switched_from		= switched_from_mycfs,
	.switched_to		= switched_to_mycfs,

	/* Return the default timeslice of a process */
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
