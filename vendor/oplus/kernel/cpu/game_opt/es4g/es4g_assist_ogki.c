// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Oplus. All rights reserved.
 */

#include <trace/events/sched.h>
#include <trace/hooks/sched.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/spinlock.h>
#include <linux/stdarg.h>
#include <linux/kprobes.h>
#if IS_ENABLED(CONFIG_SCHED_WALT)
#include <linux/sched/walt.h>
#endif /* CONFIG_SCHED_WALT */
#if IS_ENABLED(CONFIG_CPU_IDLE_GOV_QCOM_LPM)
#include <drivers/cpuidle/governors/trace-qcom-lpm.h>
#endif /* CONFIG_CPU_IDLE_GOV_QCOM_LPM */

#include <linux/sched/hmbird.h>

#include "game_ctrl.h"
#include "es4g/es4g_assist_ogki.h"
#include "es4g/es4g_assist_common.h"

#define ES4G_ALLOW_PROC_WR_OPS

#ifndef MAX_NR_CPUS
#define MAX_NR_CPUS (1 << 3)
#endif

static unsigned int es4g_assist_debug = 0;

#define DECLARE_DEBUG_TRACE(name, proto, data)				\
	static void __maybe_unused debug_##name(proto) {		\
		if (unlikely(es4g_assist_debug & DEBUG_SYSTRACE)) {	\
			name(data);										\
		}													\
	}
#include "debug_common.h"
#undef DECLARE_DEBUG_TRACE

static struct proc_dir_entry *es4g_dir = NULL;

static struct key_thread_struct {
	pid_t pid;
	struct task_struct *task;
	s32 prio; /* smaller is more critical, range from 0 to 8 */
	u32 slot;
	s32 cpu;
	s32 util;
} critical_thread_list[MAX_KEY_THREAD_RECORD];

static int heavy_task_index = -1;
static int __maybe_unused heavy_task_count = 0;
static int es4g_assist_preempt_policy = 0;

struct isolate_mask {
	u32 strict_isolate;
	u32 pipeline_isolate;
	u32 weak_isolate;
} es4g_isolate_mask = {
	.strict_isolate = 0,
	.pipeline_isolate = 0,
	.weak_isolate = 0,
};

struct cpumask_record
{
	u32 select_cpumask;
	u32 exclusive_cpumask;
	u32 period_disallow_cpumask;
	u32 nonperiod_disallow_cpumask;
	u32 reserved_cpumask;
} es4g_cpumask_record = {
	.select_cpumask = 0,
	.exclusive_cpumask = 0,
	.period_disallow_cpumask = 0,
	.nonperiod_disallow_cpumask = 0,
	.reserved_cpumask = 0,
};

static int select_cpu_list[MAX_NR_CPUS] = {7, 4, 3, 2, 6, 5, -1, -1};
static int sched_prop_to_preempt_prio[ES4G_TASK_PROP_MAX] = {0};

static DEFINE_RWLOCK(critical_task_list_rwlock);
static DEFINE_RWLOCK(select_cpu_list_rwlock);
static DEFINE_RWLOCK(top_task_prop_rwlock);
static DEFINE_RWLOCK(cpumask_record_rwlock);

static inline bool task_specific_type(uint32_t prop, enum es4g_task_prop_type type)
{
	return test_bit(type << TOP_TASK_SHIFT, (unsigned long *)&prop);
}

static inline void set_top_task_prop_locked(struct task_struct *p, u64 set, u64 clear)
{
	write_lock(&top_task_prop_rwlock);
	set_top_task_prop(p, set, clear);
	write_unlock(&top_task_prop_rwlock);
}

static inline void set_task_specific_type(struct task_struct *p, enum es4g_task_prop_type type)
{
	set_top_task_prop(p, 1 << (type + TOP_TASK_SHIFT), 0);
}

static inline void unset_task_specific_type(struct task_struct *p, enum es4g_task_prop_type type)
{
	set_top_task_prop(p, 0, 1 << (type + TOP_TASK_SHIFT));
}

