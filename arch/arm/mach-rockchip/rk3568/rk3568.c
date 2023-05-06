// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2021 Rockchip Electronics Co., Ltd
 */

#include <common.h>
#include <dm.h>
#include <asm/armv8/mmu.h>
#include <asm/io.h>
#include <asm/arch-rockchip/bootrom.h>
#include <asm/arch-rockchip/grf_rk3568.h>
#include <asm/arch-rockchip/hardware.h>
#include <dt-bindings/clock/rk3568-cru.h>

#define PMUGRF_BASE			0xfdc20000
#define GRF_BASE			0xfdc60000
#define GRF_GPIO1B_DS_2			0x218
#define GRF_GPIO1B_DS_3			0x21c
#define GRF_GPIO1C_DS_0			0x220
#define GRF_GPIO1C_DS_1			0x224
#define GRF_GPIO1C_DS_2			0x228
#define GRF_GPIO1C_DS_3			0x22c
#define SGRF_BASE			0xFDD18000
#define SGRF_SOC_CON4			0x10
#define EMMC_HPROT_SECURE_CTRL		0x03
#define SDMMC0_HPROT_SECURE_CTRL	0x01

#define PMU_BASE_ADDR		0xfdd90000
#define PMU_NOC_AUTO_CON0	(0x70)
#define PMU_NOC_AUTO_CON1	(0x74)
#define EDP_PHY_GRF_BASE	0xfdcb0000
#define EDP_PHY_GRF_CON0	(EDP_PHY_GRF_BASE + 0x00)
#define EDP_PHY_GRF_CON10	(EDP_PHY_GRF_BASE + 0x28)
#define CPU_GRF_BASE		0xfdc30000
#define GRF_CORE_PVTPLL_CON0	(0x10)

enum {
	/* PMU_GRF_GPIO0D_IOMUX_L */
	GPIO0D1_SHIFT		= 4,
	GPIO0D1_MASK		= GENMASK(6, 4),
	GPIO0D1_GPIO		= 0,
	GPIO0D1_UART2_TXM0,

	GPIO0D0_SHIFT		= 0,
	GPIO0D0_MASK		= GENMASK(2, 0),
	GPIO0D0_GPIO		= 0,
	GPIO0D0_UART2_RXM0,
};

enum {
	/* GRF_GPIO3C_IOMUX_L */
	GPIO3C3_SHIFT		= 12,
	GPIO3C3_MASK		= GENMASK(14, 12),
	GPIO3C3_GPIO		= 0,
	GPIO3C3_LCDC_DEN,
	GPIO3C3_BT1120_D15,
	GPIO3C3_SPI1_CLKM1,
	GPIO3C3_UART5_RXM1,
	GPIO3C3_I2S1_SCLKRXM,

	GPIO3C2_SHIFT		= 8,
	GPIO3C2_MASK		= GENMASK(10, 8),
	GPIO3C2_GPIO		= 0,
	GPIO3C2_LCDC_VSYNC,
	GPIO3C2_BT1120_D14,
	GPIO3C2_SPI1_MISOM1,
	GPIO3C2_UART5_TXM1,
	GPIO3C2_I2S1_SDO3M2,

	/* GRF_GPIO4C_IOMUX_H */
	GPIO4C6_SHIFT		= 8,
	GPIO4C6_MASK		= GENMASK(10, 8),
	GPIO4C6_GPIO		= 0,
	GPIO4C6_PWM13_M1,
	GPIO4C6_SPI3_CS0M1,
	GPIO4C6_SATA0_ACTLED,
	GPIO4C6_UART9_RXM1,
	GPIO4C6_I2S3_SDIM1,

	GPIO4C5_SHIFT		= 4,
	GPIO4C5_MASK		= GENMASK(6, 4),
	GPIO4C5_GPIO		= 0,
	GPIO4C5_PWM12_M1,
	GPIO4C5_SPI3_MISOM1,
	GPIO4C5_SATA1_ACTLED,
	GPIO4C5_UART9_TXM1,
	GPIO4C5_I2S3_SDOM1,

	/* GRF_IOFUNC_SEL3 */
	UART2_IO_SEL_SHIFT	= 10,
	UART2_IO_SEL_MASK	= GENMASK(11, 10),
	UART2_IO_SEL_M0		= 0,
	UART2_IO_SEL_M1,

	/* GRF_IOFUNC_SEL4 */
	UART9_IO_SEL_SHIFT	= 8,
	UART9_IO_SEL_MASK	= GENMASK(9, 8),
	UART9_IO_SEL_M0		= 0,
	UART9_IO_SEL_M1,
	UART9_IO_SEL_M2,

	UART5_IO_SEL_SHIFT	= 0,
	UART5_IO_SEL_MASK	= GENMASK(0, 0),
	UART5_IO_SEL_M0		= 0,
	UART5_IO_SEL_M1,
};

