// SPDX-License-Identifier: GPL-2.0
//
// Samsung Exynos Flattened Device Tree enabled machine
//
// Copyright (c) 2010-2014 Samsung Electronics Co., Ltd.
//		http://www.samsung.com

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_fdt.h>
#include <linux/platform_device.h>
#include <linux/irqchip.h>
#include <linux/reboot.h>
#include <linux/soc/samsung/exynos-regs-pmu.h>

#include <asm/cacheflush.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include "common.h"

#define S3C_ADDR_BASE	0xF6000000
#define S3C_ADDR(x)	((void __iomem __force *)S3C_ADDR_BASE + (x))
#define S5P_VA_CHIPID	S3C_ADDR(0x02000000)

static struct platform_device exynos_cpuidle = {
	.name              = "exynos_cpuidle",
#ifdef CONFIG_ARM_EXYNOS_CPUIDLE
	.dev.platform_data = exynos_enter_aftr,
#endif
	.id                = -1,
};

static struct platform_device exynos_power_off = {
	.name              = "exynos_power_off",
	.id                = -1,
};

static int exynos_power_off_probe(struct platform_device* pdev);

static const struct of_device_id exynos_power_off_of_id[] = {
	/*
	 * Register power off handler for Rinato only.
	 * Other Exynos3250-based machines may also be able to
	 * use something like this, requires further digging in downstream sources.
	 */
  { .compatible = "samsung,rinato", },
	/* sentinel */
  { }
};

MODULE_DEVICE_TABLE(of, exynos_power_off_of_id);

static struct platform_driver exynos_power_off_driver = {
	.driver	= {
		.name			= "exynos_power_off",
		.of_match_table		= of_match_ptr(exynos_power_off_of_id),
	},
	.probe	= exynos_power_off_probe,
	/* TODO: .remove */
};
builtin_platform_driver(exynos_power_off_driver);

void __iomem *sysram_base_addr __ro_after_init;
phys_addr_t sysram_base_phys __ro_after_init;
void __iomem *sysram_ns_base_addr __ro_after_init;

unsigned long exynos_cpu_id;
static unsigned int exynos_cpu_rev;

unsigned int exynos_rev(void)
{
	return exynos_cpu_rev;
}

void __init exynos_sysram_init(void)
{
	struct device_node *node;

	for_each_compatible_node(node, NULL, "samsung,exynos4210-sysram") {
		struct resource res;
		if (!of_device_is_available(node))
			continue;

		of_address_to_resource(node, 0, &res);
		sysram_base_addr = ioremap(res.start, resource_size(&res));
		sysram_base_phys = res.start;
		of_node_put(node);
		break;
	}

	for_each_compatible_node(node, NULL, "samsung,exynos4210-sysram-ns") {
		if (!of_device_is_available(node))
			continue;
		sysram_ns_base_addr = of_iomap(node, 0);
		of_node_put(node);
		break;
	}
}

static int __init exynos_fdt_map_chipid(unsigned long node, const char *uname,
					int depth, void *data)
{
	struct map_desc iodesc;
	const __be32 *reg;
	int len;

	if (!of_flat_dt_is_compatible(node, "samsung,exynos4210-chipid"))
		return 0;

	reg = of_get_flat_dt_prop(node, "reg", &len);
	if (reg == NULL || len != (sizeof(unsigned long) * 2))
		return 0;

	iodesc.pfn = __phys_to_pfn(be32_to_cpu(reg[0]));
	iodesc.length = be32_to_cpu(reg[1]) - 1;
	iodesc.virtual = (unsigned long)S5P_VA_CHIPID;
	iodesc.type = MT_DEVICE;
	iotable_init(&iodesc, 1);
	return 1;
}

static void __init exynos_init_io(void)
{
	debug_ll_io_init();

	of_scan_flat_dt(exynos_fdt_map_chipid, NULL);

	/* detect cpu id and rev. */
	exynos_cpu_id = readl_relaxed(S5P_VA_CHIPID);
	exynos_cpu_rev = exynos_cpu_id & 0xFF;

	pr_info("Samsung CPU ID: 0x%08lx\n", exynos_cpu_id);

}

/*
 * Set or clear the USE_DELAYED_RESET_ASSERTION option. Used by smp code
 * and suspend.
 *
 * This is necessary only on Exynos4 SoCs. When system is running
 * USE_DELAYED_RESET_ASSERTION should be set so the ARM CLK clock down
 * feature could properly detect global idle state when secondary CPU is
 * powered down.
 *
 * However this should not be set when such system is going into suspend.
 */
void exynos_set_delayed_reset_assertion(bool enable)
{
	if (of_machine_is_compatible("samsung,exynos4")) {
		unsigned int tmp, core_id;

		for (core_id = 0; core_id < num_possible_cpus(); core_id++) {
			tmp = pmu_raw_readl(EXYNOS_ARM_CORE_OPTION(core_id));
			if (enable)
				tmp |= S5P_USE_DELAYED_RESET_ASSERTION;
			else
				tmp &= ~(S5P_USE_DELAYED_RESET_ASSERTION);
			pmu_raw_writel(tmp, EXYNOS_ARM_CORE_OPTION(core_id));
		}
	}
}

