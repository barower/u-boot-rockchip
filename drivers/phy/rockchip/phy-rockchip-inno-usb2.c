// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Rockchip USB2.0 PHY with Innosilicon IP block driver
 *
 * Copyright (C) 2016 Fuzhou Rockchip Electronics Co., Ltd
 * Copyright (C) 2020 Amarula Solutions(India)
 */

#include <common.h>
#include <clk-uclass.h>
#include <dm.h>
#include <dm/device_compat.h>
#include <dm/lists.h>
#include <generic-phy.h>
#include <regmap.h>
#include <syscon.h>
#include <asm/arch-rockchip/clock.h>

#define BIT_WRITEABLE_SHIFT	16

enum rockchip_usb2phy_port_id {
	USB2PHY_PORT_OTG,
	USB2PHY_PORT_HOST,
	USB2PHY_NUM_PORTS,
};

struct usb2phy_reg {
	unsigned int	offset;
	unsigned int	bitend;
	unsigned int	bitstart;
	unsigned int	disable;
	unsigned int	enable;
};

struct rockchip_usb2phy_port_cfg {
	struct usb2phy_reg	phy_sus;
	struct usb2phy_reg	bvalid_det_en;
	struct usb2phy_reg	bvalid_det_st;
	struct usb2phy_reg	bvalid_det_clr;
	struct usb2phy_reg	ls_det_en;
	struct usb2phy_reg	ls_det_st;
	struct usb2phy_reg	ls_det_clr;
	struct usb2phy_reg	utmi_avalid;
	struct usb2phy_reg	utmi_bvalid;
	struct usb2phy_reg	utmi_ls;
	struct usb2phy_reg	utmi_hstdet;
};

struct rockchip_usb2phy_cfg {
	unsigned int reg;
	struct usb2phy_reg clkout_ctl;
	const struct rockchip_usb2phy_port_cfg port_cfgs[USB2PHY_NUM_PORTS];
};

struct rockchip_usb2phy_port {
	struct regmap *reg_base;
	unsigned long port_id;
	struct clk clk480m;
	const struct rockchip_usb2phy_port_cfg *port_cfg;
};

struct rockchip_usb2phy {
	struct regmap *reg_base;
	struct clk phyclk;
	const struct rockchip_usb2phy_cfg *phy_cfg;
	int enable_count;
};

static inline int property_enable(struct regmap *base,
				  const struct usb2phy_reg *reg, bool en)
{
	unsigned int val, mask, tmp;

	if (!reg->offset && !reg->enable && !reg->disable)
		return 0;

	tmp = en ? reg->enable : reg->disable;
	mask = GENMASK(reg->bitend, reg->bitstart);
	val = (tmp << reg->bitstart) | (mask << BIT_WRITEABLE_SHIFT);

	return regmap_write(base, reg->offset, val);
}

static inline bool property_enabled(struct regmap *base,
				    const struct usb2phy_reg *reg)
{
	unsigned int val, mask, tmp;

	if (!reg->offset && !reg->enable && !reg->disable)
		return false;

	regmap_read(base, reg->offset, &val);
	mask = GENMASK(reg->bitend, reg->bitstart);
	tmp = (val & mask) >> reg->bitstart;

	return tmp == reg->enable;
}

static int rockchip_usb2phy_power_on(struct phy *phy)
{
	struct rockchip_usb2phy_port *priv = dev_get_priv(phy->dev);
	const struct rockchip_usb2phy_port_cfg *port_cfg = priv->port_cfg;
	int ret;

	ret = clk_enable(&priv->clk480m);
	if (ret && ret != -ENOSYS) {
		dev_err(phy->dev, "failed to enable clk480m (ret=%d)\n", ret);
		return ret;
	}

	property_enable(priv->reg_base, &port_cfg->phy_sus, false);

	/* waiting for the utmi_clk to become stable */
	mdelay(2);

	return 0;
}

