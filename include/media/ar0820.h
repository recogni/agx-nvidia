/**
 * Copyright (c) 2014-2019, NVIDIA Corporation.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software and related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 */

#ifndef __AR0820_H__
#define __AR0820_H__

#include <uapi/media/ar0820.h>

#define AR0820_FUSE_ID_SIZE		6
#define AR0820_FUSE_ID_STR_SIZE		(AR0820_FUSE_ID_SIZE * 2)

struct ar0820_power_rail {
	struct regulator *dvdd;
	struct regulator *avdd;
	struct regulator *iovdd;
	struct regulator *vdd_af;
};

struct ar0820_platform_data {
	struct ar0820_flash_control flash_cap;
	const char *mclk_name; /* NULL for default default_mclk */
	int (*power_on)(struct ar0820_power_rail *pw);
	int (*power_off)(struct ar0820_power_rail *pw);
};

#endif  /* __AR0820_H__ */
