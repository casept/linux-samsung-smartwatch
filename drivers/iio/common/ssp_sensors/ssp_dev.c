// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2014, Samsung Electronics Co. Ltd. All Rights Reserved.
 */

#include <linux/iio/iio.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mfd/core.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>
#include <linux/string_choices.h>

#include "ssp.h"

#define SSP_WDT_TIME			10000
#define SSP_LIMIT_RESET_CNT		20
#define SSP_LIMIT_TIMEOUT_CNT		3
#define SSP_REFRESH_RETRIES		5

#define SSP_HRM_SENSORS	(BIT(SSP_BIO_HRM_RAW) | BIT(SSP_BIO_HRM_RAW_FAC) | \
			 BIT(SSP_BIO_HRM_LIB))

/* It is possible that it is max clk rate for version 1.0 of bootcode */
#define SSP_BOOT_SPI_HZ	400000

/*
 * These fields can look enigmatic but this structure is used mainly to flat
 * some values and depends on command type.
 */
struct ssp_instruction {
	__le32 a;
	__le32 b;
	u8 c;
} __attribute__((__packed__));

static const u8 ssp_magnitude_table[] = {110, 85, 171, 71, 203, 195, 0, 67,
	208, 56, 175, 244, 206, 213, 0, 92, 250, 0, 55, 48, 189, 252, 171,
	243, 13, 45, 250};

static const struct ssp_sensorhub_info ssp_rinato_info = {
	.fw_name = "ssp_B2.fw",
	.fw_crashed_name = "ssp_crashed.fw",
	.fw_rev = 14031200,
	.mag_table = ssp_magnitude_table,
	.mag_length = ARRAY_SIZE(ssp_magnitude_table),
};

static const struct ssp_sensorhub_info ssp_thermostat_info = {
	.fw_name = "thermostat_B2.fw",
	.fw_crashed_name = "ssp_crashed.fw",
	.fw_rev = 14080600,
	.mag_table = ssp_magnitude_table,
	.mag_length = ARRAY_SIZE(ssp_magnitude_table),
};

static const struct mfd_cell sensorhub_sensor_devs[] = {
	{
		.name = "ssp-accelerometer",
	},
	{
		.name = "ssp-gyroscope",
	},
	{
		.name = "ssp-hrm",
	},
	{
		.name = "ssp-hrm-raw",
	},
};

static void ssp_toggle_mcu_reset_gpio(struct ssp_data *data)
{
	gpiod_set_value(data->mcu_reset_gpiod, 0);
	usleep_range(1000, 1200);
	gpiod_set_value(data->mcu_reset_gpiod, 1);
	msleep(50);
}

static int ssp_add_sensor(struct ssp_data *data, enum ssp_sensor_type type,
			  u32 delay)
{
	struct ssp_instruction to_send;

	to_send.a = cpu_to_le32(delay);
	to_send.b = cpu_to_le32(data->batch_latency_buf[type]);
	to_send.c = data->batch_opt_buf[type];

	return ssp_send_instruction(data, SSP_MSG2SSP_INST_BYPASS_SENSOR_ADD,
				    type, (u8 *)&to_send, sizeof(to_send));
}

/* Re-adds the sensors that were running before the MCU got reset */
static int ssp_sync_enabled_sensors(struct ssp_data *data)
{
	int i, ret, err = 0;

	lockdep_assert_held(&data->enable_lock);

	for (i = 0; i < SSP_SENSOR_MAX; ++i) {
		if (!(data->sensor_enable & BIT(i)))
			continue;

		ret = ssp_add_sensor(data, i, data->delay_buf[i]);
		if (ret < 0) {
			dev_err(&data->spi->dev, "Sync sensor nr: %d fail\n", i);
			/* The first error tells the most */
			err = err ?: ret;
		}
	}

	ret = ssp_command(data, SSP_MSG2SSP_AP_MCU_SET_DUMPMODE,
			  data->mcu_dump_mode);
	if (ret < 0) {
		dev_err(&data->spi->dev,
			"SSP_MSG2SSP_AP_MCU_SET_DUMPMODE failed\n");
		err = err ?: ret;
	}

	return err;
}

