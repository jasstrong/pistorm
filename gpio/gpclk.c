// SPDX-License-Identifier: GPL-2.0
/*
 * gpclk — tiny kernel module to configure GPCLK0 via cprman.
 * Usage: insmod gpclk.ko [freq=200000000]
 *
 * Looks up the "gp0" clock from the cprman provider, sets its rate,
 * and enables it. The clock stays running until the module is unloaded.
 */
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/platform_device.h>

static unsigned long freq = 200000000;
module_param(freq, ulong, 0444);
MODULE_PARM_DESC(freq, "GPCLK0 frequency in Hz (default 200000000)");

static struct clk *gp0_clk;

static int __init gpclk_init(void)
{
	struct device_node *cprman_node;
	struct of_phandle_args clkspec;
	int ret;

	/* Find the cprman clock provider in the device tree */
	cprman_node = of_find_compatible_node(NULL, NULL, "brcm,bcm2711-cprman");
	if (!cprman_node)
		cprman_node = of_find_compatible_node(NULL, NULL, "brcm,bcm2835-cprman");
	if (!cprman_node) {
		pr_err("gpclk: cannot find cprman node\n");
		return -ENODEV;
	}

	/* Get gp0 clock by specifying the provider + clock ID directly */
	clkspec.np = cprman_node;
	clkspec.args_count = 1;
	clkspec.args[0] = 38;  /* BCM2835_CLOCK_GP0 */
	gp0_clk = of_clk_get_from_provider(&clkspec);
	of_node_put(cprman_node);

	if (IS_ERR(gp0_clk)) {
		pr_err("gpclk: cannot get gp0 clock: %ld\n", PTR_ERR(gp0_clk));
		return PTR_ERR(gp0_clk);
	}

	ret = clk_set_rate(gp0_clk, freq);
	if (ret) {
		pr_err("gpclk: failed to set rate to %lu Hz: %d\n", freq, ret);
		clk_put(gp0_clk);
		return ret;
	}

	ret = clk_prepare_enable(gp0_clk);
	if (ret) {
		pr_err("gpclk: failed to enable: %d\n", ret);
		clk_put(gp0_clk);
		return ret;
	}

	pr_info("gpclk: GPCLK0 running at %lu Hz (requested %lu)\n",
		clk_get_rate(gp0_clk), freq);
	return 0;
}

static void __exit gpclk_exit(void)
{
	if (!IS_ERR_OR_NULL(gp0_clk)) {
		clk_disable_unprepare(gp0_clk);
		clk_put(gp0_clk);
	}
	pr_info("gpclk: GPCLK0 stopped\n");
}

module_init(gpclk_init);
module_exit(gpclk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("PiStorm GPCLK0 clock setup");