static int exynos_power_off_handler(struct sys_off_data *data)
{
	/* Sequence for entering LP charging mode (not a true power off).
	 * Unused currently as there's no reasonable API to expose this, but here for posterity
	 * pmu_raw_writel(0x0, S5P_INFORM2);
	 * pmu_raw_writel(0x1, EXYNOS_SWRESET);
	 * Wait for magic to happen, kernel should be dead after this
	 *	while (1)
	 *		;
	 */

	int reboot_try;

	reboot_try = 0;
	while (reboot_try < 5) {
		/*
		 * Magical register write done by Rinato on downstream kernel when powering off.
		 * Might also be possible in DT via syscon-poweroff binding, but didn't work in my testing.
		 *
		 * PS_HOLD Out/High -->
		 * Low PS_HOLD_CONTROL, R/W, 0x1002_330C
		 */
			pmu_raw_writel(pmu_raw_readl(S5P_PS_HOLD_CONTROL) & 0xFFFFFEFF,
			       S5P_PS_HOLD_CONTROL);
		msleep(1000);
		reboot_try++;
	}
	/* If we get here, something's wrong */
	panic("Reboot failed!");
	return 0;
}

static int exynos_power_off_probe(struct platform_device *pdev)
{
	int err;

	/* Only Rinato for now */
	if (!of_machine_is_compatible("samsung,rinato"))
		return 0;

	// Not sure if actually handled by firmware
	err = devm_register_sys_off_handler(&pdev->dev,
					    SYS_OFF_MODE_POWER_OFF,
					    SYS_OFF_PRIO_FIRMWARE,
					    exynos_power_off_handler, NULL);
	if (err) {
		dev_err(&pdev->dev, "failed to register sys-off handler: %d\n",
			err);
		return err;
	}

	return err;
}

/*
 * Apparently, these SoCs are not able to wake-up from suspend using
 * the PMU. Too bad. Should they suddenly become capable of such a
 * feat, the matches below should be moved to suspend.c.
 */
static const struct of_device_id exynos_dt_pmu_match[] = {
	{ .compatible = "samsung,exynos5260-pmu" },
	{ .compatible = "samsung,exynos5410-pmu" },
	{ /*sentinel*/ },
};

static void exynos_map_pmu(void)
{
	struct device_node *np;

	np = of_find_matching_node(NULL, exynos_dt_pmu_match);
	if (np)
		pmu_base_addr = of_iomap(np, 0);
	of_node_put(np);
}

static void __init exynos_init_irq(void)
{
	irqchip_init();
	/*
	 * Since platsmp.c needs pmu base address by the time
	 * DT is not unflatten so we can't use DT APIs before
	 * init_irq
	 */
	exynos_map_pmu();
}

static void __init exynos_dt_machine_init(void)
{
	/*
	 * This is called from smp_prepare_cpus if we've built for SMP, but
	 * we still need to set it up for PM and firmware ops if not.
	 */
	if (!IS_ENABLED(CONFIG_SMP))
		exynos_sysram_init();

#if defined(CONFIG_SMP) && defined(CONFIG_ARM_EXYNOS_CPUIDLE)
	if (of_machine_is_compatible("samsung,exynos4210") ||
	    of_machine_is_compatible("samsung,exynos3250"))
		exynos_cpuidle.dev.platform_data = &cpuidle_coupled_exynos_data;
#endif
	if (of_machine_is_compatible("samsung,exynos4210") ||
	    of_machine_is_compatible("samsung,exynos4212") ||
	    (of_machine_is_compatible("samsung,exynos4412") &&
	     (of_machine_is_compatible("samsung,trats2") ||
		  of_machine_is_compatible("samsung,midas") ||
		  of_machine_is_compatible("samsung,p4note"))) ||
	    of_machine_is_compatible("samsung,exynos3250") ||
	    of_machine_is_compatible("samsung,exynos5250"))
		platform_device_register(&exynos_cpuidle);

	if (of_machine_is_compatible("samsung,rinato")) {
		platform_device_register(&exynos_power_off);
		platform_driver_register(&exynos_power_off_driver);
	}
}

static char const *const exynos_dt_compat[] __initconst = {
	"samsung,exynos3",
	"samsung,exynos3250",
	"samsung,exynos4",
	"samsung,exynos4210",
	"samsung,exynos4212",
	"samsung,exynos4412",
	"samsung,exynos5",
	"samsung,exynos5250",
	"samsung,exynos5260",
	"samsung,exynos5420",
	NULL
};

static void __init exynos_dt_fixup(void)
{
	/*
	 * Some versions of uboot pass garbage entries in the memory node,
	 * use the old CONFIG_ARM_NR_BANKS
	 */
	of_fdt_limit_memory(8);
}

DT_MACHINE_START(EXYNOS_DT, "Samsung Exynos (Flattened Device Tree)")
	.l2c_aux_val	= 0x08400000,
	.l2c_aux_mask	= 0xf60fffff,
	.smp		= smp_ops(exynos_smp_ops),
	.map_io		= exynos_init_io,
	.init_early	= exynos_firmware_init,
	.init_irq	= exynos_init_irq,
	.init_machine	= exynos_dt_machine_init,
	.init_late	= exynos_pm_init,
	.dt_compat	= exynos_dt_compat,
	.dt_fixup	= exynos_dt_fixup,
MACHINE_END
