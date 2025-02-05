// SPDX-License-Identifier: GPL-2.0-only
/*
 * Samsung S-Boot upload mode driver
 *
 * S-Boot provides the ability to display a message on screen
 * and dump memory after a crash.
 * This is a useful debug feature, especially if no debug console is available.
 *
 * This driver automatically boots into S-Boot upload mode after a kernel
 * panic and displays the panic message on screen.
 *
 * Copyright (C) 2025 Davids Paskevics <davids.paskevics@gmail.com>.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/device.h>
#include <linux/kdebug.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/notifier.h>
#include <linux/of_address.h>
#include <linux/panic_notifier.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/of.h>

/*
 * Comment from Samsung's downstream kernel regarding the debug area layout:
 * 0: magic (4B)
│* 4~1023: panic string (1020B)
 * 1024~0x1000: panic dumper log
│* 0x4000: copy of magic
 */

#define SBOOT_DEBUG_AREA_SIZE 0x4000

#define SBOOT_MAGIC_START_OFFSET 0
#define SBOOT_MAGIC_END_OFFSET (SBOOT_DEBUG_AREA_SIZE - 4)

#define SBOOT_PANIC_STRING_START_OFFSET 4
#define SBOOT_PANIC_STRING_SIZE 1020
#define SBOOT_PANIC_STRING_END_OFFSET \
	(SBOOT_PANIC_STRING_START_OFFSET + SBOOT_PANIC_STRING_SIZE)

#define SBOOT_MAGIC_NORMAL 0
#define SBOOT_MAGIC_UPLOAD 0x66262564

static void sboot_iowrite32(void __iomem *sboot_mapping, u32 val,
			    unsigned long offset)
{
	writel(val, sboot_mapping + offset);
}

static void sboot_iowrite8(void __iomem *sboot_mapping, u8 val,
			   unsigned long offset)
{
	writeb(val, sboot_mapping + offset);
}

static void sboot_write_magic(void __iomem *sboot_mapping, u32 magic)
{
	sboot_iowrite32(sboot_mapping, magic, SBOOT_MAGIC_START_OFFSET);
	sboot_iowrite32(sboot_mapping, magic, SBOOT_MAGIC_END_OFFSET);
}

static void sboot_clear_panic_string(void __iomem *sboot_mapping)
{
	for (unsigned long offset = SBOOT_PANIC_STRING_START_OFFSET;
	     offset <= SBOOT_PANIC_STRING_END_OFFSET; offset++) {
		sboot_iowrite8(sboot_mapping, 0, offset);
	}
}

static void sboot_write_panic_string(void __iomem *sboot_mapping,
				     const char *str)
{
	int len = strlen(str);

	if (len > (SBOOT_PANIC_STRING_SIZE - 1))
		pr_warn("Panic string will not fit in S-Boot buffer, truncating the end!\n");

	unsigned long bytes_to_write = min(len, SBOOT_PANIC_STRING_SIZE);

	for (unsigned int i = 0; i <= bytes_to_write; i++) {
		unsigned long offset = i + SBOOT_PANIC_STRING_START_OFFSET;

		sboot_iowrite8(sboot_mapping, str[i], offset);
	}
}

/* Structure holding notifier_block and other info needed to handle callback */
struct sboot_reboot_notifier {
	struct notifier_block nb;
	void *__iomem sboot_mapping;
};

/* Structure holding private data of a driver instance */
struct sboot_reboot_drvdata {
	/* Kernel panic notification handler */
	struct sboot_reboot_notifier srn_panic;
	/* Description of memory region allocated in DT */
	struct resource *region;
	/* Size of this region */
	resource_size_t size;
};

static int sboot_panic_cb(struct notifier_block *nb, unsigned long action,
			  void *data)
{
	struct sboot_reboot_notifier *srn;
	void __iomem *sboot_mapping;

	/* Extract pointer to I/O mapping we appended after notifier_block structure */
	srn = (struct sboot_reboot_notifier *)nb;
	sboot_mapping = srn->sboot_mapping;

	pr_info("Handling kernel panic, will enter upload mode after reboot\n");
	/* Notify S-Boot to enter upload mode on next reboot */
	sboot_write_magic(sboot_mapping, SBOOT_MAGIC_UPLOAD);

	/* Make S-Boot display (part of) the panic message */
	sboot_clear_panic_string(sboot_mapping);
	const char *panic_str = (const char *)data;

	pr_info("Panic string: %s\n", panic_str);
	sboot_write_panic_string(sboot_mapping, panic_str);

	return NOTIFY_OK;
}