/* Disabling a wakeup source that failed to enable warns */
static void ssp_set_irq_wake(struct ssp_data *data, bool on)
{
	if (on == data->irq_wake)
		return;

	if (on) {
		data->irq_wake = !enable_irq_wake(data->spi->irq);
	} else {
		disable_irq_wake(data->spi->irq);
		data->irq_wake = false;
	}
}

static void ssp_enable_mcu(struct ssp_data *data, bool enable)
{
	dev_info(&data->spi->dev, "current shutdown = %d, old = %d\n", enable,
		 data->shut_down);

	if (enable && data->shut_down) {
		data->shut_down = false;
		enable_irq(data->spi->irq);
		ssp_set_irq_wake(data, true);
	} else if (!enable && !data->shut_down) {
		data->shut_down = true;
		disable_irq(data->spi->irq);
		ssp_set_irq_wake(data, false);
	} else {
		dev_warn(&data->spi->dev, "current shutdown = %d, old = %d\n",
			 enable, data->shut_down);
	}
}

/*
 * This function is the first one which communicates with the mcu so it is
 * possible that the first attempt will fail
 */
static int ssp_check_fwbl(struct ssp_data *data)
{
	int retries = 0;

	while (retries++ < 5) {
		data->cur_firm_rev = ssp_get_firmware_rev(data);
		if (data->cur_firm_rev == SSP_INVALID_REVISION ||
		    data->cur_firm_rev == SSP_INVALID_REVISION2) {
			dev_warn(&data->spi->dev,
				 "Invalid revision, trying %d time\n", retries);
		} else {
			break;
		}
	}

	if (data->cur_firm_rev == SSP_INVALID_REVISION ||
	    data->cur_firm_rev == SSP_INVALID_REVISION2) {
		dev_err(&data->spi->dev, "SSP_INVALID_REVISION\n");
		return SSP_FW_DL_STATE_NEED_TO_SCHEDULE;
	}

	dev_info(&data->spi->dev,
		 "MCU Firm Rev : Old = %8u, New = %8u\n",
		 data->cur_firm_rev,
		 data->sensorhub_info->fw_rev);

	if (data->cur_firm_rev != data->sensorhub_info->fw_rev)
		return SSP_FW_DL_STATE_NEED_TO_SCHEDULE;

	return SSP_FW_DL_STATE_NONE;
}

static void ssp_reset_mcu(struct ssp_data *data)
{
	ssp_enable_mcu(data, false);
	ssp_clean_pending_list(data);
	ssp_toggle_mcu_reset_gpio(data);
	ssp_enable_mcu(data, true);
}

static void ssp_wdt_work_func(struct work_struct *work)
{
	struct ssp_data *data = container_of(work, struct ssp_data, work_wdt);

	dev_err(&data->spi->dev, "%s - Sensor state: 0x%x, RC: %u, CC: %u\n",
		__func__, data->available_sensors, data->reset_cnt,
		data->com_fail_cnt);

	ssp_reset_mcu(data);
	data->com_fail_cnt = 0;
	data->timeout_cnt = 0;
}

static void ssp_wdt_timer_func(struct timer_list *t)
{
	struct ssp_data *data = timer_container_of(data, t, wdt_timer);

	switch (data->fw_dl_state) {
	case SSP_FW_DL_STATE_FAIL:
	case SSP_FW_DL_STATE_DOWNLOADING:
	case SSP_FW_DL_STATE_SYNC:
		goto _mod;
	}

	if (data->timeout_cnt > SSP_LIMIT_TIMEOUT_CNT ||
	    data->com_fail_cnt > SSP_LIMIT_RESET_CNT)
		queue_work(system_power_efficient_wq, &data->work_wdt);
_mod:
	mod_timer(&data->wdt_timer, jiffies + msecs_to_jiffies(SSP_WDT_TIME));
}