static inline void init_sched_prop_to_preempt_prio(void)
{
	/**
	 * prio list: 8 > 7 > 1 > other > 0 > 2
	 *
	 * type 8: 5
	 * type 7: 4
	 * type 1,9: 3
	 * other: 2
	 * type 0: 1
	 * type 2: 0
	 *
	 */
	for (int i = 0; i < ES4G_TASK_PROP_MAX; i++) {
		switch (i) {
		case ES4G_TASK_PROP_TRANSIENT_AND_CRITICAL:
			sched_prop_to_preempt_prio[i] = 5;
			break;

		case ES4G_TASK_PROP_PERIODIC_AND_CRITICAL:
			sched_prop_to_preempt_prio[i] = 4;
			break;

		case ES4G_TASK_PROP_PIPELINE:
		case ES4G_TASK_PROP_ISOLATE:
			sched_prop_to_preempt_prio[i] = 3;
			break;

		case ES4G_TASK_PROP_COMMON:
			sched_prop_to_preempt_prio[i] = 1;
			break;

		case ES4G_TASK_PROP_DEBUG_OR_LOG:
			sched_prop_to_preempt_prio[i] = 0;
			break;

		default:
			sched_prop_to_preempt_prio[i] = 2;
			break;
		}
	}
}

static inline enum es4g_task_prop_type es4g_get_task_type(struct task_struct *p)
{
	uint32_t prop = get_top_task_prop(p);

	if (task_specific_type(prop, ES4G_TASK_PROP_TRANSIENT_AND_CRITICAL)) {
		return ES4G_TASK_PROP_TRANSIENT_AND_CRITICAL;
	}
	if (task_specific_type(prop, ES4G_TASK_PROP_PERIODIC_AND_CRITICAL)) {
		return ES4G_TASK_PROP_PERIODIC_AND_CRITICAL;
	}
	if (task_specific_type(prop, ES4G_TASK_PROP_PIPELINE)) {
		return ES4G_TASK_PROP_PIPELINE;
	}
	if (task_specific_type(prop, ES4G_TASK_PROP_COMMON) ||
			!task_specific_type(prop, ES4G_TASK_PROP_DEBUG_OR_LOG)) {
		return ES4G_TASK_PROP_COMMON;
	}
	return ES4G_TASK_PROP_DEBUG_OR_LOG;
}

static inline bool __maybe_unused es4g_prio_higher(struct task_struct *a, struct task_struct *b)
{
	int type_a = es4g_get_task_type(a);
	int type_b = es4g_get_task_type(b);

	return sched_prop_to_preempt_prio[type_a] > sched_prop_to_preempt_prio[type_b];
}

static void update_real_isolate_cpumask(void)
{
	int cpu;
	struct rq *rq;
	struct hmbird_rq *hrq;
	struct rq_flags rf;
	int strict_isolate = READ_ONCE(es4g_isolate_mask.strict_isolate);
	int pipeline_isolate = READ_ONCE(es4g_cpumask_record.select_cpumask) & READ_ONCE(es4g_isolate_mask.pipeline_isolate);
	int weak_isolate = READ_ONCE(es4g_isolate_mask.weak_isolate);

	es4g_cpumask_record.exclusive_cpumask = strict_isolate | pipeline_isolate;
	es4g_cpumask_record.period_disallow_cpumask = strict_isolate | pipeline_isolate | weak_isolate;
	es4g_cpumask_record.nonperiod_disallow_cpumask = strict_isolate | pipeline_isolate;

	for_each_possible_cpu(cpu) {
		rq = cpu_rq(cpu);
		rq_lock_irqsave(rq, &rf);

		hrq = get_hmbird_rq(rq);
		if (likely(hrq)) {
			hrq->pipeline = test_bit(cpu, (unsigned long *)&es4g_cpumask_record.select_cpumask);
			hrq->exclusive = test_bit(cpu, (unsigned long *)&es4g_cpumask_record.exclusive_cpumask);
			hrq->period_disallow = test_bit(cpu, (unsigned long *)&es4g_cpumask_record.period_disallow_cpumask);
			hrq->nonperiod_disallow = test_bit(cpu, (unsigned long *)&es4g_cpumask_record.nonperiod_disallow_cpumask);
		}

		rq_unlock_irqrestore(rq, &rf);
	}
}