static int sboot_upload_probe(struct platform_device *pdev)
{
	int err;
	struct device_node *mem_node;
	struct resource mem_res;
	struct resource *region;
	void __iomem *sboot_mapping;
	resource_size_t start;
	resource_size_t size;
	struct sboot_reboot_drvdata *drv_data;

	/* Get S-Boot memory region reference from DT */
	mem_node = of_parse_phandle(pdev->dev.of_node, "memory-region", 0);
	if (!mem_node) {
		pr_err("Probe failed: could not get memory region! Is a memory-region entry defined in DT?\n");
		err = -ENXIO;
		goto err;
	}

	err = of_address_to_resource(mem_node, 0, &mem_res);
	if (err) {
		pr_err("Probe failed: No memory address assigned to the region\n");
		goto err;
	}

	/* Claim S-Boot debug area memory */
	start = mem_res.start;
	size = mem_res.end - mem_res.start + 1;
	region = request_mem_region(start, size, "sboot_upload");
	if (IS_ERR(region)) {
		pr_err("Failed to request memory region, backing out!\n");
		err = PTR_ERR(region);
		goto err;
	}

	sboot_mapping = ioremap(start, size);
	if (sboot_mapping == NULL) {
		pr_err("Got NULL MMU mapping, backing out!");
		pr_err("Perhaps you didn't exclude the configured S-Boot debug address range from your device's main memory range?\n");
		err = PTR_ERR(sboot_mapping);
		goto err;
	}

	/* Tell S-Boot to treat the reboot as a normal reboot, if we don't panic */
	sboot_write_magic(sboot_mapping, SBOOT_MAGIC_NORMAL);

	drv_data = kzalloc(sizeof(*drv_data), 0);
	if (drv_data == NULL) {
		err = -ENOMEM;
		goto err_remapped;
	}
	drv_data->srn_panic.nb.notifier_call = sboot_panic_cb;
	drv_data->srn_panic.sboot_mapping = sboot_mapping;
	drv_data->region = region;
	drv_data->size = size;

	atomic_notifier_chain_register(&panic_notifier_list,
				       &drv_data->srn_panic.nb);
	platform_set_drvdata(pdev, drv_data);
	return 0;

err_remapped:
	iounmap(sboot_mapping);
err:
	return err;
}

static void sboot_upload_remove(struct platform_device *pdev)
{
	struct sboot_reboot_drvdata *drv;

	drv = platform_get_drvdata(pdev);

	atomic_notifier_chain_unregister(&panic_notifier_list,
					 &drv->srn_panic.nb);

	iounmap(drv->srn_panic.sboot_mapping);
	release_mem_region(drv->region->start, drv->size);

	kfree(drv);
}

static struct platform_device sboot_upload_dev = {
	.name = "sboot_upload",
	.id = -1,
};

static const struct of_device_id sboot_upload_dt_ids[] = {
	{
		.compatible = "samsung,sboot-upload",
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sboot_upload_dt_ids);

static struct platform_driver sboot_upload_driver = {
	.probe = sboot_upload_probe,
	.remove = sboot_upload_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = "sboot-upload",
		.of_match_table = sboot_upload_dt_ids,
	},
};

static int __init sboot_upload_init(void)
{
	int r;

	pr_info("Init\n");

	r = platform_device_register(&sboot_upload_dev);
	if (r)
		goto out;

	r = platform_driver_register(&sboot_upload_driver);
	if (r)
		goto out_err;
	return 0;

out_err:
	platform_device_unregister(&sboot_upload_dev);
out:
	return r;
}

static void __exit sboot_upload_exit(void)
{
	platform_driver_unregister(&sboot_upload_driver);
	platform_device_unregister(&sboot_upload_dev);
}

module_init(sboot_upload_init);
module_exit(sboot_upload_exit);
MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("Davids Paskevics <davids.paskevics@gmail.com>");
MODULE_DESCRIPTION("Debug helper for devices with Samsung S-Boot bootloader");
