// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Activity tracker running as a library on the Samsung sensorhub
 *
 * The MCU classifies the user's activity from the accelerometer and its
 * pedometer, and reports whenever the activity changes.
 */

#include <linux/bitmap.h>
#include <linux/iio/common/ssp_sensors.h>
#include <linux/iio/events.h>
#include <linux/iio/iio.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/time.h>
#include <linux/timekeeping.h>

#define SSP_ACTIVITY_NAME		"ssp-activity"
#define SSP_LIBRARY_ACTIVITY		0x1a

/*
 * Requests are added and removed with their type and a mask of the
 * activities they are about, the mask with all bits set means all of them.
 */
#define SSP_ACTIVITY_REQ_NOTIFY		1
#define SSP_ACTIVITY_ALL		0x20

/* Flags, time of day in ms, activity and how sure the MCU is about it */
#define SSP_ACTIVITY_RECORD_LEN		7
#define SSP_ACTIVITY_RECORD_STATUS	5
#define SSP_ACTIVITY_RECORD_ACCURACY	6

#define SSP_ACTIVITY_UNKNOWN		0
#define SSP_ACTIVITY_STILL		1
#define SSP_ACTIVITY_WALK		2
#define SSP_ACTIVITY_RUN		3
#define SSP_ACTIVITY_VEHICLE		4

/* Low, medium and high */
#define SSP_ACTIVITY_ACCURACY_MAX	2

static const enum iio_modifier ssp_activity_mods[] = {
	[SSP_ACTIVITY_STILL] = IIO_MOD_STILL,
	[SSP_ACTIVITY_WALK] = IIO_MOD_WALKING,
	[SSP_ACTIVITY_RUN] = IIO_MOD_RUNNING,
};

#define SSP_ACTIVITY_EVENTS		(ARRAY_SIZE(ssp_activity_mods) * 2)

struct ssp_activity {
	struct ssp_library lib;
	struct iio_dev *indio_dev;
	/* Serializes starting, stopping and configuring the library */
	struct mutex lock;
	bool tracker_en;
	/* Rising and falling event per activity, by status */
	DECLARE_BITMAP(event_en, SSP_ACTIVITY_EVENTS);

	/* Status and accuracy of the last report, updated together */
	u16 report;
};

#define SSP_ACTIVITY_REPORT(status, accuracy)	((status) | (accuracy) << 8)
#define SSP_ACTIVITY_REPORT_STATUS(report)	((report) & 0xff)
#define SSP_ACTIVITY_REPORT_ACCURACY(report)	((report) >> 8)

static int ssp_activity_status(enum iio_modifier mod)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ssp_activity_mods); i++)
		if (ssp_activity_mods[i] == mod && i != SSP_ACTIVITY_UNKNOWN)
			return i;

	return -EINVAL;
}

static unsigned int ssp_activity_event_bit(u8 status,
					   enum iio_event_direction dir)
{
	return status * 2 + (dir == IIO_EV_DIR_FALLING);
}

static void ssp_activity_push_event(struct ssp_activity *act, u8 status,
				    enum iio_event_direction dir, s64 ts)
{
	if (status >= ARRAY_SIZE(ssp_activity_mods) ||
	    status == SSP_ACTIVITY_UNKNOWN ||
	    !test_bit(ssp_activity_event_bit(status, dir), act->event_en))
		return;

	iio_push_event(act->indio_dev,
		       IIO_MOD_EVENT_CODE(IIO_ACTIVITY, 0,
					  ssp_activity_mods[status],
					  IIO_EV_TYPE_THRESH, dir),
		       ts);
}

static int ssp_activity_process_data(struct ssp_library *lib, const u8 *buf,
				     unsigned int len)
{
	struct ssp_activity *act = container_of(lib, struct ssp_activity, lib);
	u8 status, old = SSP_ACTIVITY_REPORT_STATUS(READ_ONCE(act->report));
	s64 ts;

	if (len < SSP_ACTIVITY_RECORD_LEN)
		return -EPROTO;

	status = buf[SSP_ACTIVITY_RECORD_STATUS];
	WRITE_ONCE(act->report,
		   SSP_ACTIVITY_REPORT(status,
				       min(buf[SSP_ACTIVITY_RECORD_ACCURACY],
					   SSP_ACTIVITY_ACCURACY_MAX)));

	if (status != old) {
		ts = iio_get_time_ns(act->indio_dev);
		ssp_activity_push_event(act, old, IIO_EV_DIR_FALLING, ts);
		ssp_activity_push_event(act, status, IIO_EV_DIR_RISING, ts);
	}

	return SSP_ACTIVITY_RECORD_LEN;
}

/* The MCU forgot the activity in its reset, and reports again on a change */
static void ssp_activity_readded(struct ssp_library *lib)
{
	struct ssp_activity *act = container_of(lib, struct ssp_activity, lib);
	u8 old = SSP_ACTIVITY_REPORT_STATUS(READ_ONCE(act->report));

	WRITE_ONCE(act->report, SSP_ACTIVITY_UNKNOWN);
	ssp_activity_push_event(act, old, IIO_EV_DIR_FALLING,
				iio_get_time_ns(act->indio_dev));
}

