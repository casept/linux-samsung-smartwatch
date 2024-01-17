#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kdebug.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/notifier.h>
#include <linux/panic_notifier.h>
#include <linux/percpu-defs.h>
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

/*
 * NOTE: Upload cause handling should really be moved into the PMU driver or something,
 * as it touches PMU registers and other stuff that really doesn't belong here.
 * However, it's really unlikely to be upstreamed anyway as Samsung seems to not care about
 * making mainline boot on retail (AKA S-Boot) devices at all.
 */
#define PMU_INFORM_BASE (0x02180000 + 0x0800)
#define PMU_INFORM_SIZE 0x190
#define PMU_INFORM2_OFFSET 0x0008
#define PMU_INFORM3_OFFSET 0x000C
#define PMU_INFORM4_OFFSET 0x0010
#define PMU_INFORM6_OFFSET 0x0018

/* Taken verbatim from Samsung's downstream sources, not sure what some of these mean */
enum upload_cause_t {
	UPLOAD_CAUSE_INIT = 0xCAFEBABE,
	UPLOAD_CAUSE_KERNEL_PANIC = 0x000000C8,
	UPLOAD_CAUSE_FORCED_UPLOAD = 0x00000022,
	UPLOAD_CAUSE_CP_ERROR_FATAL = 0x000000CC,
	UPLOAD_CAUSE_USER_FAULT = 0x0000002F,
	UPLOAD_CAUSE_HSIC_DISCONNECTED = 0x000000DD,
};

static void __iomem *sboot_mapping = NULL;
static void __iomem *pmu_mapping = NULL;

DEFINE_PER_CPU(enum upload_cause_t, upload_cause);

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

static void sboot_pmuwrite32(u32 val, unsigned long offset)
{
	writel(val, pmu_mapping + offset);
}

static void sboot_write_upload_cause(enum upload_cause_t cause)
{
	// Why is it even needed to write this to a per-CPU var in RAM? Downstream doesn't ever read it out from there
	per_cpu(upload_cause, smp_processor_id()) = cause;
	pr_err("Upload cause: 0x%u\n", cause);
	sboot_pmuwrite32(cause, PMU_INFORM3_OFFSET);
	sboot_pmuwrite32(cause, PMU_INFORM4_OFFSET);
	sboot_pmuwrite32(cause, PMU_INFORM6_OFFSET);
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

	/* Notify S-Boot that upload was caused by a kernel panic */
	sboot_write_upload_cause(UPLOAD_CAUSE_KERNEL_PANIC);

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
	struct resource *sboot_region = request_mem_region(
		SBOOT_DEBUG_AREA_START_ADDR, SBOOT_DEBUG_AREA_SIZE, "sboot");
	if (IS_ERR(sboot_region)) {
		pr_err("Failed to request S-Boot debug memory region, backing out!\n");
		return PTR_ERR(sboot_region);
	}

	sboot_mapping =
		ioremap(SBOOT_DEBUG_AREA_START_ADDR, SBOOT_DEBUG_AREA_SIZE);
	if (sboot_mapping == NULL) {
		pr_err("Got NULL MMU mapping, backing out!\n");
		pr_err("Perhaps you didn't exclude the configured S-Boot debug address range from your device tree's memory range?\n");
		return PTR_ERR(sboot_mapping);
	}

	/* Claim relevant PMU debug register memory */
	struct resource *pmu_region = request_mem_region(
		PMU_INFORM_BASE, PMU_INFORM_SIZE, "sboot_pmu_debug");
	if (IS_ERR(pmu_region)) {
		pr_err("Failed to request PMU debug memory region, backing out!\n");
		pr_err("Maybe a conflict with the real PMU driver?\n");
		return PTR_ERR(pmu_region);
	}

	pmu_mapping = ioremap(PMU_INFORM_BASE, PMU_INFORM_SIZE);
	if (pmu_mapping == NULL) {
		pr_err("Got NULL MMU mapping, backing out!\n");
		return PTR_ERR(pmu_mapping);
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
	iounmap(pmu_mapping);
	release_mem_region(SBOOT_DEBUG_AREA_START_ADDR, SBOOT_DEBUG_AREA_SIZE);
	release_mem_region(PMU_INFORM_BASE, PMU_INFORM_SIZE);
	pr_info("Exit OK\n");
}

module_init(sboot_init);
module_exit(sboot_exit);
