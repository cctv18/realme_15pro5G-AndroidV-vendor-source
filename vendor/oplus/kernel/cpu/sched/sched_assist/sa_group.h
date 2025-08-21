
#ifndef _OPLUS_SA_GROUP_H_
#define _OPLUS_SA_GROUP_H_

#ifdef pr_fmt
#undef pr_fmt
#endif

#define pr_fmt(fmt) "sa_group: " fmt

#include <linux/cgroup-defs.h>

#define NR_TG_GRP (40)
#define SHARE_DEFAULT (100)
#define BG_SHARE_DEFAULT (50)
#define MAX_OUTPUT	(1024)
#define EXTRA_SIZE (100)
#define MAX_GUARD_SIZE (MAX_OUTPUT - EXTRA_SIZE)
#define SG_DDL_RTHRES_DEFAULT (60)

enum ddl_cgrp {
	ROOTGROUP,
	FOREGROUND,
	BACKGROUND,
	TOP_APP,
	SYSTEM_BG,
	BG,
	DDL_CGRP_DEFAULT,
	DDL_CGRP_MAX,
};

struct oplus_sg_ddl {
	const char *tg_name;
	u64 ddl;
	u64 ddl_rthres;
};

struct css_tg_map {
	struct list_head map_list;
	const char *tg_name;
	int id;
	u64 ddl;
	u64 ddl_rthres;
	int share_pct;
};

extern struct task_group root_task_group;

#ifdef CONFIG_OPLUS_SCHED_GROUP_OPT
u64 get_sg_ddl_rthres(struct task_group *tg);

#else

u64 get_sg_ddl_rthres(struct task_group *tg)
{
	return SG_DDL_RTHRES_DEFAULT;
}
#endif
void oplus_sg_wake_up_new_task(struct task_struct *tsk);
void oplus_sched_group_init(struct proc_dir_entry *pde);
void oplus_update_tg_map(struct cgroup_subsys_state *css, bool initial);
#endif