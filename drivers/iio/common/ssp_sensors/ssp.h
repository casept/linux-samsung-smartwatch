/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Copyright (C) 2014, Samsung Electronics Co. Ltd. All Rights Reserved.
 */

#ifndef __SSP_SENSORHUB_H__
#define __SSP_SENSORHUB_H__

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/iio/common/ssp_sensors.h>
#include <linux/iio/iio.h>
#include <linux/kfifo.h>
#include <linux/sizes.h>
#include <linux/spi/spi.h>
#include <linux/wait.h>

#define SSP_DEVICE_ID		0x55

#define SSP_SW_RESET_TIME		3000
/* Sensor polling in ms */
#define SSP_DEFAULT_POLLING_DELAY	200
#define SSP_DEFAULT_RETRIES		3
#define SSP_DATA_PACKET_SIZE		960
#define SSP_HEADER_BUFFER_SIZE		4

enum {
	SSP_KERNEL_BINARY = 0,
	SSP_KERNEL_CRASHED_BINARY,
};

/* Firmware download STATE */
enum {
	SSP_FW_DL_STATE_FAIL = -1,
	SSP_FW_DL_STATE_NONE = 0,
	SSP_FW_DL_STATE_NEED_TO_SCHEDULE,
	SSP_FW_DL_STATE_SCHEDULED,
	SSP_FW_DL_STATE_DOWNLOADING,
	SSP_FW_DL_STATE_SYNC,
	SSP_FW_DL_STATE_DONE,
};

#define SSP_INVALID_REVISION			99999
#define SSP_INVALID_REVISION2			0xffffff

/* AP -> SSP Instruction */
#define SSP_MSG2SSP_INST_BYPASS_SENSOR_ADD	0xa1
#define SSP_MSG2SSP_INST_BYPASS_SENSOR_RM	0xa2
#define SSP_MSG2SSP_INST_REMOVE_ALL		0xa3
#define SSP_MSG2SSP_INST_CHANGE_DELAY		0xa4
#define SSP_MSG2SSP_INST_LIBRARY_ADD		0xb1
#define SSP_MSG2SSP_INST_LIBRARY_REMOVE		0xb2
#define SSP_MSG2SSP_INST_LIB_NOTI		0xb4
#define SSP_MSG2SSP_INST_LIB_DATA		0xc1

#define SSP_MSG2SSP_AP_MCU_SET_GYRO_CAL		0xcd
#define SSP_MSG2SSP_AP_MCU_SET_ACCEL_CAL	0xce
#define SSP_MSG2SSP_AP_STATUS_SHUTDOWN		0xd0
#define SSP_MSG2SSP_AP_STATUS_WAKEUP		0xd1
#define SSP_MSG2SSP_AP_STATUS_SLEEP		0xd2
#define SSP_MSG2SSP_AP_STATUS_RESUME		0xd3
#define SSP_MSG2SSP_AP_STATUS_SUSPEND		0xd4
#define SSP_MSG2SSP_AP_STATUS_RESET		0xd5
#define SSP_MSG2SSP_AP_STATUS_POW_CONNECTED	0xd6
#define SSP_MSG2SSP_AP_STATUS_POW_DISCONNECTED	0xd7
#define SSP_MSG2SSP_AP_TEMPHUMIDITY_CAL_DONE	0xda
#define SSP_MSG2SSP_AP_MCU_SET_DUMPMODE		0xdb
#define SSP_MSG2SSP_AP_MCU_DUMP_CHECK		0xdc
#define SSP_MSG2SSP_AP_MCU_BATCH_FLUSH		0xdd
#define SSP_MSG2SSP_AP_MCU_BATCH_COUNT		0xdf