static int rockchip_usb2phy_power_off(struct phy *phy)
{
	struct rockchip_usb2phy_port *priv = dev_get_priv(phy->dev);
	const struct rockchip_usb2phy_port_cfg *port_cfg = priv->port_cfg;

	property_enable(priv->reg_base, &port_cfg->phy_sus, true);

	clk_disable(&priv->clk480m);

	return 0;
}

static int rockchip_usb2phy_init(struct phy *phy)
{
	struct rockchip_usb2phy_port *priv = dev_get_priv(phy->dev);
	struct rockchip_usb2phy *p_priv = dev_get_priv(phy->dev->parent);
	const struct rockchip_usb2phy_port_cfg *port_cfg = priv->port_cfg;
	int ret;

	ret = clk_enable(&p_priv->phyclk);
	if (ret && ret != -ENOSYS) {
		dev_err(phy->dev, "failed to enable phyclk (ret=%d)\n", ret);
		return ret;
	}

	property_enable(priv->reg_base, &port_cfg->bvalid_det_clr, true);
	property_enable(priv->reg_base, &port_cfg->bvalid_det_en, true);

	return 0;
}

static int rockchip_usb2phy_exit(struct phy *phy)
{
	struct rockchip_usb2phy *p_priv = dev_get_priv(phy->dev->parent);

	clk_disable(&p_priv->phyclk);

	return 0;
}

static int rockchip_usb2phy_of_xlate(struct phy *phy,
				     struct ofnode_phandle_args *args)
{
	struct rockchip_usb2phy_port *priv = dev_get_priv(phy->dev);

	phy->id = priv->port_id;

	return 0;
}

static struct phy_ops rockchip_usb2phy_port_ops = {
	.init = rockchip_usb2phy_init,
	.exit = rockchip_usb2phy_exit,
	.power_on = rockchip_usb2phy_power_on,
	.power_off = rockchip_usb2phy_power_off,
	.of_xlate = rockchip_usb2phy_of_xlate,
};

static ulong rockchip_usb2phy_clk_round_rate(struct clk *clk, ulong rate)
{
	return 480000000;
}

static int rockchip_usb2phy_clk_enable(struct clk *clk)
{
	struct rockchip_usb2phy *priv = dev_get_priv(clk->dev);
	const struct usb2phy_reg *clkout_ctl = &priv->phy_cfg->clkout_ctl;

	/* turn on 480m clk output if it is off */
	if (!property_enabled(priv->reg_base, clkout_ctl)) {
		property_enable(priv->reg_base, clkout_ctl, true);

		/* waiting for the clk to become stable */
		udelay(1300);
	}

	++priv->enable_count;

	return 0;
}

static int rockchip_usb2phy_clk_disable(struct clk *clk)
{
	struct rockchip_usb2phy *priv = dev_get_priv(clk->dev);
	const struct usb2phy_reg *clkout_ctl = &priv->phy_cfg->clkout_ctl;

	if (--priv->enable_count > 0)
		return 0;

	/* turn off 480m clk output */
	property_enable(priv->reg_base, clkout_ctl, false);

	priv->enable_count = 0;

	return 0;
}

static struct clk_ops rockchip_usb2phy_ops = {
	.round_rate = rockchip_usb2phy_clk_round_rate,
	.enable = rockchip_usb2phy_clk_enable,
	.disable = rockchip_usb2phy_clk_disable,
};

static int rockchip_usb2phy_port_probe(struct udevice *dev)
{
	struct rockchip_usb2phy_port *priv = dev_get_priv(dev);
	struct rockchip_usb2phy *p_priv = dev_get_priv(dev->parent);
	struct udevice *dev_clk;
	int ret;

	if (!p_priv || !p_priv->phy_cfg)
		return -EINVAL;

	if (!strcmp(dev->name, "host-port"))
		priv->port_id = USB2PHY_PORT_HOST;
	else if (!strcmp(dev->name, "otg-port"))
		priv->port_id = USB2PHY_PORT_OTG;
	else
		return -EINVAL;

	ret = uclass_get_device_by_ofnode(UCLASS_CLK, dev_ofnode(dev->parent),
					  &dev_clk);
	if (ret) {
		dev_err(dev, "failed to get the clk480m (ret=%d)\n", ret);
		return ret;
	}

	priv->reg_base = p_priv->reg_base;
	priv->clk480m.dev = dev_clk;
	priv->port_cfg = &p_priv->phy_cfg->port_cfgs[priv->port_id];

	return 0;
}

