// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2023, Ferass El Hafidi <vitali64pmemail@protonmail.com>
 *
 * Partially based on: https://github.com/hardkernel/u-boot.git/
 * HEAD at commit be63a06fa3d50dd2410e2e722d1f8bafe7370abd
 * "PD#111584: update bl30 and bl31: fix cpu_off crash issue",
 * path: plat/gxb/ddr/ddr.c, this file is licensed under the GPL-2.0+
 * license and is:
 *
 * Copyright (C) 2015, Amlogic, Inc. All rights reserved.
 */
// DO NOT USE THIS IN PRODUCTION SYSTEMS!
#include <common.h>
#include <init.h>
#include <asm/unaligned.h>
#include <linux/libfdt.h>
#include <config.h>
#include <asm/io.h>
#include <asm/arch/dram-gx.h>
#include <asm/arch/gx.h>
#if CONFIG_DRAM_USE_BOARD_SETTINGS
#include <board/dram-settings.h>
#else
#include <asm/arch/dram-settings-gx.h>
#endif
#include <linux/delay.h>
#include <asm/global_data.h>

DECLARE_GLOBAL_DATA_PTR;

/*
 * Unfourtunately all we have are old Amlogic BL2 sources (BL2 was later
 * made proprietary) and reverse-engineering efforts made available at
 * <https://moin.vitali64.duckdns.org/AmlogicBL2>. The DDR init code is
 * full of magic, which also explains why this code isn't that much
 * documented.
 */

//unsigned int dram_clock_pll;

#define WAIT_FOR(a) \
	start = get_timer(0); \
	while (!(readl(a) & 1) && (get_timer(start) < 10000)) { \
		debug("get_timer(start): %lu\n", get_timer(start)) ;} \
	if (!(readl(a) & 1)) \
		panic("%s: init failed, err=%d", __func__, -ETIMEDOUT);

void initialise_dram_pll(void)
{
	debug("%s\n", __func__);
	setbits_32(AM_ANALOG_TOP_REG1, 1);
	setbits_32(HHI_MPLL_CNTL5, 1);

	clrbits_32(AM_DDR_PLL_CNTL4, 1 << 12);
	setbits_32(AM_DDR_PLL_CNTL4, 1 << 12);
	udelay(10);

	do {
		writel(1 << 29, AM_DDR_PLL_CNTL0);
		writel(0x69C80000, AM_DDR_PLL_CNTL1);
		writel(0xCA463823, AM_DDR_PLL_CNTL2);
		writel(0x00C00023, AM_DDR_PLL_CNTL3);
		writel(0x00303500, AM_DDR_PLL_CNTL4);
#if CONFIG_DRAM_CLK >= 375 && CONFIG_DRAM_CLK <= 749
		writel((1 << 29) | ((2 << 16) | (1 << 9) |
			(((CONFIG_DRAM_CLK / 6) * 6) / 12)), AM_DDR_PLL_CNTL0);
#elif CONFIG_DRAM_CLK >= 750 && CONFIG_DRAM_CLK <= 1449
		writel((1 << 29) | ((1 << 16) | (1 << 9) |
			(((CONFIG_DRAM_CLK / 12) * 12) / 24)), AM_DDR_PLL_CNTL0);
#else
#error "CONFIG_DRAM_CLK too high or too low"
#endif
		clrbits_32(AM_DDR_PLL_CNTL0, 1 << 29);
		udelay(200);
	} while (!((readl(AM_DDR_PLL_CNTL0) >> 0x1F) & 1));

	writel(0xB0000000, DDR_CLK_CNTL);
	writel(0xB0000000, DDR_CLK_CNTL);
	debug("DRAM clock: %d MHz\n", CONFIG_DRAM_CLK);
}

