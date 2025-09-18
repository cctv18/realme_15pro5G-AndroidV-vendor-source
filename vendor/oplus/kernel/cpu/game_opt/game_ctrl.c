// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Oplus. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>

#include "game_ctrl.h"
#include "yield_opt.h"
#ifdef CONFIG_HMBIRD_SCHED
#include "es4g/es4g_assist_ogki.h"
#include "es4g/es4g_assist_gki.h"
#include "cpufreq_scx_main.h"
#include "es4g/es4g_assist_common.h"
#include <linux/sched/hmbird_version.h>
#endif /* CONFIG_HMBIRD_SCHED */
#include "frame_sync.h"
#include "task_boost/heavy_task_boost.h"
#include "critical_task_boost.h"
#include "dsu_freq.h"

struct proc_dir_entry *game_opt_dir = NULL;
struct proc_dir_entry *early_detect_dir = NULL;
struct proc_dir_entry *critical_heavy_boost_dir = NULL;

#if IS_ENABLED(CONFIG_OPLUS_FEATURE_GEAS)
extern void init_geas_proc_node(void);
#endif

static int __init game_ctrl_init(void)
{
	game_opt_dir = proc_mkdir("game_opt", NULL);
	if (!game_opt_dir) {
		pr_err("fail to mkdir /proc/game_opt\n");
		return -ENOMEM;
	}
	early_detect_dir = proc_mkdir("early_detect", game_opt_dir);
	if (!early_detect_dir) {
		pr_err("fail to mkdir /proc/game_opt/early_detect\n");
		return -ENOMEM;
	}
	critical_heavy_boost_dir = proc_mkdir("task_boost", game_opt_dir);
	if (!critical_heavy_boost_dir) {
		pr_err("fail to mkdir /proc/game_opt/task_boost\n");
		return -ENOMEM;
	}

	cpu_load_init();
	frame_load_init();
	cpufreq_limits_init();
	early_detect_init();
	task_util_init();
	rt_info_init();
	fake_cpufreq_init();
	debug_init();
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_GEAS)
	init_geas_proc_node();
#endif

#ifdef CONFIG_HMBIRD_SCHED
	if (HMBIRD_GKI_VERSION == get_hmbird_version_type()) {
#ifdef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
		/*Only Qcom support GKI hmbird*/
		es4g_assist_gki_init();
#endif /* CONFIG_OPLUS_SYSTEM_KERNEL_QCOM */
	} else if (HMBIRD_OGKI_VERSION == get_hmbird_version_type()) {
		int cpu;
		bool hmbird_effective = true;
		for_each_possible_cpu(cpu) {
			struct hmbird_rq *hrq;
			struct rq *rq = cpu_rq(cpu);
			if (!rq) {
				hmbird_effective = false;
				break;
			}
			hrq = get_hmbird_rq(rq);
			if (!hrq) {
				hmbird_effective = false;
				break;
			}
		}

		if (hmbird_effective) {
			es4g_assist_ogki_init();
			hmbird_cpufreq_init();
		}
	}
#endif /* CONFIG_HMBIRD_SCHED */
	yield_opt_init();
	frame_sync_init();
	heavy_task_boost_init();
	hrtimer_boost_init();
	dsu_freq_init();

	return 0;
}

static void __exit game_ctrl_exit(void)
{
#ifdef CONFIG_HMBIRD_SCHED
	if (HMBIRD_GKI_VERSION == get_hmbird_version_type()) {
#ifdef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
		/*Only Qcom support GKI hmbird*/
		es4g_assist_gki_exit();
#endif /* CONFIG_OPLUS_SYSTEM_KERNEL_QCOM */
	} else if (HMBIRD_OGKI_VERSION == get_hmbird_version_type()) {
		es4g_assist_ogki_exit();
	}
#endif /* CONFIG_HMBIRD_SCHED */

	heavy_task_boost_exit();
	hrtimer_boost_exit();
}

module_init(game_ctrl_init);
module_exit(game_ctrl_exit);
MODULE_LICENSE("GPL v2");