static const
struct rockchip_usb2phy_cfg *rockchip_usb2phy_get_phy_cfg(struct udevice *dev)
{
	const struct rockchip_usb2phy_cfg *phy_cfgs =
		(const struct rockchip_usb2phy_cfg *)dev_get_driver_data(dev);
	u32 reg;
	int index;

	if (!phy_cfgs)
		return NULL;

	if (dev_read_u32_index(dev, "reg", 0, &reg)) {
		dev_err(dev, "failed to read reg property\n");
		return NULL;
	}

	/* support address_cells=2 */
	if (reg == 0 && dev_read_u32_index(dev, "reg", 1, &reg)) {
		dev_err(dev, "failed to read reg property\n");
		return NULL;
	}

	/* find out a proper config which can be matched with dt. */
	index = 0;
	do {
		if (phy_cfgs[index].reg == reg)
			return &phy_cfgs[index];

		++index;
	} while (phy_cfgs[index].reg);

	dev_err(dev, "failed find proper phy-cfg\n");
	return NULL;
}

static int rockchip_usb2phy_probe(struct udevice *dev)
{
	struct rockchip_usb2phy *priv = dev_get_priv(dev);
	int ret;

	if (dev_read_bool(dev, "rockchip,usbgrf"))
		priv->reg_base =
			syscon_regmap_lookup_by_phandle(dev, "rockchip,usbgrf");
	else
		priv->reg_base =
			syscon_get_regmap_by_driver_data(ROCKCHIP_SYSCON_GRF);
	if (IS_ERR(priv->reg_base))
		return PTR_ERR(priv->reg_base);

	priv->phy_cfg = rockchip_usb2phy_get_phy_cfg(dev);
	if (!priv->phy_cfg)
		return -EINVAL;

	ret = clk_get_by_name(dev, "phyclk", &priv->phyclk);
	if (ret) {
		dev_err(dev, "failed to get the phyclk (ret=%d)\n", ret);
		return ret;
	}

	return 0;
}

static int rockchip_usb2phy_bind(struct udevice *dev)
{
	ofnode node;
	const char *name;
	int ret;

	dev_for_each_subnode(node, dev) {
		if (!ofnode_is_enabled(node))
			continue;

		name = ofnode_get_name(node);
		dev_dbg(dev, "subnode %s\n", name);

		ret = device_bind_driver_to_node(dev, "rockchip_usb2phy_port",
						 name, node, NULL);
		if (ret) {
			dev_err(dev,
				"'%s' cannot bind 'rockchip_usb2phy_port'\n", name);
			return ret;
		}
	}

	return 0;
}

