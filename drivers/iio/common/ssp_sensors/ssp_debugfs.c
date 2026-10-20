// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Log of the messages the sensorhub MCU sends for debugging, readable from
 * debugfs instead of filling the kernel log.
 */

#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/math64.h>
#include <linux/slab.h>
#include <linux/timekeeping.h>
#include <linux/uaccess.h>

#include "ssp.h"

/**
 * ssp_log_mcu_message() - appends a debug message of the MCU to its log
 * @data:	sensorhub structure
 * @msg:	message, NUL-terminated or not
 * @len:	length of @msg
 *
 * The oldest lines are dropped when the log is full.
 */
void ssp_log_mcu_message(struct ssp_data *data, const char *msg,
			 unsigned int len)
{
	char stamp[24];
	u32 nsec;
	u64 sec = div_u64_rem(ktime_get_ns(), NSEC_PER_SEC, &nsec);
	unsigned int stamp_len, need;
	bool newline;
	char c;

	if (!IS_ENABLED(CONFIG_DEBUG_FS))
		return;

	/* The MCU counts the terminating NUL in */
	len = strnlen(msg, len);

	stamp_len = scnprintf(stamp, sizeof(stamp), "[%5llu.%06u] ", sec,
			      nsec / 1000);
	newline = !len || msg[len - 1] != '\n';
	need = stamp_len + len + newline;
	if (need > kfifo_size(&data->mcu_log))
		return;

	scoped_guard(mutex, &data->mcu_log_lock) {
		while (kfifo_avail(&data->mcu_log) < need)
			while (kfifo_get(&data->mcu_log, &c) && c != '\n')
				;

		kfifo_in(&data->mcu_log, stamp, stamp_len);
		kfifo_in(&data->mcu_log, msg, len);
		if (newline)
			kfifo_put(&data->mcu_log, '\n');
	}

	wake_up_interruptible(&data->mcu_log_wq);
}

static bool ssp_mcu_log_readable(struct ssp_data *data)
{
	return !kfifo_is_empty(&data->mcu_log) || READ_ONCE(data->mcu_log_closed);
}

static int ssp_mcu_log_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;

	return stream_open(inode, file);
}

static ssize_t ssp_mcu_log_read(struct file *file, char __user *ubuf,
				size_t count, loff_t *ppos)
{
	struct ssp_data *data = file->private_data;
	unsigned int copied;
	int ret;

	/* Nothing would ever be copied, while the log looks readable */
	if (!count)
		return 0;

	count = min(count, PAGE_SIZE);
	char *buf __free(kfree) = kmalloc(count, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	for (;;) {
		if (!(file->f_flags & O_NONBLOCK)) {
			ret = wait_event_interruptible(data->mcu_log_wq,
						       ssp_mcu_log_readable(data));
			if (ret)
				return ret;
		}

		/* Not copying to userspace here, it can sleep for long */
		scoped_guard(mutex, &data->mcu_log_lock)
			copied = kfifo_out(&data->mcu_log, buf, count);

		if (copied || READ_ONCE(data->mcu_log_closed))
			break;
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		/* Another reader was faster */
	}

	if (copy_to_user(ubuf, buf, copied))
		return -EFAULT;

	return copied;
}

static const struct file_operations ssp_mcu_log_fops = {
	.owner = THIS_MODULE,
	.open = ssp_mcu_log_open,
	.read = ssp_mcu_log_read,
};

void ssp_debugfs_init(struct ssp_data *data)
{
	char name[32];

	INIT_KFIFO(data->mcu_log);
	mutex_init(&data->mcu_log_lock);
	init_waitqueue_head(&data->mcu_log_wq);
	data->mcu_log_closed = false;

	snprintf(name, sizeof(name), "sensorhub-%s", dev_name(&data->spi->dev));
	data->debugfs = debugfs_create_dir(name, NULL);
	debugfs_create_file("mcu_log", 0400, data->debugfs, data,
			    &ssp_mcu_log_fops);
}

void ssp_debugfs_remove(struct ssp_data *data)
{
	/*
	 * Let blocked readers return, removal waits for them. There is no poll
	 * support, which would keep waiters on the queue past its lifetime.
	 */
	WRITE_ONCE(data->mcu_log_closed, true);
	wake_up_interruptible(&data->mcu_log_wq);

	debugfs_remove_recursive(data->debugfs);
	mutex_destroy(&data->mcu_log_lock);
}