static void remove_slot_of_index(struct key_thread_struct *list, size_t index)
{
	struct hmbird_entity *he;

	rcu_read_lock();
	if (list[index].slot > 0 && likely(list[index].task)) {
		set_top_task_prop_locked(list[index].task, 0, TOP_TASK_BITS_MASK);
		he = get_hmbird_ts(list[index].task);
		if (likely(he)) {
			he->critical_affinity_cpu = -1;
		}
		put_task_struct(list[index].task);
	}
	rcu_read_unlock();
	list[index].pid = -1;
	list[index].task = NULL;
	list[index].prio = -1;
	list[index].slot = 0;
	list[index].cpu = -1;
	list[index].util = -1;
	if (heavy_task_index == index) {
		heavy_task_index = -1;
	}
}

static bool clear_key_thread(struct key_thread_struct *list, size_t len)
{
	write_lock(&critical_task_list_rwlock);
	for (int i = 0; i < len; i++) {
		remove_slot_of_index(list, i);
	}
	write_unlock(&critical_task_list_rwlock);
	write_lock(&cpumask_record_rwlock);
	WRITE_ONCE(es4g_cpumask_record.select_cpumask, 0);
	update_real_isolate_cpumask();
	write_unlock(&cpumask_record_rwlock);
	return true;
}

static bool init_key_thread(struct key_thread_struct *list, size_t len)
{
	return clear_key_thread(list, len);
}

static void update_key_thread_cpu(struct key_thread_struct *list, size_t len)
{
	int prio_count[KEY_THREAD_PRIORITY_COUNT + 1] = {0};
	int select_cpu_mask = 0;
	u32 pipeline_isolate_mask = READ_ONCE(es4g_isolate_mask.pipeline_isolate);
	struct hmbird_entity *he;

	/* boost priority of heavy task */
	if (heavy_task_index >= 0) {
		list[heavy_task_index].prio--;
	}

	for (int i = 0; i < len; i++) {
		if (list[i].slot > 0) {
			prio_count[list[i].prio + 1]++;
		}
	}
	/* 1st and the last slot is not necessary to count */
	for (int i = 2; i < KEY_THREAD_PRIORITY_COUNT; i++) {
		prio_count[i] += prio_count[i - 1];
	}

	read_lock(&select_cpu_list_rwlock);
	write_lock(&top_task_prop_rwlock);
	for (int i = 0; i < len; i++) {
		if (list[i].slot <= 0 || prio_count[list[i].prio] >= MAX_NR_CPUS) {
			continue;
		}
		list[i].cpu = select_cpu_list[prio_count[list[i].prio]];
		he = get_hmbird_ts(list[i].task);
		if (likely(he)) {
			he->critical_affinity_cpu = select_cpu_list[prio_count[list[i].prio]];
		}
		if (list[i].cpu < 0) {
			unset_task_specific_type(list[i].task, ES4G_TASK_PROP_PIPELINE);
			unset_task_specific_type(list[i].task, ES4G_TASK_PROP_ISOLATE);
		} else {
			set_task_specific_type(list[i].task, ES4G_TASK_PROP_PIPELINE);
			if (pipeline_isolate_mask & (1 << list[i].cpu)) {
				set_task_specific_type(list[i].task, ES4G_TASK_PROP_ISOLATE);
			}
			select_cpu_mask |= 1 << list[i].cpu;
		}
		prio_count[list[i].prio]++;
	}
	write_unlock(&top_task_prop_rwlock);
	read_unlock(&select_cpu_list_rwlock);

	if (heavy_task_index >= 0) {
		list[heavy_task_index].prio++;
	}

	write_lock(&cpumask_record_rwlock);
	WRITE_ONCE(es4g_cpumask_record.select_cpumask, select_cpu_mask);
	update_real_isolate_cpumask();
	write_unlock(&cpumask_record_rwlock);
}