static int ssp_activity_start(struct ssp_activity *act)
{
	struct tm tm;

	time64_to_tm(ktime_get_real_seconds(), 0, &tm);

	/*
	 * Type, activities, reserved, then the time of day for the MCU clock,
	 * which only timestamps the records
	 */
	const u8 args[] = {
		SSP_ACTIVITY_REQ_NOTIFY, SSP_ACTIVITY_ALL, 0, 0, 0,
		tm.tm_hour, tm.tm_min, tm.tm_sec,
	};

	return ssp_library_add(&act->lib, args, sizeof(args));
}

/* The library runs while the tracker or any of its events is enabled */
static int ssp_activity_set_enable(struct ssp_activity *act, bool tracker_en,
				   unsigned int event, bool event_en)
{
	DECLARE_BITMAP(events, SSP_ACTIVITY_EVENTS);
	bool running, run;
	int ret = 0;

	lockdep_assert_held(&act->lock);

	running = act->tracker_en || !bitmap_empty(act->event_en,
						   SSP_ACTIVITY_EVENTS);

	bitmap_copy(events, act->event_en, SSP_ACTIVITY_EVENTS);
	if (event < SSP_ACTIVITY_EVENTS)
		__assign_bit(event, events, event_en);
	run = tracker_en || !bitmap_empty(events, SSP_ACTIVITY_EVENTS);

	if (run && !running) {
		/* Nothing is known until the MCU reports */
		WRITE_ONCE(act->report, SSP_ACTIVITY_UNKNOWN);
		ret = ssp_activity_start(act);
		if (ret < 0)
			return ret;
	} else if (!run && running) {
		/* It counts as removed even if the MCU could not be told */
		ret = ssp_library_remove(&act->lib);
	}

	act->tracker_en = tracker_en;
	if (event < SSP_ACTIVITY_EVENTS) {
		assign_bit(event, act->event_en, event_en);
		/* No event once disabling it returned */
		if (!event_en)
			ssp_sync_consumers(act->lib.data);
	}

	return ret;
}

static int ssp_activity_read_raw(struct iio_dev *indio_dev,
				 struct iio_chan_spec const *chan, int *val,
				 int *val2, long mask)
{
	struct ssp_activity *act = iio_priv(indio_dev);
	u16 report;
	u8 height_cm;

	switch (mask) {
	case IIO_CHAN_INFO_PROCESSED:
		/* Confidence in percent */
		report = READ_ONCE(act->report);
		if (ssp_activity_status(chan->channel2) !=
		    SSP_ACTIVITY_REPORT_STATUS(report))
			*val = 0;
		else
			*val = 100 * (SSP_ACTIVITY_REPORT_ACCURACY(report) + 1) /
			       (SSP_ACTIVITY_ACCURACY_MAX + 1);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_ENABLE: {
		guard(mutex)(&act->lock);
		*val = act->tracker_en;
		return IIO_VAL_INT;
	}
	case IIO_CHAN_INFO_CALIBHEIGHT:
		/* cm to m */
		height_cm = ssp_library_get_profile(&act->lib,
						    SSP_PROFILE_HEIGHT);
		*val = height_cm / 100;
		*val2 = height_cm % 100 * 10000;
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static int ssp_activity_write_raw(struct iio_dev *indio_dev,
				  struct iio_chan_spec const *chan, int val,
				  int val2, long mask)
{
	struct ssp_activity *act = iio_priv(indio_dev);
	int height_cm;

	switch (mask) {
	case IIO_CHAN_INFO_ENABLE: {
		guard(mutex)(&act->lock);
		return ssp_activity_set_enable(act, val, SSP_ACTIVITY_EVENTS,
					       false);
	}
	case IIO_CHAN_INFO_CALIBHEIGHT:
		/* m to cm, the MCU takes 1 byte */
		if (val < 0 || val > U8_MAX / 100 || val2 < 0)
			return -EINVAL;

		height_cm = val * 100 + val2 / 10000;
		if (height_cm < 1 || height_cm > U8_MAX)
			return -EINVAL;

		return ssp_library_set_profile(&act->lib, SSP_PROFILE_HEIGHT,
					       height_cm);
	default:
		return -EINVAL;
	}
}

static int ssp_activity_read_event_config(struct iio_dev *indio_dev,
					  const struct iio_chan_spec *chan,
					  enum iio_event_type type,
					  enum iio_event_direction dir)
{
	struct ssp_activity *act = iio_priv(indio_dev);
	int status = ssp_activity_status(chan->channel2);

	if (status < 0)
		return status;

	guard(mutex)(&act->lock);

	return test_bit(ssp_activity_event_bit(status, dir), act->event_en);
}

static int ssp_activity_write_event_config(struct iio_dev *indio_dev,
					   const struct iio_chan_spec *chan,
					   enum iio_event_type type,
					   enum iio_event_direction dir,
					   bool state)
{
	struct ssp_activity *act = iio_priv(indio_dev);
	int status = ssp_activity_status(chan->channel2);

	if (status < 0)
		return status;

	guard(mutex)(&act->lock);

	return ssp_activity_set_enable(act, act->tracker_en,
				       ssp_activity_event_bit(status, dir),
				       state);
}

static const struct iio_info ssp_activity_info = {
	.read_raw = ssp_activity_read_raw,
	.write_raw = ssp_activity_write_raw,
	.read_event_config = ssp_activity_read_event_config,
	.write_event_config = ssp_activity_write_event_config,
};

static const char * const ssp_activity_genders[] = { "male", "female" };

static int ssp_activity_get_gender(struct iio_dev *indio_dev,
				   const struct iio_chan_spec *chan)
{
	struct ssp_activity *act = iio_priv(indio_dev);

	return ssp_library_get_profile(&act->lib, SSP_PROFILE_GENDER) ==
	       SSP_PROFILE_FEMALE;
}

static int ssp_activity_set_gender(struct iio_dev *indio_dev,
				   const struct iio_chan_spec *chan,
				   unsigned int mode)
{
	struct ssp_activity *act = iio_priv(indio_dev);

	return ssp_library_set_profile(&act->lib, SSP_PROFILE_GENDER,
				       mode ? SSP_PROFILE_FEMALE :
					      SSP_PROFILE_MALE);
}

static const struct iio_enum ssp_activity_gender_enum = {
	.items = ssp_activity_genders,
	.num_items = ARRAY_SIZE(ssp_activity_genders),
	.get = ssp_activity_get_gender,
	.set = ssp_activity_set_gender,
};

/* The profile is shared with the pedometer, which the tracker builds on */
static const struct iio_chan_spec_ext_info ssp_activity_ext_info[] = {
	IIO_ENUM("calibgender", IIO_SHARED_BY_TYPE, &ssp_activity_gender_enum),
	IIO_ENUM_AVAILABLE("calibgender", IIO_SHARED_BY_TYPE,
			   &ssp_activity_gender_enum),
	{ }
};

static const struct iio_event_spec ssp_activity_events[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_RISING,
		.mask_separate = BIT(IIO_EV_INFO_ENABLE),
	},
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_FALLING,
		.mask_separate = BIT(IIO_EV_INFO_ENABLE),
	},
};