static struct mm_region rk3568_mem_map[] = {
	{
		.virt = 0x0UL,
		.phys = 0x0UL,
		.size = 0xf0000000UL,
		.attrs = PTE_BLOCK_MEMTYPE(MT_NORMAL) |
			 PTE_BLOCK_INNER_SHARE
	}, {
		.virt = 0xf0000000UL,
		.phys = 0xf0000000UL,
		.size = 0x10000000UL,
		.attrs = PTE_BLOCK_MEMTYPE(MT_DEVICE_NGNRNE) |
			 PTE_BLOCK_NON_SHARE |
			 PTE_BLOCK_PXN | PTE_BLOCK_UXN
	}, {
		.virt = 0x300000000,
		.phys = 0x300000000,
		.size = 0x0c0c00000,
		.attrs = PTE_BLOCK_MEMTYPE(MT_DEVICE_NGNRNE) |
			 PTE_BLOCK_NON_SHARE |
			 PTE_BLOCK_PXN | PTE_BLOCK_UXN
	}, {
		/* List terminator */
		0,
	}
};

const char * const boot_devices[BROM_LAST_BOOTSOURCE + 1] = {
	[BROM_BOOTSOURCE_EMMC] = "/mmc@fe310000",
	[BROM_BOOTSOURCE_SPINOR] = "/spi@fe300000/flash@0",
	[BROM_BOOTSOURCE_SD] = "/mmc@fe2b0000",
};

struct mm_region *mem_map = rk3568_mem_map;

void board_debug_uart_init(void)
{
	static struct rk3568_pmugrf * const pmugrf = (void *)PMUGRF_BASE;
	static struct rk3568_grf * const grf = (void *)GRF_BASE;

#if defined(CONFIG_DEBUG_UART_BASE) && (CONFIG_DEBUG_UART_BASE == 0xfe660000)
	/* UART2 M0 */
	rk_clrsetreg(&grf->iofunc_sel3, UART2_IO_SEL_MASK,
		     UART2_IO_SEL_M0 << UART2_IO_SEL_SHIFT);

	/* Switch iomux */
	rk_clrsetreg(&pmugrf->pmu_gpio0d_iomux_l,
		     GPIO0D1_MASK | GPIO0D0_MASK,
		     GPIO0D1_UART2_TXM0 << GPIO0D1_SHIFT |
		     GPIO0D0_UART2_RXM0 << GPIO0D0_SHIFT);
#elif defined(CONFIG_DEBUG_UART_BASE) && (CONFIG_DEBUG_UART_BASE == 0xfe690000)
	/* UART5 M1 */
	rk_clrsetreg(&grf->iofunc_sel4, UART5_IO_SEL_MASK,
		     UART5_IO_SEL_M1 << UART5_IO_SEL_SHIFT);

	/* Switch iomux */
	rk_clrsetreg(&grf->gpio3c_iomux_l,
		     GPIO3C3_MASK | GPIO3C2_MASK,
		     GPIO3C3_UART5_RXM1 << GPIO3C3_SHIFT |
		     GPIO3C2_UART5_TXM1 << GPIO3C2_SHIFT);
#elif defined(CONFIG_DEBUG_UART_BASE) && (CONFIG_DEBUG_UART_BASE == 0xfe6d0000)
	/* UART9 M1 */
	rk_clrsetreg(&grf->iofunc_sel4, UART9_IO_SEL_MASK,
		     UART9_IO_SEL_M1 << UART9_IO_SEL_SHIFT);

	/* Switch iomux */
	rk_clrsetreg(&grf->gpio4c_iomux_h,
		     GPIO4C6_MASK | GPIO4C5_MASK,
		     GPIO4C6_UART9_RXM1 << GPIO4C6_SHIFT |
		     GPIO4C5_UART9_TXM1 << GPIO4C5_SHIFT);
#endif
}

int arch_cpu_init(void)
{
#ifdef CONFIG_SPL_BUILD
	/*
	 * When perform idle operation, corresponding clock can
	 * be opened or gated automatically.
	 */
	writel(0xffffffff, PMU_BASE_ADDR + PMU_NOC_AUTO_CON0);
	writel(0x000f000f, PMU_BASE_ADDR + PMU_NOC_AUTO_CON1);

	/* Disable eDP phy by default */
	writel(0x00070007, EDP_PHY_GRF_CON10);
	writel(0x0ff10ff1, EDP_PHY_GRF_CON0);

	/* Set core pvtpll ring length */
	writel(0x00ff002b, CPU_GRF_BASE + GRF_CORE_PVTPLL_CON0);

	/* Set the emmc sdmmc0 to secure */
	rk_clrreg(SGRF_BASE + SGRF_SOC_CON4, (EMMC_HPROT_SECURE_CTRL << 11
		| SDMMC0_HPROT_SECURE_CTRL << 4));
	/* set the emmc driver strength to level 2 */
	writel(0x3f3f0707, GRF_BASE + GRF_GPIO1B_DS_2);
	writel(0x3f3f0707, GRF_BASE + GRF_GPIO1B_DS_3);
	writel(0x3f3f0707, GRF_BASE + GRF_GPIO1C_DS_0);
	writel(0x3f3f0707, GRF_BASE + GRF_GPIO1C_DS_1);
	writel(0x3f3f0707, GRF_BASE + GRF_GPIO1C_DS_2);
	writel(0x3f3f0707, GRF_BASE + GRF_GPIO1C_DS_3);
#endif
	return 0;
}