static void ssp_enable_wdt_timer(struct ssp_data *data)
{
	mod_timer(&data->wdt_timer, jiffies + msecs_to_jiffies(SSP_WDT_TIME));
}

static void ssp_disable_wdt_timer(struct ssp_data *data)
{
	timer_delete_sync(&data->wdt_timer);
	cancel_work_sync(&data->work_wdt);
}

static int ssp_set_hrm_power(struct ssp_data *data, bool on)
{
	int ret;

	if (on == data->hrm_powered)
		return 0;

	if (on)
		ret = regulator_enable(data->hrm_supply);
	else
		ret = regulator_disable(data->hrm_supply);
	if (ret) {
		dev_err(&data->spi->dev, "Failed to switch HRM supply %s: %d\n",
			str_on_off(on), ret);
		return ret;
	}

	data->hrm_powered = on;

	return 0;
}

/**
 * ssp_get_sensor_delay() - gets sensor data acquisition period
 * @data:	sensorhub structure
 * @type:	SSP sensor type
 *
 * Returns acquisition period in ms
 */
u32 ssp_get_sensor_delay(struct ssp_data *data, enum ssp_sensor_type type)
{
	return data->delay_buf[type];
}
EXPORT_SYMBOL_NS(ssp_get_sensor_delay, "IIO_SSP_SENSORS");

static int __ssp_change_delay(struct ssp_data *data, enum ssp_sensor_type type,
			      u32 delay)
{
	int ret;
	struct ssp_instruction to_send;

	lockdep_assert_held(&data->enable_lock);

	to_send.a = cpu_to_le32(delay);
	to_send.b = cpu_to_le32(data->batch_latency_buf[type]);
	to_send.c = data->batch_opt_buf[type];

	ret = ssp_send_instruction(data, SSP_MSG2SSP_INST_CHANGE_DELAY, type,
				   (u8 *)&to_send, sizeof(to_send));
	if (ret < 0) {
		dev_err(&data->spi->dev, "Changing sensor delay failed\n");
		return ret;
	}

	data->delay_buf[type] = delay;

	return 0;
}

/* Sensors the MCU may run, also those whose removal it didn't confirm */
static unsigned int ssp_running_sensors(struct ssp_data *data)
{
	/*
	 * Also read without enable_lock by the PM callbacks, when userspace
	 * and the freezable work changing the masks are frozen
	 */
	return READ_ONCE(data->sensor_enable) |
	       READ_ONCE(data->sensor_unremoved);
}

/* Keep the HRM supply and the watchdog only while the MCU may need them */
static void ssp_update_running(struct ssp_data *data)
{
	lockdep_assert_held(&data->enable_lock);

	if (!(ssp_running_sensors(data) & SSP_HRM_SENSORS))
		ssp_set_hrm_power(data, false);
	if (!ssp_running_sensors(data))
		ssp_disable_wdt_timer(data);
}

/* Attempts to remove a stale sensor before assuming the MCU did remove it */
#define SSP_STALE_RETRIES	3

/* The MCU got reset, which removed all sensors */
static void ssp_forget_unremoved(struct ssp_data *data)
{
	lockdep_assert_held(&data->enable_lock);

	WRITE_ONCE(data->sensor_unremoved, 0);
	WRITE_ONCE(data->sensor_given_up, 0);
	WRITE_ONCE(data->stale_sensors, 0);
	memset(data->stale_failures, 0, sizeof(data->stale_failures));
}