static const struct rockchip_usb2phy_cfg rk3399_usb2phy_cfgs[] = {
	{
		.reg		= 0xe450,
		.clkout_ctl	= { 0xe450, 4, 4, 1, 0 },
		.port_cfgs	= {
			[USB2PHY_PORT_OTG] = {
				.phy_sus	= { 0xe454, 1, 0, 2, 1 },
				.bvalid_det_en	= { 0xe3c0, 3, 3, 0, 1 },
				.bvalid_det_st	= { 0xe3e0, 3, 3, 0, 1 },
				.bvalid_det_clr	= { 0xe3d0, 3, 3, 0, 1 },
				.utmi_avalid	= { 0xe2ac, 7, 7, 0, 1 },
				.utmi_bvalid	= { 0xe2ac, 12, 12, 0, 1 },
			},
			[USB2PHY_PORT_HOST] = {
				.phy_sus	= { 0xe458, 1, 0, 0x2, 0x1 },
				.ls_det_en	= { 0xe3c0, 6, 6, 0, 1 },
				.ls_det_st	= { 0xe3e0, 6, 6, 0, 1 },
				.ls_det_clr	= { 0xe3d0, 6, 6, 0, 1 },
				.utmi_ls	= { 0xe2ac, 22, 21, 0, 1 },
				.utmi_hstdet	= { 0xe2ac, 23, 23, 0, 1 }
			}
		},
	},
	{
		.reg		= 0xe460,
		.clkout_ctl	= { 0xe460, 4, 4, 1, 0 },
		.port_cfgs	= {
			[USB2PHY_PORT_OTG] = {
				.phy_sus        = { 0xe464, 1, 0, 2, 1 },
				.bvalid_det_en  = { 0xe3c0, 8, 8, 0, 1 },
				.bvalid_det_st  = { 0xe3e0, 8, 8, 0, 1 },
				.bvalid_det_clr = { 0xe3d0, 8, 8, 0, 1 },
				.utmi_avalid	= { 0xe2ac, 10, 10, 0, 1 },
				.utmi_bvalid    = { 0xe2ac, 16, 16, 0, 1 },
			},
			[USB2PHY_PORT_HOST] = {
				.phy_sus	= { 0xe468, 1, 0, 0x2, 0x1 },
				.ls_det_en	= { 0xe3c0, 11, 11, 0, 1 },
				.ls_det_st	= { 0xe3e0, 11, 11, 0, 1 },
				.ls_det_clr	= { 0xe3d0, 11, 11, 0, 1 },
				.utmi_ls	= { 0xe2ac, 26, 25, 0, 1 },
				.utmi_hstdet	= { 0xe2ac, 27, 27, 0, 1 }
			}
		},
	},
	{ /* sentinel */ }
};

static const struct rockchip_usb2phy_cfg rk3568_phy_cfgs[] = {
	{
		.reg		= 0xfe8a0000,
		.clkout_ctl	= { 0x0008, 4, 4, 1, 0 },
		.port_cfgs	= {
			[USB2PHY_PORT_OTG] = {
				.phy_sus	= { 0x0000, 8, 0, 0x052, 0x1d1 },
				.bvalid_det_en	= { 0x0080, 2, 2, 0, 1 },
				.bvalid_det_st	= { 0x0084, 2, 2, 0, 1 },
				.bvalid_det_clr = { 0x0088, 2, 2, 0, 1 },
				.ls_det_en	= { 0x0080, 0, 0, 0, 1 },
				.ls_det_st	= { 0x0084, 0, 0, 0, 1 },
				.ls_det_clr	= { 0x0088, 0, 0, 0, 1 },
				.utmi_avalid	= { 0x00c0, 10, 10, 0, 1 },
				.utmi_bvalid	= { 0x00c0, 9, 9, 0, 1 },
				.utmi_ls	= { 0x00c0, 5, 4, 0, 1 },
			},
			[USB2PHY_PORT_HOST] = {
				.phy_sus	= { 0x0004, 8, 0, 0x1d2, 0x1d1 },
				.ls_det_en	= { 0x0080, 1, 1, 0, 1 },
				.ls_det_st	= { 0x0084, 1, 1, 0, 1 },
				.ls_det_clr	= { 0x0088, 1, 1, 0, 1 },
				.utmi_ls	= { 0x00c0, 17, 16, 0, 1 },
				.utmi_hstdet	= { 0x00c0, 19, 19, 0, 1 }
			}
		},
	},
	{
		.reg		= 0xfe8b0000,
		.clkout_ctl	= { 0x0008, 4, 4, 1, 0 },
		.port_cfgs	= {
			[USB2PHY_PORT_OTG] = {
				.phy_sus	= { 0x0000, 8, 0, 0x1d2, 0x1d1 },
				.ls_det_en	= { 0x0080, 0, 0, 0, 1 },
				.ls_det_st	= { 0x0084, 0, 0, 0, 1 },
				.ls_det_clr	= { 0x0088, 0, 0, 0, 1 },
				.utmi_ls	= { 0x00c0, 5, 4, 0, 1 },
				.utmi_hstdet	= { 0x00c0, 7, 7, 0, 1 }
			},
			[USB2PHY_PORT_HOST] = {
				.phy_sus	= { 0x0004, 8, 0, 0x1d2, 0x1d1 },
				.ls_det_en	= { 0x0080, 1, 1, 0, 1 },
				.ls_det_st	= { 0x0084, 1, 1, 0, 1 },
				.ls_det_clr	= { 0x0088, 1, 1, 0, 1 },
				.utmi_ls	= { 0x00c0, 17, 16, 0, 1 },
				.utmi_hstdet	= { 0x00c0, 19, 19, 0, 1 }
			}
		},
	},
	{ /* sentinel */ }
};

