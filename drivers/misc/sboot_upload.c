#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kdebug.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/notifier.h>
#include <linux/panic_notifier.h>
#include <linux/string.h>
#include <linux/types.h>

#include <asm/io.h>

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("Davids Paskevics <davids.paskevics@gmail.com>");
MODULE_DESCRIPTION("Debug helper for devices with Samsung S-Boot bootloader");

/*
 * Comment from Samsung's downstream kernel regarding the debug area layout:
 * 0: magic (4B)
│* 4~1023: panic string (1020B)
 * 1024~0x1000: panic dumper log
│* 0x4000: copy of magic
 */

/*
 * TODO: Make these configurable via device tree binding -
 * other devices probably have similar debug mechanisms, but at different locations in RAM.
 */

#define SBOOT_DEBUG_AREA_START_ADDR 0x40000000
#define SBOOT_DEBUG_AREA_SIZE 0x4000

#define SBOOT_MAGIC_START_OFFSET 0
#define SBOOT_MAGIC_END_OFFSET (SBOOT_DEBUG_AREA_SIZE - 4)

#define SBOOT_PANIC_STRING_START_OFFSET 4
#define SBOOT_PANIC_STRING_SIZE 1020
#define SBOOT_PANIC_STRING_END_OFFSET \
	(SBOOT_PANIC_STRING_START_OFFSET + SBOOT_PANIC_STRING_SIZE)

#define SBOOT_MAGIC_NORMAL 0
#define SBOOT_MAGIC_UPLOAD 0x66262564

static void __iomem *sboot_mapping = NULL;

static void sboot_iowrite32(u32 val, unsigned long offset)
{
	writel(val, sboot_mapping + offset);
}

static void sboot_iowrite8(u8 val, unsigned long offset)
{
	writeb(val, sboot_mapping + offset);
}

static void sboot_write_magic(u32 magic)
{
	sboot_iowrite32(magic, SBOOT_MAGIC_START_OFFSET);
	sboot_iowrite32(magic, SBOOT_MAGIC_END_OFFSET);
}

static void sboot_clear_panic_string(void)
{
	for (unsigned long offset = SBOOT_PANIC_STRING_START_OFFSET;
	     offset <= SBOOT_PANIC_STRING_END_OFFSET; offset++) {
		sboot_iowrite8(0, offset);
	}
}

static void sboot_write_panic_string(const char *str)
{
	int len = strlen(str);
	if (len > (SBOOT_PANIC_STRING_SIZE - 1)) {
		pr_warn("Panic string will not fit in S-Boot buffer, truncating the end!\n");
	}

	unsigned long bytes_to_write = min(len, SBOOT_PANIC_STRING_SIZE);
	for (unsigned int i = 0; i <= bytes_to_write; i++) {
		unsigned long offset = i + SBOOT_PANIC_STRING_START_OFFSET;
		sboot_iowrite8(str[i], offset);
	}
}

static int sboot_panic_cb(struct notifier_block *nb, unsigned long action,
			  void *data)
{
	pr_info("Handling kernel panic, will enter upload mode after reboot\n");
	/* Notify S-Boot to enter upload mode on next reboot */
	sboot_write_magic(SBOOT_MAGIC_UPLOAD);

	/* Make S-Boot display (part of) the panic message */
	sboot_clear_panic_string();
	const char *panic_str = (const char *)data;
	pr_info("Panic string: %s\n", panic_str);
	sboot_write_panic_string(panic_str);

	return NOTIFY_OK;
}

/* Kernel panic notification handler */
static struct notifier_block sboot_panic_nb = {
	.notifier_call = sboot_panic_cb,
	/* Don't care about priority - we can be executed in any order */
};

static int sboot_reboot_cb(struct notifier_block *nb, unsigned long action,
			   void *data)
{
	pr_info("Handling regular reboot\n");
	sboot_write_magic(SBOOT_MAGIC_NORMAL);
	return NOTIFY_OK;
}

/* Regular kernel reboot notification handler */
static struct notifier_block sboot_reboot_nb = {
	.notifier_call = sboot_reboot_cb,
	/* Don't care about priority - we can be executed in any order */
};

static int __init sboot_init(void)
{
	pr_info("Init\n");
	/* Claim S-Boot debug area memory */
	struct resource *region = request_mem_region(
		SBOOT_DEBUG_AREA_START_ADDR, SBOOT_DEBUG_AREA_SIZE, "sboot");
	if (IS_ERR(region)) {
		pr_err("Failed to request memory region, backing out!\n");
		return PTR_ERR(region);
	}

	sboot_mapping =
		ioremap(SBOOT_DEBUG_AREA_START_ADDR, SBOOT_DEBUG_AREA_SIZE);
	if (sboot_mapping == NULL) {
		pr_err("Got NULL MMU mapping, backing out!");
		pr_err("Perhaps you didn't exclude the configured S-Boot debug address range from your device tree's memory range?\n");
		return PTR_ERR(sboot_mapping);
	}

	atomic_notifier_chain_register(&panic_notifier_list, &sboot_panic_nb);
	blocking_notifier_chain_register(&reboot_notifier_list,
					 &sboot_reboot_nb);

	pr_info("Init OK\n");
	return 0;
}

static void __exit sboot_exit(void)
{
	pr_info("Exit\n");
	atomic_notifier_chain_unregister(&panic_notifier_list, &sboot_panic_nb);
	blocking_notifier_chain_unregister(&reboot_notifier_list,
					   &sboot_reboot_nb);
	iounmap(sboot_mapping);
	release_mem_region(SBOOT_DEBUG_AREA_START_ADDR, SBOOT_DEBUG_AREA_SIZE);
	pr_info("Exit OK\n");
}

module_init(sboot_init);
module_exit(sboot_exit);