#define SSP_MSG2SSP_AP_WHOAMI				0x0f
#define SSP_MSG2SSP_AP_FIRMWARE_REV			0xf0
#define SSP_MSG2SSP_AP_SENSOR_FORMATION			0xf1
#define SSP_MSG2SSP_AP_SENSOR_PROXTHRESHOLD		0xf2
#define SSP_MSG2SSP_AP_SENSOR_BARCODE_EMUL		0xf3
#define SSP_MSG2SSP_AP_SENSOR_SCANNING			0xf4
#define SSP_MSG2SSP_AP_SET_MAGNETIC_HWOFFSET		0xf5
#define SSP_MSG2SSP_AP_GET_MAGNETIC_HWOFFSET		0xf6
#define SSP_MSG2SSP_AP_SENSOR_GESTURE_CURRENT		0xf7
#define SSP_MSG2SSP_AP_GET_THERM			0xf8
#define SSP_MSG2SSP_AP_GET_BIG_DATA			0xf9
#define SSP_MSG2SSP_AP_SET_BIG_DATA			0xfa
#define SSP_MSG2SSP_AP_START_BIG_DATA			0xfb
#define SSP_MSG2SSP_AP_SET_MAGNETIC_STATIC_MATRIX	0xfd
#define SSP_MSG2SSP_AP_SENSOR_TILT			0xea
#define SSP_MSG2SSP_AP_MCU_SET_TIME			0xfe
#define SSP_MSG2SSP_AP_MCU_GET_TIME			0xff

#define SSP_MSG2SSP_AP_FUSEROM				0x01

/* voice data */
#define SSP_TYPE_WAKE_UP_VOICE_SERVICE			0x01
#define SSP_TYPE_WAKE_UP_VOICE_SOUND_SOURCE_AM		0x01
#define SSP_TYPE_WAKE_UP_VOICE_SOUND_SOURCE_GRAMMER	0x02

/* Factory Test */
#define SSP_ACCELEROMETER_FACTORY			0x80
#define SSP_GYROSCOPE_FACTORY				0x81
#define SSP_GEOMAGNETIC_FACTORY				0x82
#define SSP_PRESSURE_FACTORY				0x85
#define SSP_GESTURE_FACTORY				0x86
#define SSP_TEMPHUMIDITY_CRC_FACTORY			0x88
#define SSP_GYROSCOPE_TEMP_FACTORY			0x8a
#define SSP_GYROSCOPE_DPS_FACTORY			0x8b
#define SSP_MCU_FACTORY					0x8c
#define SSP_MCU_SLEEP_FACTORY				0x8d

/* SSP -> AP ACK about write CMD */
#define SSP_MSG_ACK		0x80	/* ACK from SSP to AP */
#define SSP_MSG_NAK		0x70	/* NAK from SSP to AP */

struct ssp_sensorhub_info {
	char *fw_name;
	char *fw_crashed_name;
	unsigned int fw_rev;
	const u8 * const mag_table;
	const unsigned int mag_length;
};

/* ssp_msg options bit */
#define SSP_RW		0
#define SSP_INDEX	3

#define SSP_AP2HUB_READ		0
#define SSP_AP2HUB_WRITE	1
#define SSP_HUB2AP_WRITE	2
#define SSP_AP2HUB_READY	3
#define SSP_AP2HUB_RETURN	4

/**
 * struct ssp_data - ssp platformdata structure
 * @spi:		spi device
 * @sensorhub_info:	info about sensorhub board specific features
 * @wdt_timer:		watchdog timer
 * @work_wdt:		watchdog work
 * @work_stale:		work removing sensors the MCU runs on its own
 * @stale_sensors:	sensors the MCU sent data for without being enabled
 * @stale_failures:	failed attempts to remove each stale sensor in a row
 * @work_firmware:	firmware upgrade work queue
 * @work_refresh:	refresh work queue for reset request from MCU
 * @shut_down:		shut down flag
 * @irq_wake:		the interrupt is enabled as wakeup source
 * @mcu_dump_mode:	mcu dump mode for debug
 * @time_syncing:	time syncing indication flag
 * @timestamp:		previous time in ns calculated for time syncing
 * @com_fail_cnt:	communication fail count
 * @reset_cnt:		reset count
 * @refresh_failures:	failed MCU initializations in a row
 * @refresh_requested:	a reset of the MCU wants it initialized again
 * @timeout_cnt:	timeout count
 * @available_sensors:	available sensors seen by sensorhub (bit array)
 * @cur_firm_rev:	cached current firmware revision
 * @last_resume_state:	last AP resume/suspend state used to handle the PM
 *                      state of ssp
 * @last_ap_state:	(obsolete) sleep notification for MCU
 * @sensor_enable:	sensor enable mask
 * @sensor_unremoved:	disabled sensors the MCU may still run
 * @sensor_given_up:	disabled sensors counted removed after failed removals
 * @delay_buf:		data acquisition intervals table
 * @batch_latency_buf:	yet unknown but existing in communication protocol
 * @batch_opt_buf:	yet unknown but existing in communication protocol
 * @accel_position:	yet unknown but existing in communication protocol
 * @mag_position:	yet unknown but existing in communication protocol
 * @fw_dl_state:	firmware download state
 * @comm_lock:		lock protecting the handshake
 * @pending_lock:	lock protecting pending list and completion
 * @enable_lock:	lock serializing sensor state changes, MCU init and the
 *			HRM supply
 * @hrm_supply:		heart rate monitor supply
 * @hrm_powered:	whether the HRM supply is enabled
 * @mcu_reset_gpiod:	mcu reset line
 * @ap_mcu_gpiod:	ap to mcu gpio line
 * @mcu_ap_gpiod:	mcu to ap gpio line
 * @pending_list:	pending list for messages queued to be sent/read
 * @sensor_devs:	registered IIO devices table
 * @debugfs:		debugfs directory
 * @mcu_log:		debug messages of the MCU
 * @mcu_log_lock:	lock protecting @mcu_log
 * @mcu_log_wq:		wait queue of @mcu_log readers
 * @mcu_log_closed:	@mcu_log is going away, readers return
 * @header_buffer:	cache aligned buffer for packet header
 */
