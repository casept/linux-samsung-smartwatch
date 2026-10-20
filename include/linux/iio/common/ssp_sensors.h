/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Copyright (C) 2014, Samsung Electronics Co. Ltd. All Rights Reserved.
 */
#ifndef _SSP_SENSORS_H_
#define _SSP_SENSORS_H_

#include <linux/iio/iio.h>

#define SSP_TIME_SIZE				4
#define SSP_ACCELEROMETER_SIZE			6
#define SSP_GYROSCOPE_SIZE			6
#define SSP_BIO_HRM_RAW_SIZE			8
#define SSP_BIO_HRM_RAW_FAC_SIZE		36
#define SSP_BIO_HRM_LIB_SIZE			8

/* Largest scan without the timestamp: the nine 32 bit raw HRM channels */
#define SSP_MAX_SCAN_SIZE			36

/**
 * enum ssp_sensor_type - SSP sensor type
 */
enum ssp_sensor_type {
	SSP_ACCELEROMETER_SENSOR = 0,
	SSP_GYROSCOPE_SENSOR,
	SSP_GEOMAGNETIC_UNCALIB_SENSOR,
	SSP_GEOMAGNETIC_RAW,
	SSP_GEOMAGNETIC_SENSOR,
	SSP_PRESSURE_SENSOR,
	SSP_GESTURE_SENSOR,
	SSP_PROXIMITY_SENSOR,
	SSP_TEMPERATURE_HUMIDITY_SENSOR,
	SSP_LIGHT_SENSOR,
	SSP_PROXIMITY_RAW,
	SSP_ORIENTATION_SENSOR,
	SSP_STEP_DETECTOR,
	SSP_SIG_MOTION_SENSOR,
	SSP_GYRO_UNCALIB_SENSOR,
	SSP_GAME_ROTATION_VECTOR,
	SSP_ROTATION_VECTOR,
	SSP_STEP_COUNTER,
	SSP_BIO_HRM_RAW,
	SSP_BIO_HRM_RAW_FAC,
	SSP_BIO_HRM_LIB,
	SSP_SENSOR_MAX,
};

struct ssp_data;

/**
 * struct ssp_sensor_data - Sensor object
 * @process_data:	Callback to feed sensor data.
 * @type:		Used sensor type.
 * @streaming:		Data may be pushed to the IIO buffer.
 * @buffer:		Scan assembled from the received data.
 */
struct ssp_sensor_data {
	int (*process_data)(struct iio_dev *indio_dev, void *buf,
			    int64_t timestamp);
	enum ssp_sensor_type type;
	bool streaming;
	IIO_DECLARE_BUFFER_WITH_TS(u8, buffer, SSP_MAX_SCAN_SIZE);
};

int ssp_register_consumer(struct iio_dev *indio_dev,
			  enum ssp_sensor_type type);

/* Library ids are a separate number space from the sensor types */
#define SSP_LIBRARY_MAX				64
#define SSP_LIBRARY_ADD_MAX_SIZE		16
#define SSP_LIBRARY_REMOVE_SIZE			2

/**
 * struct ssp_library - Consumer of an MCU library
 * @id:			Library id.
 * @data:		Sensorhub the library is registered with.
 * @process_data:	Callback to feed a library record, called from the
 *			interrupt thread. Records aren't delimited, it returns
 *			the length of the record at the start of the buffer,
 *			at most the length of the buffer, or a negative error
 *			code.
 * @readded:		Optional callback after the library got added again
 *			following an MCU reset, which lost its state.
 * @remove_buf:	Arguments to remove the library with, zeroes unless the
 *			library needs others.
 * @add_buf:		Arguments the library was last added with.
 * @add_len:		Length of @add_buf.
 * @added:		The library is to run on the MCU, it is added again after
 *			an MCU reset, also when adding it again failed before.
 */
struct ssp_library {
	u8 id;
	struct ssp_data *data;
	int (*process_data)(struct ssp_library *lib, const u8 *buf,
			    unsigned int len);
	void (*readded)(struct ssp_library *lib);
	u8 remove_buf[SSP_LIBRARY_REMOVE_SIZE];
	u8 add_buf[SSP_LIBRARY_ADD_MAX_SIZE];
	u8 add_len;
	bool added;
};

void ssp_library_init(struct device *dev, struct ssp_library *lib);

int ssp_register_library(struct device *dev, struct ssp_library *lib);

int ssp_library_add(struct ssp_library *lib, const void *buf, u8 len);

int ssp_library_remove(struct ssp_library *lib);

int ssp_library_request(struct ssp_library *lib, const void *buf, u8 len);

/*
 * Profile of the user, one for all libraries, in cm, kg and 1 for male or 2
 * for female
 */
enum ssp_profile_field {
	SSP_PROFILE_HEIGHT,
	SSP_PROFILE_WEIGHT,
	SSP_PROFILE_GENDER,
	SSP_PROFILE_MAX,
};

#define SSP_PROFILE_MALE			1
#define SSP_PROFILE_FEMALE			2

u8 ssp_library_get_profile(struct ssp_library *lib,
			   enum ssp_profile_field field);

int ssp_library_set_profile(struct ssp_library *lib,
			    enum ssp_profile_field field, u8 val);

int ssp_enable_sensor(struct ssp_data *data, enum ssp_sensor_type type,
		      u32 delay);

int ssp_disable_sensor(struct ssp_data *data, enum ssp_sensor_type type);

u32 ssp_get_sensor_delay(struct ssp_data *data, enum ssp_sensor_type);

int ssp_change_delay(struct ssp_data *data, enum ssp_sensor_type type,
		     u32 delay);

void ssp_sync_consumers(struct ssp_data *data);
#endif /* _SSP_SENSORS_H_ */