#define SSP_ACTIVITY_CHANNEL(_mod) {					\
	.type = IIO_ACTIVITY,						\
	.modified = 1,							\
	.channel2 = _mod,						\
	.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_ENABLE) |		\
				    BIT(IIO_CHAN_INFO_CALIBHEIGHT),	\
	.scan_index = -1,						\
	.ext_info = ssp_activity_ext_info,				\
	.event_spec = ssp_activity_events,				\
	.num_event_specs = ARRAY_SIZE(ssp_activity_events),		\
}

static const struct iio_chan_spec ssp_activity_channels[] = {
	SSP_ACTIVITY_CHANNEL(IIO_MOD_STILL),
	SSP_ACTIVITY_CHANNEL(IIO_MOD_WALKING),
	SSP_ACTIVITY_CHANNEL(IIO_MOD_RUNNING),
};

static int ssp_activity_probe(struct platform_device *pdev)
{
	struct ssp_activity *act;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*act));
	if (!indio_dev)
		return -ENOMEM;

	act = iio_priv(indio_dev);
	act->indio_dev = indio_dev;
	act->lib.id = SSP_LIBRARY_ACTIVITY;
	act->lib.process_data = ssp_activity_process_data;
	act->lib.readded = ssp_activity_readded;
	/* Zeroes would leave the notifications running on the MCU */
	act->lib.remove_buf[0] = SSP_ACTIVITY_REQ_NOTIFY;
	act->lib.remove_buf[1] = SSP_ACTIVITY_ALL;

	ret = devm_mutex_init(&pdev->dev, &act->lock);
	if (ret)
		return ret;

	indio_dev->name = SSP_ACTIVITY_NAME;
	indio_dev->info = &ssp_activity_info;
	indio_dev->channels = ssp_activity_channels;
	indio_dev->num_channels = ARRAY_SIZE(ssp_activity_channels);

	ssp_library_init(&pdev->dev, &act->lib);

	ret = devm_iio_device_register(&pdev->dev, indio_dev);
	if (ret)
		return ret;

	/* Stops, and with it the events, before the IIO device goes away */
	return ssp_register_library(&pdev->dev, &act->lib);
}

static const struct platform_device_id ssp_activity_id_table[] = {
	{ SSP_ACTIVITY_NAME },
	{ }
};
MODULE_DEVICE_TABLE(platform, ssp_activity_id_table);

static struct platform_driver ssp_activity_driver = {
	.driver = {
		.name = SSP_ACTIVITY_NAME,
	},
	.probe = ssp_activity_probe,
	.id_table = ssp_activity_id_table,
};

module_platform_driver(ssp_activity_driver);

MODULE_AUTHOR("Davids Paskevics <davids.paskevics@gmail.com>");
MODULE_DESCRIPTION("Samsung sensorhub activity tracker driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_SSP_SENSORS");