struct ssp_data {
	struct spi_device *spi;
	const struct ssp_sensorhub_info *sensorhub_info;
	struct timer_list wdt_timer;
	struct work_struct work_wdt;
	struct delayed_work work_stale;
	unsigned long stale_sensors;
	u8 stale_failures[SSP_SENSOR_MAX];
	struct delayed_work work_refresh;

	bool shut_down;
	bool irq_wake;
	bool mcu_dump_mode;
	bool time_syncing;
	int64_t timestamp;

	unsigned int com_fail_cnt;
	unsigned int reset_cnt;
	unsigned int refresh_failures;
	atomic_t refresh_requested;
	unsigned int timeout_cnt;

	unsigned int available_sensors;
	unsigned int cur_firm_rev;

	char last_resume_state;
	char last_ap_state;

	unsigned int sensor_enable;
	unsigned int sensor_unremoved;
	unsigned int sensor_given_up;
	u32 delay_buf[SSP_SENSOR_MAX];
	s32 batch_latency_buf[SSP_SENSOR_MAX];
	s8 batch_opt_buf[SSP_SENSOR_MAX];

	int accel_position;
	int mag_position;
	int fw_dl_state;

	struct mutex comm_lock;
	struct mutex pending_lock;
	struct mutex enable_lock;

	struct regulator *hrm_supply;
	bool hrm_powered;

	struct gpio_desc *mcu_reset_gpiod;
	struct gpio_desc *ap_mcu_gpiod;
	struct gpio_desc *mcu_ap_gpiod;

	struct list_head pending_list;

	struct iio_dev *sensor_devs[SSP_SENSOR_MAX];

	struct dentry *debugfs;
	DECLARE_KFIFO(mcu_log, char, SZ_16K);
	struct mutex mcu_log_lock;
	wait_queue_head_t mcu_log_wq;
	bool mcu_log_closed;

	__le16 header_buffer[SSP_HEADER_BUFFER_SIZE / sizeof(__le16)] __aligned(IIO_DMA_MINALIGN);
};

void ssp_clean_pending_list(struct ssp_data *data);

int ssp_command(struct ssp_data *data, char command, int arg);

int ssp_send_instruction(struct ssp_data *data, u8 inst, u8 sensor_type,
			 u8 *send_buf, u8 length);

int ssp_irq_msg(struct ssp_data *data);

int ssp_get_chipid(struct ssp_data *data);

int ssp_set_magnetic_matrix(struct ssp_data *data);

unsigned int ssp_get_sensor_scanning_info(struct ssp_data *data);

unsigned int ssp_get_firmware_rev(struct ssp_data *data);

int ssp_queue_ssp_refresh_task(struct ssp_data *data, unsigned int delay);

void ssp_remove_stale_sensor(struct ssp_data *data,
			     enum ssp_sensor_type type);

void ssp_log_mcu_message(struct ssp_data *data, const char *msg,
			 unsigned int len);

void ssp_debugfs_init(struct ssp_data *data);

void ssp_debugfs_remove(struct ssp_data *data);

#endif /* __SSP_SENSORHUB_H__ */