static bool add_key_thread(struct key_thread_struct *list, size_t len, pid_t pid, s32 prio)
{
	int first_slot = -1;
	bool update = false;

	if (prio > MIN_KEY_THREAD_PRIORITY) {
		prio = MIN_KEY_THREAD_PRIORITY;
	}
	if (prio < MAX_KEY_THREAD_PRIORITY_US) {
		prio = MAX_KEY_THREAD_PRIORITY_US;
	}

	for (int i = 0; i < len; i++) {
		if (list[i].slot > 0) {
			if (list[i].pid == pid) {
				if (list[i].prio != prio) {
					list[i].prio = prio;
					update = true;
				}
				goto out;
			}
		} else {
			if (first_slot < 0) {
				first_slot = i;
			}
		}
	}
	if (first_slot >= 0) {
		rcu_read_lock();
		list[first_slot].task = find_task_by_vpid(pid);
		if (list[first_slot].task) {
			get_task_struct(list[first_slot].task);
			list[first_slot].pid = pid;
			list[first_slot].prio = prio;
			list[first_slot].slot = 1;
			list[first_slot].util = -1;
			hmbird_set_sched_prop(list[first_slot].task, SCHED_PROP_DEADLINE_LEVEL3);
			set_top_task_prop_locked(list[first_slot].task, 1, 0);
			update = true;
		}
		rcu_read_unlock();
	}

out:
	if (update) {
		heavy_task_index = -1;
	}

	return update;
}

static bool remove_key_thread(struct key_thread_struct *list, size_t len, pid_t pid)
{
	for (int i = 0; i < len; i++) {
		if (list[i].slot > 0 && list[i].pid == pid) {
			remove_slot_of_index(list, i);
			return true;
		}
	}
	return false;
}

#if IS_ENABLED(CONFIG_CPU_IDLE_GOV_QCOM_LPM)
static void scx_sched_lpm_disallowed_time_hook(void *unused, int cpu, int *timeout_allowed)
{
	struct rq *rq = cpu_rq(cpu);
	struct hmbird_rq *hrq = get_hmbird_rq(rq);
	if (likely(hrq)) {
		*timeout_allowed = !!(hrq->pipeline);
	}
}
#endif /* CONFIG_CPU_IDLE_GOV_QCOM_LPM */

static int es4g_assist_proc_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int es4g_assist_proc_release(struct inode *inode, struct file *file)
{
	return 0;
}

static void set_es4g_assist_debug(int debug)
{
	es4g_assist_debug = debug < 0 ? 0 : debug;
}

static void set_es4g_assist_preempt_policy(int type)
{
	es4g_assist_preempt_policy = type < 0 ? 0 : type;
}

static void set_es4g_assist_top_task_prop(pid_t pid, int prop)
{
	struct task_struct *task = NULL;

	rcu_read_lock();
	task = find_task_by_vpid(pid);
	if (task) {
		set_top_task_prop_locked(task, prop << TOP_TASK_SHIFT, 0);
	}
	rcu_read_unlock();
}

static void unset_es4g_assist_top_task_prop(pid_t pid, int prop)
{
	struct task_struct *task = NULL;

	rcu_read_lock();
	task = find_task_by_vpid(pid);
	if (task) {
		set_top_task_prop_locked(task, 0, prop << TOP_TASK_SHIFT);
	}
	rcu_read_unlock();
}

static ssize_t es4g_assist_debug_proc_write(
	struct file *file,
	const char __user *buf, size_t count, loff_t *ppos)
{
#ifdef ES4G_ALLOW_PROC_WR_OPS
	char page[ONE_PAGE_SIZE] = {0};
	int ret, debug;

	ret = simple_write_to_buffer(page, ONE_PAGE_SIZE - 1, ppos, buf, count);
	if (ret <= 0) {
		return ret;
	}

	ret = sscanf(page, "%d", &debug);
	if (ret < 1) {
		return -EINVAL;
	}

	set_es4g_assist_debug(debug);

	return count;
#else
	return 0;
#endif /* ES4G_ALLOW_PROC_WR_OPS */
}

static ssize_t es4g_assist_debug_proc_read(
	struct file *file,
	char __user *buf, size_t count, loff_t *ppos)
{
#ifdef ES4G_ALLOW_PROC_WR_OPS
	char page[ONE_PAGE_SIZE] = {0};
	int len;

	len = snprintf(page, ONE_PAGE_SIZE - 1, "%d\n", es4g_assist_debug);

	return simple_read_from_buffer(buf, count, ppos, page, len);
#else
	return 0;
#endif /* ES4G_ALLOW_PROC_WR_OPS */
}

