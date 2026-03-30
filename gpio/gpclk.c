// SPDX-License-Identifier: GPL-2.0
/*
 * PiStorm kernel helper module.
 *
 * 1. Configures GPCLK0 via cprman (200MHz clock for CPLD).
 * 2. Exports /dev/pistorm-gpio: mmap gives the GPIO register page
 *    mapped as Device-nGnRnE — every store blocks until it reaches the
 *    GPIO peripheral.  No early write acknowledgment, no gathering,
 *    no reordering.  This should eliminate the need for DSB + readback
 *    barriers on Pi 4.
 *
 * Usage: insmod gpclk.ko [freq=200000000]
 */
#include <linux/module.h>
#include <linux/version.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <linux/fs.h>

static unsigned long freq = 200000000;
module_param(freq, ulong, 0444);
MODULE_PARM_DESC(freq, "GPCLK0 frequency in Hz (default 200000000)");

static struct clk *gp0_clk;
static phys_addr_t gpio_phys;

/* ------------------------------------------------------------------ */
/*  /dev/pistorm-gpio — one-page mmap of the GPIO register block      */
/* ------------------------------------------------------------------ */

static int pistorm_gpio_mmap(struct file *file, struct vm_area_struct *vma)
{
	unsigned long size = vma->vm_end - vma->vm_start;

	if (size > PAGE_SIZE)
		return -EINVAL;
	if (vma->vm_pgoff != 0)
		return -EINVAL;

	/* Device-nGnRnE: the strictest ARM device-memory type.
	 * pgprot_noncached() on arm64 selects MT_DEVICE_nGnRnE (MAIR Attr0 = 0x00).
	 * This means:
	 *   - no write gathering
	 *   - no reordering
	 *   - no early write acknowledgment
	 * Each STR to this mapping blocks until the write response comes
	 * back from the GPIO peripheral, not just from the AXI fabric. */
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
	vm_flags_set(vma, VM_IO | VM_DONTEXPAND | VM_DONTDUMP);
#else
	vma->vm_flags |= VM_IO | VM_DONTEXPAND | VM_DONTDUMP;
#endif

	return io_remap_pfn_range(vma, vma->vm_start,
				  gpio_phys >> PAGE_SHIFT,
				  size, vma->vm_page_prot);
}

static const struct file_operations pistorm_gpio_fops = {
	.owner = THIS_MODULE,
	.mmap  = pistorm_gpio_mmap,
};

static struct miscdevice pistorm_gpio_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "pistorm-gpio",
	.fops  = &pistorm_gpio_fops,
};

/* ------------------------------------------------------------------ */
/*  MAIR / mapping diagnostic                                         */
/* ------------------------------------------------------------------ */

static void dump_mair(void)
{
#ifdef CONFIG_ARM64
	u64 mair;
	int i;

	asm volatile("mrs %0, mair_el1" : "=r" (mair));
	pr_info("pistorm: MAIR_EL1 = 0x%016llx\n", mair);
	for (i = 0; i < 8; i++) {
		u8 attr = (mair >> (i * 8)) & 0xFF;
		const char *desc;

		if (attr == 0x00)
			desc = "Device-nGnRnE (strictest)";
		else if (attr == 0x04)
			desc = "Device-nGnRE (early-ack!)";
		else if (attr == 0x08)
			desc = "Device-nGRE";
		else if (attr == 0x0C)
			desc = "Device-GRE";
		else if (attr == 0x44)
			desc = "Normal-NC";
		else if ((attr & 0xF0) == 0 && (attr & 0x0C) != 0)
			desc = "Device (other)";
		else
			desc = (attr == 0xFF) ? "Normal-WB-RWA" : "Normal (other)";

		pr_info("pistorm:   Attr%d = 0x%02x  %s\n", i, attr, desc);
	}
#else
	pr_info("pistorm: MAIR dump only available on arm64\n");
#endif
}

/* ------------------------------------------------------------------ */
/*  Peripheral base detection                                          */
/* ------------------------------------------------------------------ */

static phys_addr_t detect_peri_base(void)
{
	if (of_machine_is_compatible("brcm,bcm2711"))
		return 0xFE000000;
	if (of_machine_is_compatible("brcm,bcm2712"))
		return 0x1F000D0000ULL; /* Pi 5 — not tested */
	if (of_machine_is_compatible("brcm,bcm2837") ||
	    of_machine_is_compatible("brcm,bcm2836"))
		return 0x3F000000;
	if (of_machine_is_compatible("brcm,bcm2835"))
		return 0x20000000;
	pr_warn("pistorm: unknown SoC, defaulting to Pi 4 base\n");
	return 0xFE000000;
}

/* ------------------------------------------------------------------ */
/*  Init / Exit                                                        */
/* ------------------------------------------------------------------ */

static int __init gpclk_init(void)
{
	struct device_node *cprman_node;
	struct of_phandle_args clkspec;
	phys_addr_t peri_base;
	int ret;

	dump_mair();

	peri_base = detect_peri_base();
	gpio_phys = peri_base + 0x200000;
	pr_info("pistorm: peri_base=0x%llx  gpio_phys=0x%llx\n",
		(unsigned long long)peri_base,
		(unsigned long long)gpio_phys);

	/* --- GPCLK0 setup --- */
	cprman_node = of_find_compatible_node(NULL, NULL, "brcm,bcm2711-cprman");
	if (!cprman_node)
		cprman_node = of_find_compatible_node(NULL, NULL,
						      "brcm,bcm2835-cprman");
	if (!cprman_node) {
		pr_err("pistorm: cannot find cprman node\n");
		return -ENODEV;
	}

	clkspec.np = cprman_node;
	clkspec.args_count = 1;
	clkspec.args[0] = 38;  /* BCM2835_CLOCK_GP0 */
	gp0_clk = of_clk_get_from_provider(&clkspec);
	of_node_put(cprman_node);

	if (IS_ERR(gp0_clk)) {
		pr_err("pistorm: cannot get gp0 clock: %ld\n",
		       PTR_ERR(gp0_clk));
		return PTR_ERR(gp0_clk);
	}

	ret = clk_set_rate(gp0_clk, freq);
	if (ret) {
		pr_err("pistorm: clk_set_rate(%lu): %d\n", freq, ret);
		clk_put(gp0_clk);
		return ret;
	}

	ret = clk_prepare_enable(gp0_clk);
	if (ret) {
		pr_err("pistorm: clk_prepare_enable: %d\n", ret);
		clk_put(gp0_clk);
		return ret;
	}

	pr_info("pistorm: GPCLK0 running at %lu Hz (requested %lu)\n",
		clk_get_rate(gp0_clk), freq);

	/* --- /dev/pistorm-gpio --- */
	ret = misc_register(&pistorm_gpio_dev);
	if (ret) {
		pr_err("pistorm: misc_register: %d\n", ret);
		clk_disable_unprepare(gp0_clk);
		clk_put(gp0_clk);
		return ret;
	}

	pr_info("pistorm: /dev/pistorm-gpio ready (nGnRnE, phys 0x%llx)\n",
		(unsigned long long)gpio_phys);

	return 0;
}

static void __exit gpclk_exit(void)
{
	misc_deregister(&pistorm_gpio_dev);

	if (!IS_ERR_OR_NULL(gp0_clk)) {
		clk_disable_unprepare(gp0_clk);
		clk_put(gp0_clk);
	}
	pr_info("pistorm: unloaded\n");
}

module_init(gpclk_init);
module_exit(gpclk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("PiStorm: GPCLK0 + GPIO nGnRnE mapping");