static void ssp_stale_work_func(struct work_struct *work)
{
	struct ssp_data *data = container_of(work, struct ssp_data,
					     work_stale.work);
	unsigned long stale = xchg(&data->stale_sensors, 0);
	unsigned long failed = 0;
	__le32 command;
	int type;

	guard(mutex)(&data->enable_lock);

	for_each_set_bit(type, &stale, SSP_SENSOR_MAX) {
		/* It got enabled or the MCU reset while the work was pending */
		if ((data->sensor_enable & BIT(type)) ||
		    !((data->sensor_unremoved | data->sensor_given_up) &
		      BIT(type)))
			continue;

		/* Counted removed, but it sends data after all */
		WRITE_ONCE(data->sensor_unremoved,
			   data->sensor_unremoved | BIT(type));
		WRITE_ONCE(data->sensor_given_up,
			   data->sensor_given_up & ~BIT(type));

		command = cpu_to_le32(data->delay_buf[type]);
		if (ssp_send_instruction(data, SSP_MSG2SSP_INST_BYPASS_SENSOR_RM,
					 type, (u8 *)&command,
					 sizeof(command)) < 0) {
			dev_err_ratelimited(&data->spi->dev,
					    "Removing stale sensor %d failed\n",
					    type);
			failed |= BIT(type);
			continue;
		}

		data->stale_failures[type] = 0;
		WRITE_ONCE(data->sensor_unremoved,
			   data->sensor_unremoved & ~BIT(type));
	}

	/*
	 * The MCU may have removed it and only the answer got lost, so no
	 * frame would ask again. Retry for a while, then count it removed,
	 * frames ask again if it isn't.
	 */
	for_each_set_bit(type, &failed, SSP_SENSOR_MAX) {
		if (++data->stale_failures[type] < SSP_STALE_RETRIES) {
			set_bit(type, &data->stale_sensors);
			queue_delayed_work(system_freezable_power_efficient_wq,
					   &data->work_stale,
					   msecs_to_jiffies(SSP_SW_RESET_TIME));
			continue;
		}

		dev_err(&data->spi->dev,
			"Giving up removing stale sensor %d\n", type);
		data->stale_failures[type] = 0;
		WRITE_ONCE(data->sensor_unremoved,
			   data->sensor_unremoved & ~BIT(type));
		WRITE_ONCE(data->sensor_given_up,
			   data->sensor_given_up | BIT(type));
	}

	ssp_update_running(data);
}

/**
 * ssp_remove_stale_sensor() - removes a sensor the MCU runs on its own
 * @data:	sensorhub structure
 * @type:	SSP sensor type
 *
 * The MCU keeps sending data for a sensor whose removal it did not get. The
 * removal is retried a few times, and again whenever more data arrives.
 */
void ssp_remove_stale_sensor(struct ssp_data *data,
			     enum ssp_sensor_type type)
{
	set_bit(type, &data->stale_sensors);
	queue_delayed_work(system_freezable_power_efficient_wq,
			   &data->work_stale, 0);
}

/**
 * ssp_enable_sensor() - enables data acquisition for sensor
 * @data:	sensorhub structure
 * @type:	SSP sensor type
 * @delay:	delay in ms
 *
 * Returns 0 or negative value in case of error
 */
int ssp_enable_sensor(struct ssp_data *data, enum ssp_sensor_type type,
		      u32 delay)
{
	int ret;

	guard(mutex)(&data->enable_lock);

	if (data->sensor_enable & BIT(type))
		return __ssp_change_delay(data, type, delay);

	if (BIT(type) & SSP_HRM_SENSORS) {
		ret = ssp_set_hrm_power(data, true);
		if (ret)
			return ret;
	}

	ret = ssp_add_sensor(data, type, delay);
	if (ret < 0) {
		dev_err(&data->spi->dev, "Enabling sensor failed\n");
		ssp_update_running(data);
		return ret;
	}

	data->delay_buf[type] = delay;

	if (!ssp_running_sensors(data))
		ssp_enable_wdt_timer(data);
	WRITE_ONCE(data->sensor_enable, data->sensor_enable | BIT(type));
	WRITE_ONCE(data->sensor_unremoved, data->sensor_unremoved & ~BIT(type));
	WRITE_ONCE(data->sensor_given_up, data->sensor_given_up & ~BIT(type));
	data->stale_failures[type] = 0;

	return 0;
}
EXPORT_SYMBOL_NS(ssp_enable_sensor, "IIO_SSP_SENSORS");

/**
 * ssp_change_delay() - changes data acquisition for sensor
 * @data:	sensorhub structure
 * @type:	SSP sensor type
 * @delay:	delay in ms
 *
 * Returns 0 or negative value in case of error
 */