static const struct proc_ops es4g_assist_debug_proc_ops = {
	.proc_write		= es4g_assist_debug_proc_write,
	.proc_read		= es4g_assist_debug_proc_read,
	.proc_lseek		= default_llseek,
};

static bool __maybe_unused set_critical_task(int tid, int prio)
{
	bool ret;

	if (tid < 0 && prio < 0) {
		return clear_key_thread(critical_thread_list, MAX_KEY_THREAD_RECORD);
	}

	if (tid < 0)
		return false;

	write_lock(&critical_task_list_rwlock);
	if (prio < 0) {
		ret = remove_key_thread(critical_thread_list, MAX_KEY_THREAD_RECORD, tid);
	} else {
		ret = add_key_thread(critical_thread_list, MAX_KEY_THREAD_RECORD, tid, prio);
	}
	if (ret) {
		update_key_thread_cpu(critical_thread_list, MAX_KEY_THREAD_RECORD);
	}
	write_unlock(&critical_task_list_rwlock);

	return ret;
}

static bool batch_set_critical_task(struct es4g_ctrl_info *data, struct key_thread_struct *list, size_t len)
{
	int pair;
	int tid;
	int prio;
	bool update;

	if (data->size <= 0 || (data->size & 1)) {
		return false;
	}

	if (data->data[0] < 0 && data->data[1] < 0) {
		return clear_key_thread(list, len);
	}

	pair = data->size / 2;
	update = false;

	write_lock(&critical_task_list_rwlock);
	for (int i = 0; i < pair; i++) {
		tid = data->data[i * 2];
		prio = data->data[i * 2 + 1];
		if (prio >= 0) {
			continue;
		}
		if (remove_key_thread(list, len, tid)) {
			update = true;
		}
	}
	for (int i = 0; i < pair; i++) {
		tid = data->data[i * 2];
		prio = data->data[i * 2 + 1];
		if (prio < 0) {
			continue;
		}
		if (add_key_thread(list, len, tid, prio)) {
			update = true;
		}
	}
	if (update) {
		update_key_thread_cpu(list, len);
	}
	write_unlock(&critical_task_list_rwlock);

	return update;
}

static ssize_t es4g_critical_task_proc_write(
	struct file *file,
	const char __user *buf, size_t count, loff_t *ppos)
{
#ifdef ES4G_ALLOW_PROC_WR_OPS
	char page[ONE_PAGE_SIZE] = {0};
	int ret;
	int tid, prio;

	ret = simple_write_to_buffer(page, ONE_PAGE_SIZE - 1, ppos, buf, count);
	if (ret <= 0)
		return ret;

	ret = sscanf(page, "%d %d", &tid, &prio);
	if (ret != 2)
		return -EINVAL;

	if (!set_critical_task(tid, prio)) {
		return -EINVAL;
	}

	return count;
#else
	return 0;
#endif /* ES4G_ALLOW_PROC_WR_OPS */
}

static ssize_t es4g_critical_task_proc_read(
	struct file *file,
	char __user *buf, size_t count, loff_t *ppos)
{
#ifdef ES4G_ALLOW_PROC_WR_OPS
	char page[ONE_PAGE_SIZE * MAX_KEY_THREAD_RECORD] = {0};
	int len = 0;
	struct hmbird_entity *he;
	int cpu;

	read_lock(&critical_task_list_rwlock);
	for (int i = 0; i < MAX_KEY_THREAD_RECORD; i++) {
		if (critical_thread_list[i].slot > 0 && critical_thread_list[i].task) {
			he = get_hmbird_ts(critical_thread_list[i].task);
			cpu = he->critical_affinity_cpu;
			len += snprintf(page + len, ONE_PAGE_SIZE * MAX_KEY_THREAD_RECORD - len,
								"tid=%d, prio=%d, cpu=%d\n",
								critical_thread_list[i].pid, critical_thread_list[i].prio, cpu);
		}
	}
	if (heavy_task_index >= 0) {
		len += snprintf(page + len, ONE_PAGE_SIZE * MAX_KEY_THREAD_RECORD - len,
								"heavy task is %d\n", critical_thread_list[heavy_task_index].pid);
	}
	read_unlock(&critical_task_list_rwlock);

	return simple_read_from_buffer(buf, count, ppos, page, len);
#else
	return 0;
#endif /* ES4G_ALLOW_PROC_WR_OPS */
}

