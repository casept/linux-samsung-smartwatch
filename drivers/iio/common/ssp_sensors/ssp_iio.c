// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2014, Samsung Electronics Co. Ltd. All Rights Reserved.
 */

#include <linux/iio/common/ssp_sensors.h>
#include <linux/iio/buffer.h>
#include <linux/iio/kfifo_buf.h>
#include <linux/module.h>
#include <linux/unaligned.h>
#include <linux/units.h>
#include "ssp_iio_sensor.h"

/**
 * ssp_common_buffer_postenable() - generic postenable callback for ssp buffer
 *
 * @indio_dev:		iio device
 *
 * Returns 0 or negative value in case of error
 */
int ssp_common_buffer_postenable(struct iio_dev *indio_dev)
{
	int ret;
	struct ssp_sensor_data *spd = iio_priv(indio_dev);
	struct ssp_data *data = dev_get_drvdata(indio_dev->dev.parent->parent);

	if (indio_dev->scan_bytes > sizeof(spd->buffer))
		return -EINVAL;

	ret = ssp_enable_sensor(data, spd->type,
				ssp_get_sensor_delay(data, spd->type));
	if (ret < 0)
		return ret;

	/* Pairs with smp_load_acquire() in ssp_common_process_data() */
	smp_store_release(&spd->streaming, true);

	return 0;
}
EXPORT_SYMBOL_NS(ssp_common_buffer_postenable, "IIO_SSP_SENSORS");

/**
 * ssp_common_buffer_predisable() - generic predisable callback for ssp buffer
 *
 * @indio_dev:		iio device
 *
 * Stops pushing data before the IIO core tears the buffer down.
 *
 * Return: 0
 */
int ssp_common_buffer_predisable(struct iio_dev *indio_dev)
{
	struct ssp_sensor_data *spd = iio_priv(indio_dev);
	struct ssp_data *data = dev_get_drvdata(indio_dev->dev.parent->parent);

	WRITE_ONCE(spd->streaming, false);
	ssp_sync_consumers(data);

	return 0;
}
EXPORT_SYMBOL_NS(ssp_common_buffer_predisable, "IIO_SSP_SENSORS");

/**
 * ssp_common_buffer_postdisable() - generic postdisable callback for ssp buffer
 *
 * @indio_dev:		iio device
 *
 * Returns 0 or negative value in case of error
 */
int ssp_common_buffer_postdisable(struct iio_dev *indio_dev)
{
	struct ssp_sensor_data *spd = iio_priv(indio_dev);
	struct ssp_data *data = dev_get_drvdata(indio_dev->dev.parent->parent);

	return ssp_disable_sensor(data, spd->type);
}
EXPORT_SYMBOL_NS(ssp_common_buffer_postdisable, "IIO_SSP_SENSORS");

/**
 * ssp_common_process_data() - Common process data callback for ssp sensors
 *
 * @indio_dev:		iio device
 * @buf:		source buffer
 * @len:		sensor data length
 * @timestamp:		system timestamp
 *
 * Returns 0 or negative value in case of error
 */
int ssp_common_process_data(struct iio_dev *indio_dev, void *buf,
			    unsigned int len, int64_t timestamp)
{
	int64_t calculated_time;
	struct ssp_sensor_data *spd = iio_priv(indio_dev);

	/* Frames can still arrive after the buffer got disabled */
	if (!smp_load_acquire(&spd->streaming))
		return 0;

	if (WARN_ON_ONCE(len > SSP_MAX_SCAN_SIZE))
		return -EINVAL;

	/*
	 * it always sends full set of samples, remember about available masks
	 */
	memcpy(spd->buffer, buf, len);

	calculated_time = timestamp +
		(int64_t)get_unaligned_le32(buf + len) * MEGA;

	return iio_push_to_buffers_with_ts(indio_dev, spd->buffer,
					   sizeof(spd->buffer), calculated_time);
}
EXPORT_SYMBOL_NS(ssp_common_process_data, "IIO_SSP_SENSORS");

MODULE_AUTHOR("Karol Wrona <k.wrona@samsung.com>");
MODULE_DESCRIPTION("Samsung sensorhub commons");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_SSP_SENSORS");