#define DMC_ENABLE_REGION(REGION) \
	writel(0xffffffff, REGION## _SEC_CFG); \
	writel(0x55555555, REGION## _SEC_WRITE_CTRL); \
	writel(0x55555555, REGION## _SEC_READ_CTRL);

void initialise_dram_dmc(void)
{
	//int ddr_size_register = 0;
	u32 reg;

	debug("%s\n", __func__);

#if CONFIG_DRAM_CHANNEL == 1 /* 1 channel, ddr0 */
	debug("dram: single channel\n");
	for (int i = CONFIG_DRAM_SIZE >> 6; !(i & 1);
		i >>= 1, ddr_size_register++);
	/* TODO: Don't default to Rank0-only */
	writel(0x20040 | (ddr_size_register | 5 << 3),
		DMC_DDR_CTRL);
#error "TODO: ADDRMAP stuff here"
#elif CONFIG_DRAM_CHANNEL == 2 /* dual channel, ddr0 & ddr1 */
	/* TODO: Don't assume both channels are different */
	//reg = (4 << 20) | (0 << 16) | (1 << 6);
	reg =  (4 << 20); /* Enable rank 1 */
	reg |= (1 << 6);  /* Channel 0 only */
	/* Size */
	reg |= (0x5 << 3) | 0x4; /* Set to 1 GB for each channel */
	//reg |= (ddr_size_register | (ddr_size_register << 3));
	//reg |= (ddr_size_register | (5 << 3));
	writel(reg, DMC_DDR_CTRL);
	/* Address map */
	//writel(11 | 31 << 5 |  0 << 10 | 14 << 15 | 15 << 20 | 16 << 25, DDR0_ADDRMAP_1);
	//writel( 0 | 12 << 5 | 13 << 10 | 29 << 15 |  0 << 20 | 30 << 25, DDR0_ADDRMAP_4);
	//rank0+1 same:
	writel(( 11| 31 << 5 | 0 << 10 | 14 << 15 | 15 << 20 | 16 << 25 ), DDR0_ADDRMAP_1);
	writel(( 30| 12 << 5 | 13 << 10 | 29 << 15 | 0 << 20 | 0 << 25 ), DDR0_ADDRMAP_4);
	//ghidra:
	//writel( 0x20f703eb, DDR0_ADDRMAP_1);
	//writel( 0x3bbd6717, DDR0_ADDRMAP_3);
	//writel( 0x20f703eb, DDR1_ADDRMAP_1);
	//writel( 0x3bbd6717, DDR1_ADDRMAP_3);
	//writel( 0x3c0e358, DDR0_ADDRMAP_4);
#else
#error "CONFIG_DRAM_CHANNEL invalid"
#endif
	writel(0x440620, DMC_PCTL_LP_CTRL);
	writel((0x20 << 8) | 0x20, DDR0_APD_CTRL);
	writel(0x5, DDR0_CLK_CTRL);

	writel(0x11, DMC_AXI0_QOS_CTRL1);

	writel(0x0, DMC_SEC_RANGE_CTRL);
	writel(0x80000000, DMC_SEC_CTRL);
	writel(0x55555555, DMC_SEC_AXI_PORT_CTRL);
	writel(0x55555555, DMC_DEV_SEC_READ_CTRL);
	writel(0x55555555, DMC_DEV_SEC_WRITE_CTRL);
	writel(0x15, DMC_GE2D_SEC_CTRL);
	writel(0x5, DMC_PARSER_SEC_CTRL);
	writel(0xffffffff, DMC_VPU_SEC_CFG);
	writel(0x55555555, DMC_VPU_SEC_WRITE_CTRL);
	writel(0x55555555, DMC_VPU_SEC_READ_CTRL);
	writel(0xffffffff, DMC_VDEC_SEC_CFG);
	writel(0x55555555, DMC_VDEC_SEC_WRITE_CTRL);
	writel(0x55555555, DMC_VDEC_SEC_READ_CTRL);
	writel(0xffffffff, DMC_HCODEC_SEC_CFG);
	writel(0x55555555, DMC_HCODEC_SEC_WRITE_CTRL);
	writel(0x55555555, DMC_HCODEC_SEC_READ_CTRL);
	writel(0xffffffff, DMC_HEVC_SEC_CFG);
	writel(0x55555555, DMC_HEVC_SEC_WRITE_CTRL);
	writel(0x55555555, DMC_HEVC_SEC_READ_CTRL);

	writel(0xFFFF, DMC_REQ_CTRL);

	/* XXX: What's SCRATCH1? */
	writel(0xbaadf00d, 0xc1107d40);

	dmb(); /* SY */
	isb();

	debug("dram: memory controller init done\n");
	return;
}

void initialise_dram_pub_prepare(void)
{
	ulong start;
	debug("%s\n", __func__);

	/* Release reset of DLL */
	writel(0xffffffff, DMC_SOFT_RST);
	writel(0xffffffff, DMC_SOFT_RST1);

	/* Enable UPCTL and PUB clock */
	writel(0x550620, DMC_PCTL_LP_CTRL);
	writel(0xf, DDR0_SOFT_RESET);

	/* PHY initialisation */
	//writel(PUB_PTR0, DDR0_PUB_PTR0);
	//writel(PUB_PTR1, DDR0_PUB_PTR1);
	//writel(PUB_PTR3, DDR0_PUB_PTR3);
	//writel(PUB_PTR4, DDR0_PUB_PTR4);

	writel(0x49494949, DDR0_PUB_IOVCR0);
	writel(0x49494949, DDR0_PUB_IOVCR1);

	writel(PUB_ODTCR, DDR0_PUB_ODTCR);

	writel(PUB_MR0, DDR0_PUB_MR0);
	writel(PUB_MR1, DDR0_PUB_MR1);
	writel(PUB_MR2, DDR0_PUB_MR2);
	writel(PUB_MR3, DDR0_PUB_MR3);

	/* Configure SDRAM timing parameters */
	writel(PUB_DTPR0, DDR0_PUB_DTPR0);
	writel(PUB_DTPR1, DDR0_PUB_DTPR1);
	// ? writel(PUB_PGCR0, DDR0_PUB_PGCR0); ?
	writel(PUB_PGCR1, DDR0_PUB_PGCR1);
	writel(PUB_PGCR2 | (1 << 28), DDR0_PUB_PGCR2); // RANK01_DIFFERENT!!
	writel(PUB_PGCR3, DDR0_PUB_PGCR3);
	writel(PUB_DXCCR, DDR0_PUB_DXCCR);

	writel(PUB_DTPR2, DDR0_PUB_DTPR2);
	writel(PUB_DTPR3, DDR0_PUB_DTPR3);
	writel(PUB_DTCR, DDR0_PUB_DTCR);

	/* Wait for DLL lock */
	WAIT_FOR(DDR0_PUB_PGSR0);

	writel(0, DDR0_PUB_ACIOCR1);
	writel(0, DDR0_PUB_ACIOCR2);
	writel(0, DDR0_PUB_ACIOCR3);
	writel(0, DDR0_PUB_ACIOCR4);
	writel(0, DDR0_PUB_ACIOCR5);

	writel(0, DDR0_PUB_DX0GCR1);
	writel(0, DDR0_PUB_DX0GCR2);
	writel((1 << 10) | (2 << 12), DDR0_PUB_DX0GCR3);
	writel(0, DDR0_PUB_DX1GCR1);
	writel(0, DDR0_PUB_DX1GCR2);
	writel((1 << 10) | (2 << 12), DDR0_PUB_DX1GCR3);
	writel(0, DDR0_PUB_DX2GCR1);
	writel(0, DDR0_PUB_DX2GCR2);
	writel((1 << 10) | (2 << 12), DDR0_PUB_DX2GCR3);
	writel(0, DDR0_PUB_DX3GCR1);
	writel(0, DDR0_PUB_DX3GCR2);
	writel((1 << 10) | (2 << 12), DDR0_PUB_DX3GCR3);

	writel(PUB_DCR, DDR0_PUB_DCR);

	writel(PUB_DTAR, DDR0_PUB_DTAR0);
	writel(PUB_DTAR | 0x8, DDR0_PUB_DTAR1);
	writel(PUB_DTAR | 0x10, DDR0_PUB_DTAR2);
	writel(PUB_DTAR | 0x18, DDR0_PUB_DTAR3);

	writel(PUB_DSGCR, DDR0_PUB_DSGCR);

	/* Wait for the SDRAM to initialise */
	WAIT_FOR(DDR0_PUB_PGSR0);

	return;
}

void initialise_dram_pctl(void)
{
	ulong start;
	int ddr0_is_active, __maybe_unused ddr1_is_active;
	debug("%s\n", __func__);
	/* XXX: don't hardcode this (0x20006d) */
	ddr0_is_active = ((0x20006d >> 7) & 1) == 0;
	ddr1_is_active = ((0x20006d >> 6) & 1) == 0;

	if (ddr0_is_active) {
		writel(PCTL0_1US_PCK, DDR0_PCTL_TOGCNT1U);
		//writel(CONFIG_DRAM_CLK / 20, DDR0_PCTL_TOGCNT100N);
		writel(PCTL0_100NS_PCK, DDR0_PCTL_TOGCNT100N);
		writel(PCTL0_INIT_US, DDR0_PCTL_TINIT);
		writel(PCTL0_RSTH_US, DDR0_PCTL_TRSTH);
		//writel(20, DDR0_PCTL_TINIT);
		//writel(50, DDR0_PCTL_TRSTH);
		writel(PCTL0_MCFG | (CONFIG_DRAM_2T_MODE ? 8 : 0),
			DDR0_PCTL_MCFG);
		writel(PCTL0_MCFG1, DDR0_PCTL_MCFG1);
		udelay(500);

		WAIT_FOR(DDR0_PCTL_DFISTSTAT0);
		writel(1, DDR0_PCTL_POWCTL);
		WAIT_FOR(DDR0_PCTL_POWSTAT);

		/*
		 * Timings can be overidden in a board-specific file
		 * We also have default timings depending on other CONFIG_
		 * values.
		 */
		writel(timings.rfc, DDR0_PCTL_TRFC);
		writel(timings.refi_mddr3, DDR0_PCTL_TREFI_MEM_DDR3);
		writel(timings.mrd, DDR0_PCTL_TMRD);
		writel(timings.rp, DDR0_PCTL_TRP);
		writel(timings.al, DDR0_PCTL_TAL);
		writel(timings.cwl, DDR0_PCTL_TCWL);
		writel(timings.cl, DDR0_PCTL_TCL);
		writel(timings.ras, DDR0_PCTL_TRAS);
		writel(timings.rc, DDR0_PCTL_TRC);
		writel(timings.rcd, DDR0_PCTL_TRCD);
		writel(timings.rrd, DDR0_PCTL_TRRD);
		writel(timings.rtp, DDR0_PCTL_TRTP);
		writel(timings.wr, DDR0_PCTL_TWR);
		writel(timings.wtr, DDR0_PCTL_TWTR);
		writel(timings.exsr, DDR0_PCTL_TEXSR);
		writel(timings.xp, DDR0_PCTL_TXP);
		writel(timings.dqs, DDR0_PCTL_TDQS);
		writel(timings.rtw, DDR0_PCTL_TRTW);
		writel(timings.cksre, DDR0_PCTL_TCKSRE);
		writel(timings.cksrx, DDR0_PCTL_TCKSRX);
		writel(timings.mod, DDR0_PCTL_TMOD);
		writel(timings.cke, DDR0_PCTL_TCKE);
		writel(timings.cke + 1, DDR0_PCTL_TCKESR);
		writel(timings.zqcs, DDR0_PCTL_TZQCS);
		writel(timings.zqcl, DDR0_PCTL_TZQCL);
		writel(timings.xpdll, DDR0_PCTL_TXPDLL);
		writel(timings.zqcsi, DDR0_PCTL_TZQCSI);

		writel(PCTL0_SCFG, DDR0_PCTL_SCFG);
		writel(PCTL0_SCTL, DDR0_PCTL_SCTL);

		/* ????? */
		writel(0xdeadbeef, 0xc1107d40);
		writel(0x88776655, 0xc883c010);

		WAIT_FOR(DDR0_PCTL_STAT);

		writel(PCTL0_PPCFG, DDR0_PCTL_PPCFG);
		writel(PCTL0_DFISTCFG0, DDR0_PCTL_DFISTCFG0);
		writel(PCTL0_DFISTCFG1, DDR0_PCTL_DFISTCFG1);
		writel(PCTL0_DFITCTRLDELAY, DDR0_PCTL_DFITCTRLDELAY);
		writel(PCTL0_DFITPHYWRDATA, DDR0_PCTL_DFITPHYWRDATA);
		writel(PCTL0_DFITPHYWRLTA, DDR0_PCTL_DFITPHYWRLAT);
		writel(PCTL0_DFITRDDATAEN, DDR0_PCTL_DFITRDDATAEN);
		writel(PCTL0_DFITPHYRDLAT, DDR0_PCTL_DFITPHYRDLAT);
		writel(PCTL0_DFITDRAMCLKDIS, DDR0_PCTL_DFITDRAMCLKDIS);
		writel(PCTL0_DFITDRAMCLKEN, DDR0_PCTL_DFITDRAMCLKEN);
		writel(PCTL0_DFILPCFG0, DDR0_PCTL_DFILPCFG0);
		writel(PCTL0_DFITPHYUPDTYPE1, DDR0_PCTL_DFITPHYUPDTYPE1);
		writel(PCTL0_DFITCTRLUPDMIN, DDR0_PCTL_DFITCTRLUPDMIN);
		writel(PCTL0_DFIODTCFG, DDR0_PCTL_DFIODTCFG);
		writel(PCTL0_DFIODTCFG1, DDR0_PCTL_DFIODTCFG1);
		writel(PCTL0_CMDTSTATEN, DDR0_PCTL_CMDTSTATEN);
	}

	writel(PUB_ZQ0PR, DDR0_PUB_ZQ0PR);
	writel(PUB_ZQ1PR, DDR0_PUB_ZQ1PR);
	writel(PUB_ZQ2PR, DDR0_PUB_ZQ2PR);
	writel(PUB_ZQ3PR, DDR0_PUB_ZQ3PR);

	writel(3, DDR0_PUB_PIR);
	WAIT_FOR(DDR0_PUB_PGSR0);
	/*
	 * Is this needed?
	 * TODO(vitali64pmemail@protonmail.com): test without
	 */
	//setbits_32(DDR0_PUB_ZQCR, (1 << 2) | (1 << 27));
	writel(readl(DDR0_PUB_ZQCR) | (1 << 2) | (1 << 27), DDR0_PUB_ZQCR);
	udelay(10);
	//clrbits_32(DDR0_PUB_ZQCR, (1 << 2) | (1 << 27));
	writel(readl(DDR0_PUB_ZQCR) & ~((1 << 2) | (1 << 27)), DDR0_PUB_ZQCR);
	udelay(30);

	writel(PUB_ACBDLR0, DDR0_PUB_ACBDLR0);

	writel(0xfff3, DDR0_PUB_PIR);
	udelay(500);

	//writel(PUB_PIR | PUB_PIR_INIT, DDR0_PUB_PIR);
	while ((readl(DDR0_PUB_PGSR0) != 0xc0000fff) &&
		(readl(DDR0_PUB_PGSR0) != 0x80000fff)) {
		udelay(20);
		printf("Waiting for PGSR0, currently 0x%x\n", readl(DDR0_PUB_PGSR0));
	}
	printf("Wait done for PGSR0, currently 0x%x\n", readl(DDR0_PUB_PGSR0));
#if CONFIG_DRAM_CHANNEL == 1
#error Not implemented
#elif CONFIG_DRAM_CHANNEL == 2
	unsigned int i=0, j=0;
	i=(readl(DDR0_PUB_DX2LCDLR0));
	writel(((i>>8)|(i&(0xffffff00))), DDR0_PUB_DX2LCDLR0);
	i=(((readl(DDR0_PUB_DX2GTR))>>3)&((7<<0)));
	j=(((readl(DDR0_PUB_DX2GTR))>>14)&((3<<0)));
	writel(i|(i<<3)|(j<<12)|(j<<14), DDR0_PUB_DX2GTR);
	i=(readl(DDR0_PUB_DX2LCDLR2));
	writel(((i>>8)|(i&(0xffffff00))), DDR0_PUB_DX2LCDLR2);
	i=(readl(DDR0_PUB_DX3LCDLR0));
	writel(((i>>8)|(i&(0xffffff00))), DDR0_PUB_DX3LCDLR0);
	i=(((readl(DDR0_PUB_DX3GTR))>>3)&((7<<0)));
	j=(((readl(DDR0_PUB_DX3GTR))>>14)&((3<<0)));
	writel(i|(i<<3)|(j<<12)|(j<<14), DDR0_PUB_DX3GTR);
	i=(readl(DDR0_PUB_DX3LCDLR2));
	writel(((i>>8)|(i&(0xffffff00))), DDR0_PUB_DX3LCDLR2);
	i=(readl(DDR0_PUB_DX0LCDLR0));
	writel(((i<<8)|(i&(0xffff00ff))), DDR0_PUB_DX0LCDLR0);
	i=(((readl(DDR0_PUB_DX0GTR))<<0)&((7<<0)));
	j=(((readl(DDR0_PUB_DX0GTR))>>12)&((3<<0)));
	writel(i|(i<<3)|(j<<12)|(j<<14), DDR0_PUB_DX0GTR);
	i=(readl(DDR0_PUB_DX0LCDLR2));
	writel(((i<<8)|(i&(0xffff00ff))), DDR0_PUB_DX0LCDLR2);
	i=(readl(DDR0_PUB_DX1LCDLR0));
	writel(((i<<8)|(i&(0xffff00ff))), DDR0_PUB_DX1LCDLR0);
	i=(((readl(DDR0_PUB_DX1GTR))<<0)&((7<<0)));
	j=(((readl(DDR0_PUB_DX1GTR))>>12)&((3<<0)));
	writel(i|(i<<3)|(j<<12)|(j<<14), DDR0_PUB_DX1GTR);
	i=(readl(DDR0_PUB_DX1LCDLR2));
	writel(((i<<8)|(i&(0xffff00ff))), DDR0_PUB_DX1LCDLR2);

	/* TODO(vitali64pmemail@protonmail.com): see line 87 */
	//writel((~(1 << 28)) & PUB_PGCR2, DDR0_PUB_PGCR2);
	writel(PUB_PGCR2 & 0xefffffff, DDR0_PUB_PGCR2);
#endif

#if CONFIG_DRAM_2T_MODE && (PUB_DCR & 7) == 3
	writel(0x1f, DDR0_PUB_ACLCDLR);
#endif

	if (ddr0_is_active) {
		WAIT_FOR(DDR0_PCTL_CMDTSTAT);
		writel(2, DDR0_PCTL_SCTL); /* UPCTL_CMD_GO */
		while (readl(DDR0_PCTL_STAT) != 3) /* UPCTL_STAT_ACCESS */
			/* Do nothing */ ;
		writel(0x880019d, DMC_REFR_CTRL1);
		writel(0x20100000 | (CONFIG_DRAM_CLK / 20) | (timings.refi << 8), DMC_REFR_CTRL2);
		clrbits_32(DDR0_PUB_ZQCR, 4);
	}

	return;
}

int dram_init(void)
{
	debug("TPL: initialising dram\n");
	initialise_dram_pll();
	initialise_dram_pub_prepare();
	initialise_dram_pctl();
	initialise_dram_dmc();

	/* Write size */
	writel((readl(GX_AO_SEC_GP_CFG0) & 0x0000ffff) |
		(CONFIG_DRAM_SIZE << 16), GX_AO_SEC_GP_CFG0);

	debug("dram: %d MB\n", (1 << ((readl(DMC_DDR_CTRL) & 7) + 7)));
	debug("TPL: dram init done\n");
	unsigned int data, pattern;
	debug("TPL: ");
	for (pattern = 1; pattern != 0; pattern <<= 1) {
		*(volatile unsigned int *)(uint64_t)0x1000000 = pattern;
		debug("reading 0x1000000 returns\t");
		data = *(volatile unsigned int *)(uint64_t)0x1000000;
		debug("%d\n", data);
		*(volatile unsigned int *)(uint64_t)0x1000000 = pattern;
		debug("TPL: reading 0x1000000 returns\t");
		data = *(volatile unsigned int *)(uint64_t)0x1000000;
		debug("%d\n", data);
		if (data != pattern) {
			debug("Error: test failed. pattern 0x%x != data\n", pattern);
			for(;;)
				/* hang */ ;
		}
	}
	gd->ram_size = (u32)(CONFIG_DRAM_SIZE << 20);
	return 0;
}