static const struct proc_ops es4g_critical_task_proc_ops = {
	.proc_write		= es4g_critical_task_proc_write,
	.proc_read		= es4g_critical_task_proc_read,
	.proc_lseek		= default_llseek,
};

static void update_select_cpu_list(s64 *data, size_t len)
{
	if (len > MAX_NR_CPUS) {
		len = MAX_NR_CPUS;
	}

	write_lock(&select_cpu_list_rwlock);
	for (int i = 0; i < len; i++) {
		select_cpu_list[i] = data[i];
	}
	for (int i = len; i < MAX_NR_CPUS; i++) {
		select_cpu_list[i] = -1;
	}
	write_unlock(&select_cpu_list_rwlock);

	write_lock(&critical_task_list_rwlock);
	update_key_thread_cpu(critical_thread_list, MAX_KEY_THREAD_RECORD);
	write_unlock(&critical_task_list_rwlock);
}

static ssize_t es4g_select_cpu_list_proc_write(
	struct file *file,
	const char __user *buf, size_t count, loff_t *ppos)
{
#ifdef ES4G_ALLOW_PROC_WR_OPS
	char page[ONE_PAGE_SIZE] = {0};
	int ret;
	s64 cpu_list[MAX_KEY_THREAD_RECORD] = {0};

	ret = simple_write_to_buffer(page, ONE_PAGE_SIZE - 1, ppos, buf, count);
	if (ret <= 0) {
		return ret;
	}

	ret = sscanf(page, "%lld %lld %lld %lld %lld %lld %lld %lld",
					&cpu_list[0],
					&cpu_list[1],
					&cpu_list[2],
					&cpu_list[3],
					&cpu_list[4],
					&cpu_list[5],
					&cpu_list[6],
					&cpu_list[7]);
	if (ret <= 0) {
		return -EINVAL;
	}

	update_select_cpu_list(cpu_list, ret);

	return count;
#else
	return 0;
#endif /* ES4G_ALLOW_PROC_WR_OPS */
}

static ssize_t es4g_select_cpu_list_proc_read(
	struct file *file,
	char __user *buf, size_t count, loff_t *ppos)
{
#ifdef ES4G_ALLOW_PROC_WR_OPS
	char page[ONE_PAGE_SIZE << 1] = {0};
	int len = 0;

	read_lock(&select_cpu_list_rwlock);
	for (int i = 0; i < MAX_KEY_THREAD_RECORD; i++) {
		if (select_cpu_list[i] >= 0) {
			len += snprintf(page + len, (ONE_PAGE_SIZE << 1) - len, "%d: %d\n", i, select_cpu_list[i]);
		} else {
			break;
		}
	}
	read_unlock(&select_cpu_list_rwlock);

	return simple_read_from_buffer(buf, count, ppos, page, len);
#else
	return 0;
#endif /* ES4G_ALLOW_PROC_WR_OPS */
}

static const struct proc_ops es4g_select_cpu_list_proc_ops = {
	.proc_write		= es4g_select_cpu_list_proc_write,
	.proc_read		= es4g_select_cpu_list_proc_read,
	.proc_lseek		= default_llseek,
};

static void set_isolate_cpus(int isolate_cpus, enum es4g_isolate_type type)
{
	switch (type) {
	case ES4G_ISOLATE_STRICT:
		if (isolate_cpus == READ_ONCE(es4g_isolate_mask.strict_isolate)) {
			return;
		}
		WRITE_ONCE(es4g_isolate_mask.strict_isolate, isolate_cpus);
		break;

	case ES4G_ISOLATE_PIPELINE:
		if (isolate_cpus == READ_ONCE(es4g_isolate_mask.pipeline_isolate)) {
			return;
		}
		WRITE_ONCE(es4g_isolate_mask.pipeline_isolate, isolate_cpus);
		break;

	case ES4G_ISOLATE_WEAK:
		if (isolate_cpus == READ_ONCE(es4g_isolate_mask.weak_isolate)) {
			return;
		}
		WRITE_ONCE(es4g_isolate_mask.weak_isolate, isolate_cpus);
		break;

	default:
		break;
	}

	if (type == ES4G_ISOLATE_PIPELINE) {
		write_lock(&critical_task_list_rwlock);
		update_key_thread_cpu(critical_thread_list, MAX_KEY_THREAD_RECORD);
		write_unlock(&critical_task_list_rwlock);
	} else {
		write_lock(&cpumask_record_rwlock);
		update_real_isolate_cpumask();
		write_unlock(&cpumask_record_rwlock);
	}

	debug_trace_pr_val_uint(type, isolate_cpus);
}

