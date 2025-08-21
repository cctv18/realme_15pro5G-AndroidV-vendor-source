#include <linux/cgroup.h>
#include <linux/proc_fs.h>
#include <linux/sched/cputime.h>
#include <kernel/sched/sched.h>
#include <trace/hooks/cgroup.h>
#include <trace/hooks/sched.h>
#include "sa_group.h"

#if IS_ENABLED(CONFIG_OPLUS_FEATURE_SCHED_DDL)
#include "sa_ddl.h"
#endif

LIST_HEAD(css_tg_map_list);

static struct task_group *css_tg(struct cgroup_subsys_state *css)
{
	return css ? container_of(css, struct task_group, css) : NULL;
}

static ssize_t tg_map_read(struct file *file, char __user *buf,
		size_t count, loff_t *ppos)
{
	char buffer[MAX_OUTPUT];
	size_t len = 0;
	struct css_tg_map *iter = NULL;

	memset(buffer, 0, sizeof(buffer));

	rcu_read_lock();
	list_for_each_entry_rcu(iter, &css_tg_map_list, map_list) {
		len += snprintf(buffer + len, sizeof(buffer) - len, "%s:%d:%llu ",
			iter->tg_name, iter->id, iter->ddl);
		if (len > MAX_GUARD_SIZE) {
			len += sprintf(buffer + len, "... ");
			break;
		}
	}
	rcu_read_unlock();

	buffer[len-1] = '\n';

	return simple_read_from_buffer(buf, count, ppos, buffer, len);
};

static struct css_tg_map *get_tg_map(const char *tg_name)
{
	struct css_tg_map *iter = NULL;

	rcu_read_lock();
	list_for_each_entry_rcu(iter, &css_tg_map_list, map_list) {
		if (!(strncmp(iter->tg_name, tg_name, strlen(tg_name)))) {

			if (strlen(iter->tg_name) != strlen(tg_name))
				continue;

			rcu_read_unlock();
			return iter;
		}
	}
	rcu_read_unlock();

	return NULL;
}

static const struct proc_ops tg_map_fops = {
	.proc_read		= tg_map_read,
};

#if IS_ENABLED(CONFIG_OPLUS_FEATURE_SCHED_DDL)
void oplus_update_ddl_tasks(struct cgroup_subsys_state *css, struct css_tg_map *map)
{
	struct css_task_iter it;
	struct task_struct *p;

	css_task_iter_start(css, 0, &it);
	while ((p = css_task_iter_next(&it)))
		oplus_set_task_ddl(p, map->ddl);

	css_task_iter_end(&it);
}

static u64 sg_ddl_read_u64(struct cgroup_subsys_state *css,
		struct cftype *cft)
{
	struct task_group *tg = css_tg(css);
	struct css_tg_map *map = NULL;

	map = (struct css_tg_map *) READ_ONCE(tg->android_kabi_reserved3);
	if (IS_ERR_OR_NULL(map))
		return 0;

	return map->ddl;
}

static int sg_ddl_write_u64(struct cgroup_subsys_state *css,
		struct cftype *cft, u64 ddlval)
{
	struct task_group *tg = css_tg(css);
	struct css_tg_map *map = NULL;

	map = (struct css_tg_map *) READ_ONCE(tg->android_kabi_reserved3);
	if (IS_ERR_OR_NULL(map))
		return -ENOENT;

	if (ddlval >= MAX_DDL_LIMIT)
		return -EINVAL;

	WRITE_ONCE(map->ddl, ddlval);
	oplus_update_ddl_tasks(css, map);
	return 0;
}

static u64 sg_ddl_rthres_read(struct cgroup_subsys_state *css,
		struct cftype *cft)
{
	struct task_group *tg = css_tg(css);
	struct css_tg_map *map = NULL;

	map = (struct css_tg_map *) READ_ONCE(tg->android_kabi_reserved3);
	if (IS_ERR_OR_NULL(map))
		return 0;

	return map->ddl_rthres;
}

static int sg_ddl_rthres_write(struct cgroup_subsys_state *css,
		struct cftype *cft, u64 ddl_rthres)
{
	struct task_group *tg = css_tg(css);
	struct css_tg_map *map = NULL;

	map = (struct css_tg_map *) READ_ONCE(tg->android_kabi_reserved3);
	if (IS_ERR_OR_NULL(map))
		return -ENOENT;

	if (ddl_rthres >= MAX_DDL_RTHRES)
		return -EINVAL;

	WRITE_ONCE(map->ddl_rthres, ddl_rthres);
	return 0;
}

static struct cftype sg_ddl_files[] = {
	{
		.name = "sg.ddl",
		.read_u64 = sg_ddl_read_u64,
		.write_u64 = sg_ddl_write_u64,
	},
	{
		.name = "sg.ddl_rthres",
		.read_u64 = sg_ddl_rthres_read,
		.write_u64 = sg_ddl_rthres_write,
	},
	{ }, /* terminate */
};

const struct oplus_sg_ddl cgrp_ddl_info[DDL_CGRP_MAX] = {
	{
		.ddl = 300,
		.ddl_rthres = 60,
	},
	{
		.tg_name = "foreground",
		.ddl = 200,
		.ddl_rthres = 80,
	},
	{
		.tg_name = "background",
		.ddl = 600,
		.ddl_rthres = 50,
	},
	{
		.tg_name = "top-app",
		.ddl = 100,
		.ddl_rthres = 100,
	},
	{
		.tg_name = "system-background",
		.ddl = 500,
		.ddl_rthres = 60,
	},
	{
		.tg_name = "bg",
		.ddl = 600,
		.ddl_rthres = 50,
	},
	{
		.ddl = 400,
		.ddl_rthres = 60,
	},
};

