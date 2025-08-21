/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef _SDCA_REGISTERS_API_H
#define _SDCA_REGISTERS_API_H

#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <sound/soc-component.h>

struct sdca_debugfs_info {
	struct dentry *sdca_dentry;
	struct dentry *registers;
	struct dentry *address;
	struct dentry *data;
};

struct sdca_regdump_info {
	struct snd_soc_component *component;
	const u32 *reg_array;
	u32 reg_address;
	int reg_num;
	bool (*sdca_readable_register)(u32 reg);
	bool (*sdca_writeable_register)(u32 reg);
};

extern void sdca_devices_debugfs_dentry_create(struct sdca_debugfs_info *debugfs_info,
				struct sdca_regdump_info *regdump_info);
extern void sdca_devices_debugfs_dentry_remove(struct sdca_debugfs_info *debugfs_info);

#endif