static ssize_t es4g_isolate_cpus_proc_write(
	struct file *file,
	const char __user *buf, size_t count, loff_t *ppos)
{
#ifdef ES4G_ALLOW_PROC_WR_OPS
	char page[ONE_PAGE_SIZE] = {0};
	int strict_isolate, pipeline_isolate, weak_isolate;
	int ret;

	ret = simple_write_to_buffer(page, ONE_PAGE_SIZE - 1, ppos, buf, count);
	if (ret <= 0) {
		return ret;
	}

	ret = sscanf(page, "%d:%d:%d", &pipeline_isolate, &weak_isolate, &strict_isolate);
	if (ret != 3) {
		return -EINVAL;
	}

	set_isolate_cpus(pipeline_isolate, ES4G_ISOLATE_PIPELINE);
	set_isolate_cpus(weak_isolate, ES4G_ISOLATE_WEAK);
	set_isolate_cpus(strict_isolate, ES4G_ISOLATE_STRICT);

	return count;
#else
	return 0;
#endif /* ES4G_ALLOW_PROC_WR_OPS */
}

static ssize_t es4g_isolate_cpus_proc_read(
	struct file *file,
	char __user *buf, size_t count, loff_t *ppos)
{
#ifdef ES4G_ALLOW_PROC_WR_OPS
	char page[ONE_PAGE_SIZE] = {0};
	int len = 0;
	int pipeline_isolate = READ_ONCE(es4g_isolate_mask.pipeline_isolate);
	int weak_isolate = READ_ONCE(es4g_isolate_mask.weak_isolate);
	int strict_isolate = READ_ONCE(es4g_isolate_mask.strict_isolate);

	len = snprintf(page, ONE_PAGE_SIZE - 1, "%d:%d:%d\n", pipeline_isolate, weak_isolate, strict_isolate);

	return simple_read_from_buffer(buf, count, ppos, page, len);
#else
	return 0;
#endif /* ES4G_ALLOW_PROC_WR_OPS */
}

static const struct proc_ops es4g_isolate_cpus_proc_ops = {
	.proc_write		= es4g_isolate_cpus_proc_write,
	.proc_read		= es4g_isolate_cpus_proc_read,
	.proc_lseek		= default_llseek,
};

static long es4g_assist_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct es4g_ctrl_info data;
	void __user *uarg = (void __user *)arg;
	long ret = 0;

	if ((_IOC_TYPE(cmd) != ES4G_MAGIC) || (_IOC_NR(cmd) >= ES4G_MAX_ID)) {
		return -EINVAL;
	}

	if (copy_from_user(&data, uarg, sizeof(data))) {
		return -EFAULT;
	}

	switch (cmd) {
	case CMD_ID_ES4G_COMMON_CTRL:
		switch (data.data[0]) {
		case ES4G_COMMON_CTRL_DEBUG_LEVEL:
			set_es4g_assist_debug(data.data[1]);
			break;

		case ES4G_COMMON_CTRL_PREEMPT_TYPE:
			set_es4g_assist_preempt_policy(data.data[1]);
			break;

		case ES4G_COMMON_CTRL_SET_SCHED_PROP:
			set_es4g_assist_top_task_prop(data.data[1], data.data[2]);
			break;

		case ES4G_COMMON_CTRL_UNSET_SCHED_PROP:
			unset_es4g_assist_top_task_prop(data.data[1], data.data[2]);
			break;

		default:
			break;
		}
		break;

	case CMD_ID_ES4G_SET_CRITICAL_TASK:
		batch_set_critical_task(&data, critical_thread_list, MAX_KEY_THREAD_RECORD);
		break;

	case CMD_ID_ES4G_SELECT_CPU_LIST:
		update_select_cpu_list(data.data, data.size);
		break;

	case CMD_ID_ES4G_SET_ISOLATE_CPUS:
		if (data.size > 0) {
			set_isolate_cpus(data.data[0], ES4G_ISOLATE_PIPELINE);
		}
		if (data.size > 1) {
			set_isolate_cpus(data.data[1], ES4G_ISOLATE_WEAK);
		}
		if (data.size > 2) {
			set_isolate_cpus(data.data[2], ES4G_ISOLATE_STRICT);
		}
		break;

	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

#if IS_ENABLED(CONFIG_COMPAT)
static long compat_es4g_assist_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return es4g_assist_ioctl(file, cmd, (unsigned long)compat_ptr(arg));
}
#endif /* CONFIG_COMPAT */

