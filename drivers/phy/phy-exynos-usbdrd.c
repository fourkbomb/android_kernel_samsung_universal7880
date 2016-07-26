/*
 * Samsung EXYNOS SoC series USB DRD PHY driver
 *
 * Phy provider for USB 3.0 DRD controller on Exynos SoC series
 *
 * Copyright (C) 2014 Samsung Electronics Co., Ltd.
 * Author: Vivek Gautam <gautam.vivek@samsung.com>
 *	   Minho Lee <minho55.lee@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/mfd/syscon.h>
#include <linux/mfd/syscon/exynos5-pmu.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/usb/samsung_usb.h>
#include <linux/usb/otg.h>

#include "phy-exynos-usbdrd.h"

static inline
struct exynos_usbdrd_phy *to_usbdrd_phy(struct phy_usb_instance *inst)
{
	return container_of((inst), struct exynos_usbdrd_phy,
			    phys[(inst)->index]);
}

/*
 * exynos_rate_to_clk() converts the supplied clock rate to the value that
 * can be written to the phy register.
 */
static unsigned int exynos_rate_to_clk(unsigned long rate, u32 *reg)
{
	/* EXYNOS_FSEL_MASK */

	switch (rate) {
	case 9600 * KHZ:
		*reg = EXYNOS_FSEL_9MHZ6;
		break;
	case 10 * MHZ:
		*reg = EXYNOS_FSEL_10MHZ;
		break;
	case 12 * MHZ:
		*reg = EXYNOS_FSEL_12MHZ;
		break;
	case 19200 * KHZ:
		*reg = EXYNOS_FSEL_19MHZ2;
		break;
	case 20 * MHZ:
		*reg = EXYNOS_FSEL_20MHZ;
		break;
	case 24 * MHZ:
		*reg = EXYNOS_FSEL_24MHZ;
		break;
	case 50 * MHZ:
		*reg = EXYNOS_FSEL_50MHZ;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void exynos_usbdrd_pipe3_phy_isol(struct phy_usb_instance *inst,
						unsigned int on)
{
	unsigned int val;

	if (!inst->reg_pmu)
		return;

	val = on ? 0 : EXYNOS5_PHY_ENABLE;

	regmap_update_bits(inst->reg_pmu, inst->pmu_offset,
			   EXYNOS5_PHY_ENABLE, val);
}

static void exynos_usbdrd_utmi_phy_isol(struct phy_usb_instance *inst,
						unsigned int on)
{
	return;
}

/*
 * Sets the pipe3 phy's clk as EXTREFCLK (XXTI) which is internal clock
 * from clock core. Further sets multiplier values and spread spectrum
 * clock settings for SuperSpeed operations.
 */
static unsigned int
exynos_usbdrd_pipe3_set_refclk(struct phy_usb_instance *inst)
{
	static u32 reg;
	struct exynos_usbdrd_phy *phy_drd = to_usbdrd_phy(inst);

	/* restore any previous reference clock settings */
	reg = readl(phy_drd->reg_phy + EXYNOS_DRD_PHYCLKRST);

	/* Use EXTREFCLK as ref clock */
	reg &= ~PHYCLKRST_REFCLKSEL_MASK;
	reg |=	PHYCLKRST_REFCLKSEL_EXT_REFCLK;

	/* FSEL settings corresponding to reference clock */
	reg &= ~PHYCLKRST_FSEL_PIPE_MASK |
		PHYCLKRST_MPLL_MULTIPLIER_MASK |
		PHYCLKRST_SSC_REFCLKSEL_MASK;
	switch (phy_drd->extrefclk) {
	case EXYNOS_FSEL_50MHZ:
		reg |= (PHYCLKRST_MPLL_MULTIPLIER_50M_REF |
			PHYCLKRST_SSC_REFCLKSEL(0x00));
		break;
	case EXYNOS_FSEL_24MHZ:
		reg |= (PHYCLKRST_MPLL_MULTIPLIER_24MHZ_REF |
			PHYCLKRST_SSC_REFCLKSEL(0x88));
		break;
	case EXYNOS_FSEL_20MHZ:
		reg |= (PHYCLKRST_MPLL_MULTIPLIER_20MHZ_REF |
			PHYCLKRST_SSC_REFCLKSEL(0x00));
		break;
	case EXYNOS_FSEL_19MHZ2:
		reg |= (PHYCLKRST_MPLL_MULTIPLIER_19200KHZ_REF |
			PHYCLKRST_SSC_REFCLKSEL(0x88));
		break;
	default:
		dev_dbg(phy_drd->dev, "unsupported ref clk\n");
		break;
	}

	return reg;
}

/*
 * Sets the utmi phy's clk as EXTREFCLK (XXTI) which is internal clock
 * from clock core. Further sets the FSEL values for HighSpeed operations.
 */
static unsigned int
exynos_usbdrd_utmi_set_refclk(struct phy_usb_instance *inst)
{
	static u32 reg;
	struct exynos_usbdrd_phy *phy_drd = to_usbdrd_phy(inst);

	/* restore any previous reference clock settings */
	reg = readl(phy_drd->reg_phy + EXYNOS_DRD_PHYCLKRST);

	reg &= ~PHYCLKRST_REFCLKSEL_MASK;
	reg |=	PHYCLKRST_REFCLKSEL_EXT_REFCLK;

	reg &= ~PHYCLKRST_FSEL_UTMI_MASK |
		PHYCLKRST_MPLL_MULTIPLIER_MASK |
		PHYCLKRST_SSC_REFCLKSEL_MASK;
	reg |= PHYCLKRST_FSEL(phy_drd->extrefclk);

	return reg;
}

static void exynos_usbdrd_pipe3_init(struct exynos_usbdrd_phy *phy_drd)
{
	u32 reg;

	reg = readl(phy_drd->reg_phy + EXYNOS_DRD_PHYPARAM1);
	/* Set Tx De-Emphasis level */
	reg &= ~PHYPARAM1_PCS_TXDEEMPH_MASK;
	reg |=	PHYPARAM1_PCS_TXDEEMPH;
	writel(reg, phy_drd->reg_phy + EXYNOS_DRD_PHYPARAM1);

	reg = readl(phy_drd->reg_phy + EXYNOS_DRD_PHYTEST);
	reg &= ~PHYTEST_POWERDOWN_SSP;
	writel(reg, phy_drd->reg_phy + EXYNOS_DRD_PHYTEST);
}

static void exynos_usbdrd_utmi_init(struct exynos_usbdrd_phy *phy_drd)
{
	u32 reg;

	reg = readl(phy_drd->reg_phy + EXYNOS_DRD_PHYPARAM0);
	/* Set Loss-of-Signal Detector sensitivity */
	reg &= ~PHYPARAM0_REF_LOSLEVEL_MASK;
	reg |=	PHYPARAM0_REF_LOSLEVEL;
	writel(reg, phy_drd->reg_phy + EXYNOS_DRD_PHYPARAM0);

	reg = readl(phy_drd->reg_phy + EXYNOS_DRD_PHYPARAM1);
	/* Set Tx De-Emphasis level */
	reg &= ~PHYPARAM1_PCS_TXDEEMPH_MASK;
	reg |=	PHYPARAM1_PCS_TXDEEMPH;
	writel(reg, phy_drd->reg_phy + EXYNOS_DRD_PHYPARAM1);

	/* UTMI Power Control */
	writel(PHYUTMI_OTGDISABLE, phy_drd->reg_phy + EXYNOS_DRD_PHYUTMI);

	reg = readl(phy_drd->reg_phy + EXYNOS_DRD_PHYTEST);
	reg &= ~PHYTEST_POWERDOWN_HSP;
	writel(reg, phy_drd->reg_phy + EXYNOS_DRD_PHYTEST);
}

static int exynos_usbdrd_phy_init(struct phy *phy)
{
	int ret;
	u32 reg;
	struct phy_usb_instance *inst = phy_get_drvdata(phy);
	struct exynos_usbdrd_phy *phy_drd = to_usbdrd_phy(inst);

	ret = clk_prepare_enable(phy_drd->clk);
	if (ret)
		return ret;

	/* Reset USB 3.0 PHY */
	writel(0x0, phy_drd->reg_phy + EXYNOS_DRD_PHYREG0);
	writel(0x0, phy_drd->reg_phy + EXYNOS_DRD_PHYRESUME);

	/*
	 * Setting the Frame length Adj value[6:1] to default 0x20
	 * See xHCI 1.0 spec, 5.2.4
	 */
	reg =	LINKSYSTEM_XHCI_VERSION_CONTROL |
		LINKSYSTEM_FLADJ(0x20);
	writel(reg, phy_drd->reg_phy + EXYNOS_DRD_LINKSYSTEM);

	reg = readl(phy_drd->reg_phy + EXYNOS_DRD_PHYPARAM0);
	/* Select PHY CLK source */
	reg &= ~PHYPARAM0_REF_USE_PAD;
	writel(reg, phy_drd->reg_phy + EXYNOS_DRD_PHYPARAM0);

	/*
	 * This bit must be set for both HS and SS operations.
	 * Actually, this setting should be seperated from Exynos5430,
	 * but this file was made for Exynos8 series and next.
	 * So, it was roughly modified.
	 */
	if (phy_drd->drv_data->cpu_type > TYPE_EXYNOS5) {
		reg = readl(phy_drd->reg_phy + EXYNOS_DRD_PHYPIPE);
		reg |= PHY_CLOCK_SEL;
		writel(reg, phy_drd->reg_phy + EXYNOS_DRD_PHYPIPE);
	} else {
		reg = readl(phy_drd->reg_phy + EXYNOS_DRD_PHYUTMICLKSEL);
		reg |= PHYUTMICLKSEL_UTMI_CLKSEL;
		writel(reg, phy_drd->reg_phy + EXYNOS_DRD_PHYUTMICLKSEL);
	}

	/* UTMI or PIPE3 specific init */
	inst->phy_cfg->phy_init(phy_drd);

	/* reference clock settings */
	reg = inst->phy_cfg->set_refclk(inst);

		/* Digital power supply in normal operating mode */
	reg |=	PHYCLKRST_RETENABLEN |
		/* Enable ref clock for SS function */
		PHYCLKRST_REF_SSP_EN |
		/* Enable spread spectrum */
		PHYCLKRST_SSC_EN |
		/* Power down HS Bias and PLL blocks in suspend mode */
		PHYCLKRST_COMMONONN |
		/* Reset the port */
		PHYCLKRST_PORTRESET;

	writel(reg, phy_drd->reg_phy + EXYNOS_DRD_PHYCLKRST);

	udelay(10);

	reg &= ~PHYCLKRST_PORTRESET;
	writel(reg, phy_drd->reg_phy + EXYNOS_DRD_PHYCLKRST);

	clk_disable_unprepare(phy_drd->clk);

	return 0;
}

static void __exynos_usbdrd_phy_shutdown(struct exynos_usbdrd_phy *phy_drd)
{
	u32 reg;

	reg =	PHYUTMI_OTGDISABLE |
		PHYUTMI_FORCESUSPEND |
		PHYUTMI_FORCESLEEP;
	writel(reg, phy_drd->reg_phy + EXYNOS_DRD_PHYUTMI);

	/* Resetting the PHYCLKRST enable bits to reduce leakage current */
	reg = readl(phy_drd->reg_phy + EXYNOS_DRD_PHYCLKRST);
	reg &= ~(PHYCLKRST_REF_SSP_EN |
		 PHYCLKRST_SSC_EN |
		 PHYCLKRST_COMMONONN);
	writel(reg, phy_drd->reg_phy + EXYNOS_DRD_PHYCLKRST);

	/* Control PHYTEST to remove leakage current */
	reg = readl(phy_drd->reg_phy + EXYNOS_DRD_PHYTEST);
	reg |=	PHYTEST_POWERDOWN_SSP;
	/*
	 * To save power, it is supposed to be set to POWERDOWN mode.
	 * However, in Exynos 5430 & 5433,
	 * Even when HSP is set to POWERDOWN mode, there is current leakage.
	 * Therefore, it is recommended not to set HSP to POWERDOWN mode.
	 */
	if (phy_drd->drv_data->cpu_type != TYPE_EXYNOS5430)
		reg |= PHYTEST_POWERDOWN_HSP;
	writel(reg, phy_drd->reg_phy + EXYNOS_DRD_PHYTEST);
}

static int exynos_usbdrd_phy_exit(struct phy *phy)
{
	int ret;
	struct phy_usb_instance *inst = phy_get_drvdata(phy);
	struct exynos_usbdrd_phy *phy_drd = to_usbdrd_phy(inst);

	ret = clk_prepare_enable(phy_drd->clk);
	if (ret)
		return ret;

	__exynos_usbdrd_phy_shutdown(phy_drd);

	clk_disable_unprepare(phy_drd->clk);

	return 0;
}

static int exynos_usbdrd_phy_tune(struct phy *phy, int phy_state)
{
	int ret;
	struct phy_usb_instance *inst = phy_get_drvdata(phy);
	struct exynos_usbdrd_phy *phy_drd = to_usbdrd_phy(inst);

	ret = clk_prepare_enable(phy_drd->clk);
	if (ret)
		return ret;

	if (phy_state >= OTG_STATE_A_IDLE)
		/* for host mode */
		;
	else
		/* for device mode */
		;

	clk_disable_unprepare(phy_drd->clk);

	return 0;
}

static int exynos_usbdrd_phy_power_on(struct phy *phy)
{
	int ret;
	struct phy_usb_instance *inst = phy_get_drvdata(phy);
	struct exynos_usbdrd_phy *phy_drd = to_usbdrd_phy(inst);

	dev_dbg(phy_drd->dev, "Request to power_on usbdrd_phy phy\n");

	clk_prepare_enable(phy_drd->ref_clk);

	/* Enable VBUS supply */
	if (phy_drd->vbus) {
		ret = regulator_enable(phy_drd->vbus);
		if (ret) {
			dev_err(phy_drd->dev, "Failed to enable VBUS supply\n");
			goto fail_vbus;
		}
	}

	/* Power-on PHY*/
	inst->phy_cfg->phy_isol(inst, 0);

	return 0;

fail_vbus:
	clk_disable_unprepare(phy_drd->ref_clk);

	return ret;
}

static int exynos_usbdrd_phy_power_off(struct phy *phy)
{
	struct phy_usb_instance *inst = phy_get_drvdata(phy);
	struct exynos_usbdrd_phy *phy_drd = to_usbdrd_phy(inst);

	dev_dbg(phy_drd->dev, "Request to power_off usbdrd_phy phy\n");

	/* Power-off the PHY */
	inst->phy_cfg->phy_isol(inst, 1);

	/* Disable VBUS supply */
	if (phy_drd->vbus)
		regulator_disable(phy_drd->vbus);

	clk_disable_unprepare(phy_drd->ref_clk);

	return 0;
}

static struct phy *exynos_usbdrd_phy_xlate(struct device *dev,
					struct of_phandle_args *args)
{
	struct exynos_usbdrd_phy *phy_drd = dev_get_drvdata(dev);

	if (WARN_ON(args->args[0] > EXYNOS_DRDPHYS_NUM))
		return ERR_PTR(-ENODEV);

	return phy_drd->phys[args->args[0]].phy;
}

static struct phy_ops exynos_usbdrd_phy_ops = {
	.init		= exynos_usbdrd_phy_init,
	.exit		= exynos_usbdrd_phy_exit,
	.tune		= exynos_usbdrd_phy_tune,
	.power_on	= exynos_usbdrd_phy_power_on,
	.power_off	= exynos_usbdrd_phy_power_off,
	.owner		= THIS_MODULE,
};

static const struct exynos_usbdrd_phy_config phy_cfg_exynos8[] = {
	{
		.id		= EXYNOS_DRDPHY_UTMI,
		.phy_isol	= exynos_usbdrd_utmi_phy_isol,
		.phy_init	= exynos_usbdrd_utmi_init,
		.set_refclk	= exynos_usbdrd_utmi_set_refclk,
	},
	{
		.id		= EXYNOS_DRDPHY_PIPE3,
		.phy_isol	= exynos_usbdrd_pipe3_phy_isol,
		.phy_init	= exynos_usbdrd_pipe3_init,
		.set_refclk	= exynos_usbdrd_pipe3_set_refclk,
	},
};

static const struct exynos_usbdrd_phy_drvdata exynos8_usbdrd_phy = {
	.phy_cfg		= phy_cfg_exynos8,
	.pmu_offset_usbdrd0_phy	= EXYNOS5_USBDRD_PHY_CONTROL,
	.cpu_type		= TYPE_EXYNOS8,
};

static const struct of_device_id exynos_usbdrd_phy_of_match[] = {
	{
		.compatible = "samsung,exynos8-usbdrd-phy",
		.data = &exynos8_usbdrd_phy
	},
	{ },
};
MODULE_DEVICE_TABLE(of, exynos5_usbdrd_phy_of_match);

static int exynos_usbdrd_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct exynos_usbdrd_phy *phy_drd;
	struct phy_provider *phy_provider;
	struct resource *res;
	const struct of_device_id *match;
	const struct exynos_usbdrd_phy_drvdata *drv_data;
	struct regmap *reg_pmu;
	u32 pmu_offset;
	unsigned long ref_rate;
	int i, ret;
	int channel;

	phy_drd = devm_kzalloc(dev, sizeof(*phy_drd), GFP_KERNEL);
	if (!phy_drd)
		return -ENOMEM;

	dev_set_drvdata(dev, phy_drd);
	phy_drd->dev = dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	phy_drd->reg_phy = devm_ioremap_resource(dev, res);
	if (IS_ERR(phy_drd->reg_phy))
		return PTR_ERR(phy_drd->reg_phy);

	match = of_match_node(exynos_usbdrd_phy_of_match, pdev->dev.of_node);

	drv_data = match->data;
	phy_drd->drv_data = drv_data;

	phy_drd->clk = devm_clk_get(dev, "phy");
	if (IS_ERR(phy_drd->clk)) {
		dev_err(dev, "Failed to get clock of phy controller\n");
		return PTR_ERR(phy_drd->clk);
	}

	phy_drd->ref_clk = devm_clk_get(dev, "ref");
	if (IS_ERR(phy_drd->ref_clk)) {
		dev_err(dev, "Failed to get reference clock of usbdrd phy\n");
		return PTR_ERR(phy_drd->ref_clk);
	}
	ref_rate = clk_get_rate(phy_drd->ref_clk);

	ret = exynos_rate_to_clk(ref_rate, &phy_drd->extrefclk);
	if (ret) {
		dev_err(phy_drd->dev, "Clock rate (%ld) not supported\n",
			ref_rate);
		return ret;
	}

	reg_pmu = syscon_regmap_lookup_by_phandle(dev->of_node,
						   "samsung,pmu-syscon");
	if (IS_ERR(reg_pmu)) {
		dev_err(dev, "Failed to lookup PMU regmap\n");
		return PTR_ERR(reg_pmu);
	}

	channel = of_alias_get_id(node, "usbdrdphy");
	if (channel < 0)
		dev_dbg(dev, "Not a multi-controller usbdrd phy\n");

	switch (channel) {
	case 1:
		pmu_offset = phy_drd->drv_data->pmu_offset_usbdrd1_phy;
		break;
	case 0:
	default:
		pmu_offset = phy_drd->drv_data->pmu_offset_usbdrd0_phy;
		break;
	}

	dev_vdbg(dev, "Creating usbdrd_phy phy\n");

	for (i = 0; i < EXYNOS_DRDPHYS_NUM; i++) {
		struct phy *phy = devm_phy_create(dev, NULL,
						  &exynos_usbdrd_phy_ops,
						  NULL);
		if (IS_ERR(phy)) {
			dev_err(dev, "Failed to create usbdrd_phy phy\n");
			return PTR_ERR(phy);
		}

		phy_drd->phys[i].phy = phy;
		phy_drd->phys[i].index = i;
		phy_drd->phys[i].reg_pmu = reg_pmu;
		phy_drd->phys[i].pmu_offset = pmu_offset;
		phy_drd->phys[i].phy_cfg = &drv_data->phy_cfg[i];
		phy_set_drvdata(phy, &phy_drd->phys[i]);
	}

	phy_provider = devm_of_phy_provider_register(dev,
						     exynos_usbdrd_phy_xlate);
	if (IS_ERR(phy_provider)) {
		dev_err(phy_drd->dev, "Failed to register phy provider\n");
		return PTR_ERR(phy_provider);
	}

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int exynos_usbdrd_phy_resume(struct device *dev)
{
	int ret;
	struct exynos_usbdrd_phy *phy_drd = dev_get_drvdata(dev);

	/*
	 * There is issue, when USB3.0 PHY is in active state
	 * after resume. This leads to increased power consumption
	 * if no USB drivers use the PHY.
	 *
	 * The following code shutdowns the PHY, so it is in defined
	 * state (OFF) after resume. If any USB driver already got
	 * the PHY at this time, we do nothing and just exit.
	 */

	dev_dbg(dev, "%s\n", __func__);

	ret = clk_prepare_enable(phy_drd->clk);
	if (ret) {
		dev_err(dev, "%s: clk_enable failed\n", __func__);
		return ret;
	}

	__exynos_usbdrd_phy_shutdown(phy_drd);

exit:
	clk_disable_unprepare(phy_drd->clk);

	return 0;
}

static const struct dev_pm_ops exynos_usbdrd_phy_dev_pm_ops = {
	.resume	= exynos_usbdrd_phy_resume,
};

#define EXYNOS_USBDRD_PHY_PM_OPS	&(exynos_usbdrd_phy_dev_pm_ops)
#else
#define EXYNOS_USBDRD_PHY_PM_OPS	NULL
#endif

static struct platform_driver phy_exynos_usbdrd = {
	.probe	= exynos_usbdrd_phy_probe,
	.driver = {
		.of_match_table	= exynos_usbdrd_phy_of_match,
		.name		= "phy_exynos_usbdrd",
		.pm		= EXYNOS_USBDRD_PHY_PM_OPS,
	}
};

module_platform_driver(phy_exynos_usbdrd);
MODULE_DESCRIPTION("Samsung EXYNOS SoCs USB DRD controller PHY driver");
MODULE_AUTHOR("Vivek Gautam <gautam.vivek@samsung.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:phy_exynos_usbdrd");
