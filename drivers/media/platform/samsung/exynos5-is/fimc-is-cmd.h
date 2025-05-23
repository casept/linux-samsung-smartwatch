/*
 * Samsung Exynos5 SoC series FIMC-IS driver
 *
 * Copyright (c) 2013 Samsung Electronics Co., Ltd
 * Kil-yeon Lim <kilyeon.im@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef FIMC_IS_CMD_H
#define FIMC_IS_CMD_H

#define IS_COMMAND_VER 122 /* IS COMMAND VERSION 1.22 */

enum is_cmd {
	/* HOST -> IS */
	HIC_PREVIEW_STILL = 0x1,
	HIC_PREVIEW_VIDEO,
	HIC_CAPTURE_STILL,
	HIC_CAPTURE_VIDEO,
	HIC_PROCESS_START,
	HIC_PROCESS_STOP,
	HIC_STREAM_ON,
	HIC_STREAM_OFF,
	HIC_SHOT,
	HIC_GET_STATIC_METADATA,
	HIC_SET_CAM_CONTROL,
	HIC_GET_CAM_CONTROL,
	HIC_SET_PARAMETER,
	HIC_GET_PARAMETER,
	HIC_SET_A5_MEM_ACCESS,
	RESERVED2,
	HIC_GET_STATUS,
	/* SENSOR PART*/
	HIC_OPEN_SENSOR,
	HIC_CLOSE_SENSOR,
	HIC_SIMMIAN_INIT,
	HIC_SIMMIAN_WRITE,
	HIC_SIMMIAN_READ,
	HIC_POWER_DOWN,
	HIC_GET_SET_FILE_ADDR,
	HIC_LOAD_SET_FILE,
	HIC_MSG_CONFIG,
	HIC_MSG_TEST,
	/* IS -> HOST */
	IHC_GET_SENSOR_NUMBER = 0x1000,
	/* Parameter1 : Address of space to copy a setfile */
	/* Parameter2 : Space szie */
	IHC_SET_SHOT_MARK,
	/* PARAM1 : a frame number */
	/* PARAM2 : confidence level(smile 0~100) */
	/* PARMA3 : confidence level(blink 0~100) */
	IHC_SET_FACE_MARK,
	/* PARAM1 : coordinate count */
	/* PARAM2 : coordinate buffer address */
	IHC_FRAME_DONE,
	/* PARAM1 : frame start number */
	/* PARAM2 : frame count */
	IHC_AA_DONE,
	IHC_NOT_READY,
	IHC_FLASH_READY
};

enum is_reply {
	ISR_DONE	= 0x2000,
	ISR_NDONE
};

enum is_scenario_id {
	ISS_PREVIEW_STILL,
	ISS_PREVIEW_VIDEO,
	ISS_CAPTURE_STILL,
	ISS_CAPTURE_VIDEO,
	ISS_END
};

enum is_subscenario_id {
	ISS_SUB_SCENARIO_STILL,
	ISS_SUB_SCENARIO_VIDEO,
	ISS_SUB_SCENARIO_SCENE1,
	ISS_SUB_SCENARIO_SCENE2,
	ISS_SUB_SCENARIO_SCENE3,
	ISS_SUB_END
};

struct is_setfile_header_element {
	u32 binary_addr;
	u32 binary_size;
};

struct is_setfile_header {
	struct is_setfile_header_element isp[ISS_END];
	struct is_setfile_header_element drc[ISS_END];
	struct is_setfile_header_element fd[ISS_END];
};

struct is_common_reg {
	u32 hicmd;
	u32 hic_sensorid;
	u32 hic_param[4];

	u32 reserved1[3];

	u32 ihcmd_iflag;
	u32 ihcmd;
	u32 ihc_sensorid;
	u32 ihc_param[4];

	u32 reserved2[3];

	u32 isp_bayer_iflag;
	u32 isp_bayer_sensor_id;
	u32 isp_bayer_param[2];

	u32 reserved3[4];

	u32 scc_iflag;
	u32 scc_sensor_id;
	u32 scc_param[3];

	u32 reserved4[3];

	u32 dnr_iflag;
	u32 dnr_sensor_id;
	u32 dnr_param[2];

	u32 reserved5[4];

	u32 scp_iflag;
	u32 scp_sensor_id;
	u32 scp_param[3];

	u32 reserved6[1];

	u32 isp_yuv_iflag;
	u32 isp_yuv_sensor_id;
	u32 isp_yuv_param[2];

	u32 reserved7[1];

	u32 shot_iflag;
	u32 shot_sensor_id;
	u32 shot_param[2];

	u32 reserved8[1];

	u32 meta_iflag;
	u32 meta_sensor_id;
	u32 meta_param1;

	u32 reserved9[1];

	u32 fcount;
};

struct is_mcuctl_reg {
	u32 mcuctl;
	u32 bboar;

	u32 intgr0;
	u32 intcr0;
	u32 intmr0;
	u32 intsr0;
	u32 intmsr0;

	u32 intgr1;
	u32 intcr1;
	u32 intmr1;
	u32 intsr1;
	u32 intmsr1;

	u32 intcr2;
	u32 intmr2;
	u32 intsr2;
	u32 intmsr2;

	u32 gpoctrl;
	u32 cpoenctlr;
	u32 gpictlr;

	u32 pad[0xD];

	struct is_common_reg common_reg;
};
#endif