static const struct oplus_sg_ddl *get_oplus_sg_ddl(int id, const char *tg_name)
{
	int iter = FOREGROUND;

	if (0 >= id)
		return NULL;

	if (1 == id)
		return &cgrp_ddl_info[ROOTGROUP];

	while (iter < DDL_CGRP_DEFAULT) {
		if(!(strncmp(cgrp_ddl_info[iter].tg_name, tg_name, strlen(tg_name))))
			return &cgrp_ddl_info[iter];
		iter++;
	}

	return &cgrp_ddl_info[DDL_CGRP_DEFAULT];
}

u64 get_sg_ddl_rthres(struct task_group *tg)
{
	struct css_tg_map *map = NULL;

	if (!tg)
		return SG_DDL_RTHRES_DEFAULT;

	map = (struct css_tg_map *) READ_ONCE(tg->android_kabi_reserved3);
	if (IS_ERR_OR_NULL(map))
		return SG_DDL_RTHRES_DEFAULT;

	return map->ddl_rthres;
}

static struct css_tg_map *get_oplus_tg_map(struct task_group *tg)
{
	struct css_tg_map *map = NULL;

	if (!tg)
		return NULL;

	map = (struct css_tg_map *) READ_ONCE(tg->android_kabi_reserved3);
	if (IS_ERR_OR_NULL(map))
		return NULL;

	return map;
}

#else

static const struct oplus_sg_ddl *get_oplus_sg_ddl(int id, const char *tg_name)
{
	return NULL;
}

u64 get_sg_ddl_rthres(struct task_group *tg)
{
	return 0;
}

void oplus_update_ddl_tasks(struct cgroup_subsys_state *css, struct css_tg_map *map)
{

}

#endif

static struct css_tg_map *map_node_init(struct cgroup_subsys_state *css, bool initial)
{
	struct cgroup *cgrp = NULL;
	struct css_tg_map *map = NULL;
	struct task_group *tg = NULL;
	const struct oplus_sg_ddl *sg_ddl = NULL;

	map = kzalloc(sizeof(struct css_tg_map), GFP_KERNEL);
	if (!map || !css) {
		pr_err("alloc %s tg_map failed\n",
			css && css->cgroup && css->cgroup->kn ? css->cgroup->kn->name : "CSS_NONAME");
		return NULL;
	}

	tg = css_tg(css);
	cgrp = css->cgroup;
	sg_ddl = get_oplus_sg_ddl(css->id, cgrp->kn->name);
	if (sg_ddl) {
		map->ddl = sg_ddl->ddl;
		map->ddl_rthres = sg_ddl->ddl_rthres;
	}
	map->tg_name = kstrdup_const(cgrp->kn->name, GFP_KERNEL);
	map->id = css->id;

	if (likely(tg)) {
		smp_mb();
		WRITE_ONCE(tg->android_kabi_reserved3, (u64) map);
	}
	if (initial)
		oplus_update_ddl_tasks(css, map);

	return map;
}

void oplus_update_tg_map(struct cgroup_subsys_state *css, bool initial)
{
	struct cgroup *cgrp = css->cgroup;
	struct css_tg_map *map = NULL, *iter = NULL;

	if (!cgrp || !cgrp->kn)
		return;

	if (!(map = map_node_init(css, initial)))
		return;

	iter = get_tg_map(cgrp->kn->name);
	if (iter) {
		list_replace_rcu(&iter->map_list, &map->map_list);
		kfree_const(iter->tg_name);
		kfree(iter);
		return;
	}
	list_add_tail_rcu(&map->map_list, &css_tg_map_list);
}
EXPORT_SYMBOL(oplus_update_tg_map);

static void android_vh_sched_move_task_handler(void *unused, struct task_struct *tsk)
{
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_SCHED_DDL)
	struct css_tg_map *map = NULL;
	struct task_group *tg = NULL;
	u64 ddl;

	tg = container_of(task_css(tsk, cpu_cgrp_id),
		struct task_group, css);
	if (tg == tsk->sched_task_group)
		return;

	map = get_oplus_tg_map(tg);
	if (!map)
		return;

	ddl = oplus_get_task_ddl(tsk);
	if (ddl == map->ddl)
		return;

	oplus_set_task_ddl(tsk, map->ddl);
#endif
}

void oplus_sg_wake_up_new_task(struct task_struct *tsk)
{
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_SCHED_DDL)
	struct css_tg_map *map = NULL;
	struct task_group *tg = NULL;

	tg = tsk->sched_task_group;
	map = get_oplus_tg_map(tg);
	if (!map)
		return;

	oplus_set_task_ddl(tsk, map->ddl);
#endif
}

static void register_oplus_cgrp_hooks(void)
{
	register_trace_android_vh_sched_move_task(android_vh_sched_move_task_handler, NULL);
}

void oplus_sched_group_init(struct proc_dir_entry *pde)
{
	struct proc_dir_entry *proc_node;
	proc_node = proc_create("tg_map", 0666, pde, &tg_map_fops);
	if (!proc_node) {
		pr_err("failed to create proc node tg_css_map\n");
		remove_proc_entry("tg_map", pde);
	}

	register_oplus_cgrp_hooks();

#if IS_ENABLED(CONFIG_OPLUS_FEATURE_SCHED_DDL)
	int ret = cgroup_add_legacy_cftypes(&cpu_cgrp_subsys, sg_ddl_files);
	if (ret < 0)
		pr_err("add sg_ddl file fail\n");
#endif
}