static const struct rockchip_usb2phy_cfg rk3588_phy_cfgs[] = {
	{
		.reg		= 0x0000,
		.port_cfgs	= {
			[USB2PHY_PORT_OTG] = {
				.phy_sus	= { 0x000c, 11, 11, 0, 1 },
				.ls_det_en	= { 0x0080, 0, 0, 0, 1 },
				.ls_det_st	= { 0x0084, 0, 0, 0, 1 },
				.ls_det_clr	= { 0x0088, 0, 0, 0, 1 },
				.utmi_ls	= { 0x00c0, 10, 9, 0, 1 },
			}
		},
	},
	{
		.reg		= 0x4000,
		.port_cfgs	= {
			[USB2PHY_PORT_OTG] = {
				.phy_sus	= { 0x000c, 11, 11, 0, 0 },
				.ls_det_en	= { 0x0080, 0, 0, 0, 1 },
				.ls_det_st	= { 0x0084, 0, 0, 0, 1 },
				.ls_det_clr	= { 0x0088, 0, 0, 0, 1 },
				.utmi_ls	= { 0x00c0, 10, 9, 0, 1 },
			}
		},
	},
	{
		.reg		= 0x8000,
		.port_cfgs	= {
			[USB2PHY_PORT_HOST] = {
				.phy_sus	= { 0x0008, 2, 2, 0, 1 },
				.ls_det_en	= { 0x0080, 0, 0, 0, 1 },
				.ls_det_st	= { 0x0084, 0, 0, 0, 1 },
				.ls_det_clr	= { 0x0088, 0, 0, 0, 1 },
				.utmi_ls	= { 0x00c0, 10, 9, 0, 1 },
			}
		},
	},
	{
		.reg		= 0xc000,
		.port_cfgs	= {
			[USB2PHY_PORT_HOST] = {
				.phy_sus	= { 0x0008, 2, 2, 0, 1 },
				.ls_det_en	= { 0x0080, 0, 0, 0, 1 },
				.ls_det_st	= { 0x0084, 0, 0, 0, 1 },
				.ls_det_clr	= { 0x0088, 0, 0, 0, 1 },
				.utmi_ls	= { 0x00c0, 10, 9, 0, 1 },
			}
		},
	},
	{ /* sentinel */ }
};

static const struct udevice_id rockchip_usb2phy_ids[] = {
	{
		.compatible = "rockchip,rk3399-usb2phy",
		.data = (ulong)&rk3399_usb2phy_cfgs,
	},
	{
		.compatible = "rockchip,rk3568-usb2phy",
		.data = (ulong)&rk3568_phy_cfgs,
	},
	{
		.compatible = "rockchip,rk3588-usb2phy",
		.data = (ulong)&rk3588_phy_cfgs,
	},
	{ /* sentinel */ }
};

U_BOOT_DRIVER(rockchip_usb2phy_port) = {
	.name		= "rockchip_usb2phy_port",
	.id		= UCLASS_PHY,
	.probe		= rockchip_usb2phy_port_probe,
	.priv_auto	= sizeof(struct rockchip_usb2phy_port),
	.ops		= &rockchip_usb2phy_port_ops,
};

U_BOOT_DRIVER(rockchip_usb2phy) = {
	.name		= "rockchip_usb2phy",
	.id		= UCLASS_CLK,
	.of_match	= rockchip_usb2phy_ids,
	.bind		= rockchip_usb2phy_bind,
	.probe		= rockchip_usb2phy_probe,
	.priv_auto	= sizeof(struct rockchip_usb2phy),
	.ops		= &rockchip_usb2phy_ops,
};