static const struct proc_ops es4g_assist_sys_ctrl_proc_ops = {
	.proc_ioctl			= es4g_assist_ioctl,
	.proc_open			= es4g_assist_proc_open,
	.proc_release		= es4g_assist_proc_release,
#if IS_ENABLED(CONFIG_COMPAT)
	.proc_compat_ioctl	= compat_es4g_assist_ioctl,
#endif /* CONFIG_COMPAT */
	.proc_lseek			= default_llseek,
};

static void register_es4g_assist_vendor_hooks(void)
{
#if IS_ENABLED(CONFIG_CPU_IDLE_GOV_QCOM_LPM)
	register_trace_android_vh_scx_sched_lpm_disallowed_time(scx_sched_lpm_disallowed_time_hook, NULL);
#endif /* CONFIG_CPU_IDLE_GOV_QCOM_LPM */
}

static void unregister_es4g_assist_vendor_hooks(void)
{
#if IS_ENABLED(CONFIG_CPU_IDLE_GOV_QCOM_LPM)
	unregister_trace_android_vh_scx_sched_lpm_disallowed_time(scx_sched_lpm_disallowed_time_hook, NULL);
#endif /* CONFIG_CPU_IDLE_GOV_QCOM_LPM */
}

static void es4g_proc_create(void)
{
	es4g_dir = proc_mkdir("es4g", game_opt_dir);

	if (unlikely(!es4g_dir))
		return;

	proc_create_data("es4ga_ctrl", 0644, es4g_dir, &es4g_assist_sys_ctrl_proc_ops, NULL);
	proc_create_data("es4ga_debug", 0644, es4g_dir, &es4g_assist_debug_proc_ops, NULL);
	proc_create_data("critical_task", 0644, es4g_dir, &es4g_critical_task_proc_ops, NULL);
	proc_create_data("select_cpu_list", 0644, es4g_dir, &es4g_select_cpu_list_proc_ops, NULL);
	proc_create_data("isolate_cpus", 0644, es4g_dir, &es4g_isolate_cpus_proc_ops, NULL);
}

static void es4g_remove_proc_entry(void)
{
	if (unlikely(!es4g_dir))
		return;

	remove_proc_entry("es4ga_ctrl", es4g_dir);
	remove_proc_entry("es4ga_debug", es4g_dir);
	remove_proc_entry("critical_task", es4g_dir);
	remove_proc_entry("select_cpu_list", es4g_dir);
	remove_proc_entry("isolate_cpus", es4g_dir);
	remove_proc_entry("es4g", game_opt_dir);
}

int es4g_assist_ogki_init(void)
{
	if (unlikely(!game_opt_dir))
		return -ENOTDIR;

	register_es4g_assist_vendor_hooks();
	es4g_proc_create();

	init_key_thread(critical_thread_list, MAX_KEY_THREAD_RECORD);
	init_sched_prop_to_preempt_prio();

	return 0;
}

void es4g_assist_ogki_exit(void)
{
	if (unlikely(!game_opt_dir))
		return;

	unregister_es4g_assist_vendor_hooks();
	es4g_remove_proc_entry();

	clear_key_thread(critical_thread_list, MAX_KEY_THREAD_RECORD);
}
