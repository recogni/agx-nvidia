/**
 * include/uapi/linux/nvhvivc_mempool_ioctl.h
 *
 * Copyright (c) 2016-2020, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software and related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 */

#ifndef __UAPI_AR0820_H__
#define __UAPI_AR0820_H__

#include <linux/ioctl.h>
#include <media/nvc.h>

#define AR0820_IOCTL_SET_MODE		_IOW('o', 1, struct ar0820_mode)
#define AR0820_IOCTL_GET_STATUS		_IOR('o', 2, __u8)
#define AR0820_IOCTL_SET_FRAME_LENGTH	_IOW('o', 3, __u32)
#define AR0820_IOCTL_SET_COARSE_TIME	_IOW('o', 4, __u32)
#define AR0820_IOCTL_SET_GAIN		_IOW('o', 5, struct ar0820_gain)
#define AR0820_IOCTL_GET_FUSEID		_IOR('o', 6, struct nvc_fuseid)
#define AR0820_IOCTL_SET_GROUP_HOLD	_IOW('o', 7, struct ar0820_ae)
#define AR0820_IOCTL_GET_AFDAT		_IOR('o', 8, __u32)
#define AR0820_IOCTL_SET_POWER		_IOW('o', 20, __u32)
#define AR0820_IOCTL_GET_FLASH_CAP	_IOR('o', 30, __u32)
#define AR0820_IOCTL_SET_FLASH_MODE	_IOW('o', 31, \
						struct ar0820_flash_control)

/* TODO: revisit these values for AR0820 */
#define AR0820_FRAME_LENGTH_ADDR_MSB		0x0160
#define AR0820_FRAME_LENGTH_ADDR_LSB		0x0161
#define AR0820_COARSE_TIME_ADDR_MSB		0x015a
#define AR0820_COARSE_TIME_ADDR_LSB		0x015b
#define AR0820_GAIN_ADDR			0x0157

struct ar0820_flash_control {
	__u8 enable;
	__u8 edge_trig_en;
	__u8 start_edge;
	__u8 repeat;
	__u16 delay_frm;
};

struct ar0820_mode {
	int xres;
	int yres;
	__u32 frame_length;
	__u32 coarse_time;
	__u32 gain;
};

struct ar0820_ae {
	__u32 frame_length;
	__u8  frame_length_enable;
	__u32 coarse_time;
	__u8  coarse_time_enable;
	__u32 gain;
	__u8  gain_enable;
};

#endif  /* __UAPI_AR0820_H__ */
