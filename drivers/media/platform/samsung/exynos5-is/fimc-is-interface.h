/*
 * Samsung EXYNOS5 FIMC-IS (Imaging Subsystem) driver
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 *  Arun Kumar K <arun.kk@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef FIMC_IS_INTERFACE_H_
#define FIMC_IS_INTERFACE_H_

#include "fimc-is-core.h"

#define TRY_RECV_AWARE_COUNT    100

#define ISDRV_VERSION		111

enum interrupt_map {
	INTR_GENERAL            = 0,
	INTR_ISP_FDONE          = 1,
	INTR_SCC_FDONE          = 2,
	INTR_DNR_FDONE          = 3,
	INTR_SCP_FDONE          = 4,
	INTR_ISP_YUV_DONE	= 5,
	INTR_META_DONE          = 6,
	INTR_SHOT_DONE          = 7,
	INTR_MAX_MAP
};

enum fimc_is_interface_state {
	IS_IF_STATE_INIT,
	IS_IF_STATE_OPEN,
	IS_IF_STATE_START,
	IS_IF_STATE_BUSY
};

enum streaming_state {
	IS_IF_STREAMING_INIT,
	IS_IF_STREAMING_OFF,
	IS_IF_STREAMING_ON
};

enum processing_state {
	IS_IF_PROCESSING_INIT,
	IS_IF_PROCESSING_OFF,
	IS_IF_PROCESSING_ON
};

enum pdown_ready_state {
	IS_IF_POWER_DOWN_READY,
	IS_IF_POWER_DOWN_NREADY
};

struct fimc_is_msg {
	u32 id;
	u32 command;
	u32 instance;
	u32 param[4];
};

struct fimc_is_interface {
	unsigned long			state;

	void __iomem			*regs;
	struct is_common_reg __iomem    *com_regs;
	/* Lock for writing into MCUCTL registers */
	spinlock_t			slock;
	/* Lock for context state variable */
	spinlock_t			slock_state;
	wait_queue_head_t		irq_queue;
	struct device			*dev;
	/* Held while sending commands to FW */
	struct mutex			request_barrier;

	enum streaming_state		streaming;
	enum processing_state		processing;
	enum pdown_ready_state		pdown_ready;

	struct fimc_is_msg		reply;

	int				debug_cnt;
	struct dentry                   *debugfs_entry;

};

int fimc_is_interface_init(struct fimc_is_interface *itf,
		void __iomem *regs, int irq);
int fimc_is_itf_wait_init_state(struct fimc_is_interface *itf);
int fimc_is_itf_open_sensor(struct fimc_is_interface *itf,
		unsigned int instance,
		unsigned int sensor_id,
		unsigned int i2c_channel,
		unsigned int sensor_ext);
int fimc_is_itf_get_setfile_addr(struct fimc_is_interface *this,
		unsigned int instance, unsigned int *setfile_addr);
int fimc_is_itf_load_setfile(struct fimc_is_interface *itf,
		unsigned int instance);
int fimc_is_itf_stream_on(struct fimc_is_interface *itf,
		unsigned int instance);
int fimc_is_itf_stream_off(struct fimc_is_interface *itf,
		unsigned int instance);
int fimc_is_itf_process_on(struct fimc_is_interface *itf,
		unsigned int instance);
int fimc_is_itf_process_off(struct fimc_is_interface *itf,
		unsigned int instance);
int fimc_is_itf_set_param(struct fimc_is_interface *this,
		unsigned int instance,
		unsigned int lindex,
		unsigned int hindex);
int fimc_is_itf_preview_still(struct fimc_is_interface *itf,
		unsigned int instance);
int fimc_is_itf_get_capability(struct fimc_is_interface *itf,
	unsigned int instance, unsigned int address);
int fimc_is_itf_cfg_mem(struct fimc_is_interface *itf,
		unsigned int instance, unsigned int address,
		unsigned int size);
int fimc_is_itf_shot_nblk(struct fimc_is_interface *itf,
		unsigned int instance, unsigned int bayer,
		unsigned int shot, unsigned int fcount, unsigned int rcount);
int fimc_is_itf_power_down(struct fimc_is_interface *itf,
		unsigned int instance);
#endif