int ssp_change_delay(struct ssp_data *data, enum ssp_sensor_type type,
		     u32 delay)
{
	guard(mutex)(&data->enable_lock);

	/* A stopped sensor gets the delay when it is enabled */
	if (!(data->sensor_enable & BIT(type))) {
		data->delay_buf[type] = delay;
		return 0;
	}

	return __ssp_change_delay(data, type, delay);
}
EXPORT_SYMBOL_NS(ssp_change_delay, "IIO_SSP_SENSORS");

/**
 * ssp_disable_sensor() - disables sensor
 *
 * @data:	sensorhub structure
 * @type:	SSP sensor type
 *
 * The sensor counts as disabled even if the MCU could not be told, but its
 * supply and the watchdog stay until the MCU confirms or gets reset.
 *
 * Returns 0 or negative value in case of error
 */
int ssp_disable_sensor(struct ssp_data *data, enum ssp_sensor_type type)
{
	int ret;
	__le32 command;

	guard(mutex)(&data->enable_lock);

	if (!(data->sensor_enable & BIT(type)))
		return 0;

	command = cpu_to_le32(data->delay_buf[type]);

	ret = ssp_send_instruction(data, SSP_MSG2SSP_INST_BYPASS_SENSOR_RM,
				   type, (u8 *)&command, sizeof(command));
	if (ret < 0) {
		dev_err(&data->spi->dev, "Remove sensor fail\n");
		WRITE_ONCE(data->sensor_unremoved,
			   data->sensor_unremoved | BIT(type));
		ssp_remove_stale_sensor(data, type);
	}

	WRITE_ONCE(data->sensor_enable, data->sensor_enable & ~BIT(type));
	ssp_update_running(data);

	return ret;
}
EXPORT_SYMBOL_NS(ssp_disable_sensor, "IIO_SSP_SENSORS");

static irqreturn_t ssp_irq_thread_fn(int irq, void *dev_id)
{
	struct ssp_data *data = dev_id;

	/*
	 * This wrapper is done to preserve error path for ssp_irq_msg, also
	 * it is defined in different file.
	 */
	ssp_irq_msg(data);

	return IRQ_HANDLED;
}

static int ssp_initialize_mcu(struct ssp_data *data)
{
	int ret;

	ssp_clean_pending_list(data);

	ret = ssp_get_chipid(data);
	if (ret != SSP_DEVICE_ID) {
		dev_err(&data->spi->dev, "%s - MCU %s ret = %d\n", __func__,
			ret < 0 ? "is not working" : "identification failed",
			ret);
		return ret < 0 ? ret : -ENODEV;
	}

	dev_info(&data->spi->dev, "MCU device ID = %d\n", ret);

	/*
	 * needs clarification, for now do not want to export all transfer
	 * methods to sensors' drivers
	 */
	ret = ssp_set_magnetic_matrix(data);
	if (ret < 0) {
		dev_err(&data->spi->dev,
			"%s - ssp_set_magnetic_matrix failed\n", __func__);
		return ret;
	}

	data->available_sensors = ssp_get_sensor_scanning_info(data);
	if (data->available_sensors == 0) {
		dev_err(&data->spi->dev,
			"%s - ssp_get_sensor_scanning_info failed\n", __func__);
		return -EIO;
	}

	data->cur_firm_rev = ssp_get_firmware_rev(data);
	dev_info(&data->spi->dev, "MCU Firm Rev : New = %8u\n",
		 data->cur_firm_rev);

	return ssp_command(data, SSP_MSG2SSP_AP_MCU_DUMP_CHECK, 0);
}

/*
 * sensorhub can request its reinitialization as some brutal and rare error
 * handling. It can be requested from the MCU.
 */
static void ssp_refresh_task(struct work_struct *work)
{
	struct ssp_data *data = container_of((struct delayed_work *)work,
					     struct ssp_data, work_refresh);
	int ret;

	dev_info(&data->spi->dev, "refreshing\n");

	guard(mutex)(&data->enable_lock);

	/* Each reset of the MCU gets the full retries */
	if (atomic_xchg(&data->refresh_requested, 0)) {
		data->reset_cnt++;
		data->refresh_failures = 0;
		ssp_forget_unremoved(data);
	}

	ret = ssp_initialize_mcu(data);
	if (ret >= 0)
		ret = ssp_sync_enabled_sensors(data);
	if (ret < 0) {
		/* For example when the MCU got reset again meanwhile */
		if (++data->refresh_failures > SSP_REFRESH_RETRIES) {
			dev_err(&data->spi->dev,
				"Giving up initializing the MCU: %d\n", ret);
			/*
			 * Drop the supply and watchdog for sensors whose removal
			 * the reset confirmed, enabled ones keep them
			 */
			ssp_update_running(data);
			return;
		}

		dev_warn(&data->spi->dev,
			 "Initializing the MCU failed: %d, retrying\n", ret);
		/* Not delaying a refresh requested meanwhile */
		queue_delayed_work(system_freezable_power_efficient_wq,
				   &data->work_refresh,
				   msecs_to_jiffies(SSP_SW_RESET_TIME));
		return;
	}

	ssp_update_running(data);
	if (data->last_ap_state != 0)
		ssp_command(data, data->last_ap_state, 0);

	if (data->last_resume_state != 0)
		ssp_command(data, data->last_resume_state, 0);

	data->timeout_cnt = 0;
	data->com_fail_cnt = 0;
}

int ssp_queue_ssp_refresh_task(struct ssp_data *data, unsigned int delay)
{
	/*
	 * Called from the interrupt thread, which a running refresh task
	 * needs to complete its transfers, so don't wait for it. If it is
	 * running, it runs once more afterwards. Not while suspended.
	 */
	atomic_set(&data->refresh_requested, 1);

	return mod_delayed_work(system_freezable_power_efficient_wq,
				&data->work_refresh, msecs_to_jiffies(delay));
}

static const struct of_device_id ssp_of_match[] = {
	{
		.compatible	= "samsung,sensorhub-rinato",
		.data		= &ssp_rinato_info,
	}, {
		.compatible	= "samsung,sensorhub-thermostat",
		.data		= &ssp_thermostat_info,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, ssp_of_match);

static struct ssp_data *ssp_parse_dt(struct device *dev)
{
	struct ssp_data *data;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return NULL;

	data->mcu_ap_gpiod = devm_gpiod_get(dev, "mcu-ap", GPIOD_IN);
	if (IS_ERR(data->mcu_ap_gpiod))
		return NULL;

	data->ap_mcu_gpiod = devm_gpiod_get(dev, "ap-mcu", GPIOD_OUT_HIGH);
	if (IS_ERR(data->ap_mcu_gpiod))
		return NULL;

	data->mcu_reset_gpiod = devm_gpiod_get(dev, "mcu-reset",
					       GPIOD_OUT_HIGH);
	if (IS_ERR(data->mcu_reset_gpiod))
		return NULL;

	data->sensorhub_info = device_get_match_data(dev);

	dev_set_drvdata(dev, data);

	return data;
}

/**
 * ssp_sync_consumers() - waits for the data frame being parsed, if any
 * @data:	sensorhub structure
 *
 * Consumer state changed before the call is seen by all later data frames.
 */
void ssp_sync_consumers(struct ssp_data *data)
{
	synchronize_irq(data->spi->irq);
}
EXPORT_SYMBOL_NS(ssp_sync_consumers, "IIO_SSP_SENSORS");

static void ssp_unregister_consumer(void *arg)
{
	struct iio_dev *indio_dev = arg;
	struct ssp_sensor_data *spd = iio_priv(indio_dev);
	struct ssp_data *data = dev_get_drvdata(indio_dev->dev.parent->parent);

	WRITE_ONCE(data->sensor_devs[spd->type], NULL);
	ssp_sync_consumers(data);
}

/**
 * ssp_register_consumer() - registers iio consumer in ssp framework
 *
 * @indio_dev:	consumer iio device
 * @type:	ssp sensor type
 *
 * The consumer is unregistered automatically when its device is unbound.
 *
 * Return: 0 or negative error code
 */
int ssp_register_consumer(struct iio_dev *indio_dev, enum ssp_sensor_type type)
{
	struct ssp_data *data = dev_get_drvdata(indio_dev->dev.parent->parent);

	/* The interrupt thread may use it right away */
	smp_store_release(&data->sensor_devs[type], indio_dev);

	return devm_add_action_or_reset(indio_dev->dev.parent,
					ssp_unregister_consumer, indio_dev);
}
EXPORT_SYMBOL_NS(ssp_register_consumer, "IIO_SSP_SENSORS");

static int ssp_probe_mcu(struct ssp_data *data)
{
	/* The MCU announces its reset, keep the refresh task from interfering */
	guard(mutex)(&data->enable_lock);

	data->fw_dl_state = ssp_check_fwbl(data);
	if (data->fw_dl_state != SSP_FW_DL_STATE_NONE) {
		dev_err(&data->spi->dev, "Firmware version not supported! Continuing, but things may not work!\n");
		return 0;
	}

	return ssp_initialize_mcu(data);
}

static int ssp_probe(struct spi_device *spi)
{
	int ret, i;
	struct ssp_data *data;

	data = ssp_parse_dt(&spi->dev);
	if (!data) {
		dev_err(&spi->dev, "Failed to find platform data\n");
		return -ENODEV;
	}

	/* The MCU firmware hangs during boot without this supply */
	ret = devm_regulator_get_enable(&spi->dev, "vdd");
	if (ret)
		return dev_err_probe(&spi->dev, ret, "Failed to enable vdd\n");

	data->hrm_supply = devm_regulator_get(&spi->dev, "hrm");
	if (IS_ERR(data->hrm_supply))
		return dev_err_probe(&spi->dev, PTR_ERR(data->hrm_supply),
				     "Failed to get HRM supply\n");

	spi->mode = SPI_MODE_1;
	ret = spi_setup(spi);
	if (ret < 0) {
		dev_err(&spi->dev, "Failed to setup spi\n");
		return ret;
	}

	data->fw_dl_state = SSP_FW_DL_STATE_NONE;
	data->spi = spi;
	spi_set_drvdata(spi, data);

	ssp_debugfs_init(data);

	mutex_init(&data->comm_lock);
	mutex_init(&data->enable_lock);

	for (i = 0; i < SSP_SENSOR_MAX; ++i) {
		data->delay_buf[i] = SSP_DEFAULT_POLLING_DELAY;
		data->batch_latency_buf[i] = 0;
		data->batch_opt_buf[i] = 0;
	}

	data->delay_buf[SSP_BIO_HRM_LIB] = 100;

	data->time_syncing = true;

	mutex_init(&data->pending_lock);
	INIT_LIST_HEAD(&data->pending_list);

	INIT_WORK(&data->work_wdt, ssp_wdt_work_func);
	INIT_DELAYED_WORK(&data->work_stale, ssp_stale_work_func);
	INIT_DELAYED_WORK(&data->work_refresh, ssp_refresh_task);

	timer_setup(&data->wdt_timer, ssp_wdt_timer_func, 0);

	ret = request_threaded_irq(data->spi->irq, NULL,
				   ssp_irq_thread_fn,
				   IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
				   "SSP_Int", data);
	if (ret < 0) {
		dev_err(&spi->dev, "Irq request fail\n");
		goto err_setup_irq;
	}

	/* Let's start with enabled one so irq balance could be ok */
	data->shut_down = false;

	/* Restart the MCU in case it got stuck before vdd was enabled */
	ssp_toggle_mcu_reset_gpio(data);

	ssp_set_irq_wake(data, true);

	ret = ssp_probe_mcu(data);
	if (ret < 0) {
		dev_err(&spi->dev, "Initialize_mcu failed\n");
		goto err_read_reg;
	}

	ret = mfd_add_devices(&spi->dev, PLATFORM_DEVID_NONE,
			      sensorhub_sensor_devs,
			      ARRAY_SIZE(sensorhub_sensor_devs), NULL, 0, NULL);
	if (ret < 0) {
		dev_err(&spi->dev, "mfd add devices fail\n");
		goto err_read_reg;
	}

	return 0;

err_read_reg:
	ssp_set_irq_wake(data, false);
	free_irq(data->spi->irq, data);
	cancel_delayed_work_sync(&data->work_refresh);
	cancel_delayed_work_sync(&data->work_stale);
err_setup_irq:
	ssp_debugfs_remove(data);
	mutex_destroy(&data->pending_lock);
	mutex_destroy(&data->enable_lock);
	mutex_destroy(&data->comm_lock);

	dev_err(&spi->dev, "Probe failed!\n");

	return ret;
}

static void ssp_remove(struct spi_device *spi)
{
	struct ssp_data *data = spi_get_drvdata(spi);

	mfd_remove_devices(&spi->dev);

	if (ssp_command(data, SSP_MSG2SSP_AP_STATUS_SHUTDOWN, 0) < 0)
		dev_err(&data->spi->dev,
			"SSP_MSG2SSP_AP_STATUS_SHUTDOWN failed\n");

	ssp_enable_mcu(data, false);
	ssp_disable_wdt_timer(data);

	ssp_clean_pending_list(data);

	free_irq(data->spi->irq, data);
	cancel_delayed_work_sync(&data->work_refresh);
	cancel_delayed_work_sync(&data->work_stale);
	ssp_debugfs_remove(data);

	timer_delete_sync(&data->wdt_timer);
	cancel_work_sync(&data->work_wdt);

	ssp_set_hrm_power(data, false);

	mutex_destroy(&data->comm_lock);
	mutex_destroy(&data->enable_lock);
	mutex_destroy(&data->pending_lock);
}

static int ssp_suspend(struct device *dev)
{
	int ret;
	struct ssp_data *data = spi_get_drvdata(to_spi_device(dev));

	data->last_resume_state = SSP_MSG2SSP_AP_STATUS_SUSPEND;

	ssp_disable_wdt_timer(data);

	ret = ssp_command(data, SSP_MSG2SSP_AP_STATUS_SUSPEND, 0);
	if (ret < 0) {
		dev_err(&data->spi->dev,
			"%s SSP_MSG2SSP_AP_STATUS_SUSPEND failed\n", __func__);

		if (ssp_running_sensors(data))
			ssp_enable_wdt_timer(data);
		return ret;
	}

	data->time_syncing = false;
	disable_irq(data->spi->irq);

	return 0;
}

static int ssp_resume(struct device *dev)
{
	int ret;
	struct ssp_data *data = spi_get_drvdata(to_spi_device(dev));

	enable_irq(data->spi->irq);

	if (ssp_running_sensors(data))
		ssp_enable_wdt_timer(data);

	ret = ssp_command(data, SSP_MSG2SSP_AP_STATUS_RESUME, 0);
	if (ret < 0) {
		dev_err(&data->spi->dev,
			"%s SSP_MSG2SSP_AP_STATUS_RESUME failed\n", __func__);
		/* The watchdog stays armed to recover a hung MCU */
		return ret;
	}

	/* timesyncing is set by MCU */
	data->last_resume_state = SSP_MSG2SSP_AP_STATUS_RESUME;

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(ssp_pm_ops, ssp_suspend, ssp_resume);

static struct spi_driver ssp_driver = {
	.probe = ssp_probe,
	.remove = ssp_remove,
	.driver = {
		.pm = pm_sleep_ptr(&ssp_pm_ops),
		.of_match_table = ssp_of_match,
		.name = "sensorhub"
	},
};

module_spi_driver(ssp_driver);

MODULE_DESCRIPTION("ssp sensorhub driver");
MODULE_AUTHOR("Samsung Electronics");
MODULE_LICENSE("GPL");
