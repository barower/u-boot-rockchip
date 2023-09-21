// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023, Ferass El Hafidi <vitali64pmemail@protonmail.com>
 */
#ifndef DRAM_SETTINGS_GX_H
#define DRAM_SETTINGS_GX_H
#include <linux/bitops.h>
#include <asm/arch/dram-gx.h>

struct meson_gx_dram_timings timings = {
	.drv        = 0,
	.odt        = 2,

	/* Timings */
	.rtp        = 0x7,
	.wtr        = 0x7,
	.rp         = 0xd,
	.rcd        = 0xd,
	.ras        = 0x25,
	.rrd        = 0x7,
	.rc         = 0x34,
	.mrd        = 0x6, /* Should be < 8 */
	.mod        = 0x4,
	.faw        = 0x21,
	.wlmrd      = 0x28,
	.wlo        = 0x7,
	.rfc        = 0x118,
	.xp         = 0x7,
	.xs         = 0x200,
	.dllk       = 0x200,
	.cke        = 0x5,
	.rtodt      = 0x0,
	.rtw        = 0x7,
	.refi       = 0x4e,
	.refi_mddr3 = 0x4,
	.cl         = 0xd,
	.wr         = 0x10,
	.cwl        = 0x9,
	.al         = 0x0,
	.dqs        = 0x17,
	.cksre      = 0xf,
	.cksrx      = 0xf,
	.zqcs       = 0x40,
	.xpdll      = 0x17,
	.exsr       = 0x200,
	.zqcl       = 0x88,
	.zqcsi      = 0x3e8,
	.rpab       = 0x0,
	.rppb       = 0x0,
	.tdqsck     = 0x0,
	.tdqsckmax  = 0x0,
	.tckesr     = 0x0,
	.tdpd       = 0x0,
	.taond_aofd = 0x0
};

/*
 * These registers are pretty similar to other DRAM registers found in
 * Allwinner A31/sun6i. Some of these registers also exist in some Rockchip
 * SoCs and the TI KeyStone3.
 */
/* Mode Register */
#define PUB_MR0                (((timings.cl - 4) & 8) >> 1) | \
	(((timings.cl - 4) & 7) << 4) | \
	(((timings.wr <= 8 ? (timings.wr - 4) : (timings.wr >> 1)) & 7) << 9) | 0x1c00
#define PUB_MR1                (timings.drv << 1) | \
	((timings.odt & 1) << 2)        | \
	(((timings.odt >> 1) & 1) << 6) | \
	(((timings.odt >> 2) & 1) << 9) | \
	(1 << 7)                        | \
	((timings.al ? ((timings.cl - timings.al) & 3) : 0) << 3)
#define PUB_MR2                (1 << 6) | (((timings.cwl - 5) & 7) << 3)
#define PUB_MR3                0 /* Magic stuff performed by AmlBL2 */

/* ODT Configuration Register */
#define PUB_ODTCR              0x210000

/* DDR Timing Parameter? */
#define PUB_DTPR0              timings.rtp | \
	(timings.wtr << 4)  | \
	(timings.rp  << 8)  | \
	(timings.ras << 16) | \
	(timings.rrd << 22) | \
	(timings.rcd << 26)
#define PUB_DTPR1              (timings.mod << 2) | \
	(timings.faw << 5)    | \
	(timings.rfc << 11)   | \
	(timings.wlmrd << 20) | \
	(timings.wlo << 26)
#define PUB_DTPR2              timings.xs | \
	(timings.xp << 10)   | \
	(timings.dllk << 19)
#define PUB_DTPR3              0 | \
	(0 << 3)            | \
	(timings.rc << 6)   | \
	(timings.cke << 13) | \
	(timings.mrd << 18) | \
	(0 << 29)

/* PHY General Control Register? */
#define PUB_PGCR0              0x7D81E3F
#define PUB_PGCR1              0x380C6A0
#define PUB_PGCR2              (0x1F12480 & 0xefffffff)
#define PUB_PGCR3              0xC0AAFE60

#define PUB_DXCCR              0x181884
#define PUB_DTCR               0x43003087
#define PUB_DCR                0xB
#define PUB_DTAR               (0 | (0 << 12) | (0 << 28))
#define PUB_DSGCR              0x20645A

#define PUB_ZQ0PR              0x69
#define PUB_ZQ1PR              0x69
#define PUB_ZQ2PR              0x69
#define PUB_ZQ3PR              0x69

#define PCTL0_1US_PCK          0x1C8
#define PCTL0_100NS_PCK        0x2D
#define PCTL0_INIT_US          0x2
#define PCTL0_RSTH_US          0x2

/* Mode Config(?) */
#define PCTL0_MCFG             ((((timings.faw + timings.rrd - 1) / timings.rrd) & 3) << 0x12) | (0xa2f21 & 0xfff3ffff)
#define PCTL0_MCFG1            (((timings.rrd - ((timings.faw - (timings.faw / timings.rrd) * timings.rrd) & 0xff)) & 7) << 8) | (0x80200000 & 0xfffffcff)

#define PCTL0_SCFG             0xF01
#define PCTL0_SCTL             0x1
#define PCTL0_PPCFG            0x1e0

#define PCTL0_DFISTCFG0        0x4
#define PCTL0_DFISTCFG1        0x1

#define PCTL0_DFITCTRLDELAY    0x2
#define PCTL0_DFITPHYWRDATA    0x1
#define PCTL0_DFITPHYWRLTA     (timings.cwl + timings.al - \
	(((timings.cwl + timings.al) % 2) ? 3 : 4)) / 2
#define PCTL0_DFITRDDATAEN     (timings.cl + timings.al - \
	(((timings.cl + timings.al ) % 2) ? 3 : 4)) / 2
#define PCTL0_DFITPHYRDLAT     ((timings.cl + timings.al) % 2) ? 14 : 16
#define PCTL0_DFITDRAMCLKDIS   0x1
#define PCTL0_DFITDRAMCLKEN    0x1
#define PCTL0_DFITPHYUPDTYPE1  0x200
#define PCTL0_DFITCTRLUPDMIN   16

#define PCTL0_CMDTSTATEN       0x1

#define PCTL0_DFIODTCFG        0x8
#define PCTL0_DFIODTCFG1       (0 | (6 << 16))
#define PCTL0_DFILPCFG0        (1 | (3 << 4) | (1 << 8) | (3 << 12) | \
	(7 << 16) | (1 << 24) | ( 3 << 28))

#define PUB_ACBDLR0            0x10
//#define PUB_MR11               0x2

#define LPDDR3_CA0             0x2
#define LPDDR3_CA1             0x0
#define LPDDR3_REMAP           0x3
#define LPDDR3_WL              0x1

/* PHY Init Register */
#define PUB_PIR_INIT           (1<<0)
#define PUB_PIR_ZCAL						(1<<1)
#define PUB_PIR_CA             (1<<2)

#define PUB_PIR_PLLINIT        (1<<4)
#define PUB_PIR_DCAL           (1<<5)
#define PUB_PIR_PHYRST         (1<<6)
#define PUB_PIR_DRAMRST            (1<<7)
#define PUB_PIR_DRAMINIT					(1<<8)
#define PUB_PIR_WL             (1<<9)
#define PUB_PIR_QSGATE             (1<<10)
#define PUB_PIR_WLADJ             (1<<11)
#define PUB_PIR_RDDSKW             (1<<12)
#define PUB_PIR_WRDSKW             (1<<13)
#define PUB_PIR_RDEYE             (1<<14)
#define PUB_PIR_WREYE             (1<<15)


#define PUB_PIR_ICPC             (1<<16)
#define PUB_PIR_PLLBYP             (1<<17)
#define PUB_PIR_CTLDINIT             (1<<18)
#define PUB_PIR_RDIMMINIT             (1<<19)
#define PUB_PIR_CLRSR             (1<<27)
#define PUB_PIR_LOCKBYP             (1<<28)
#define PUB_PIR_DCALBYP             (1<<29)
#define PUB_PIR_ZCALBYP             (1<<30)
#define PUB_PIR_INITBYP             (1<<31)

#define PUB_PIR                0x10 | 0x20 | 0x40 | 0x80 | 0x100 | 0x200 | \
	0x400 | 0x800 | 0x1000 | 0x2000 | 0x4000 | 0x8000
#define DDR_PIR ((PUB_PIR_ZCAL) 		|\
				(PUB_PIR_PLLINIT) 		|\
				(PUB_PIR_DCAL) 			|\
				(PUB_PIR_PHYRST)		|\
				(PUB_PIR_DRAMRST)		|\
				(PUB_PIR_DRAMINIT)		|\
				(PUB_PIR_WL)			|\
				(PUB_PIR_QSGATE)		|\
				(PUB_PIR_WLADJ)			|\
				(PUB_PIR_RDDSKW)		|\
				(PUB_PIR_WRDSKW)		|\
				(PUB_PIR_RDEYE)			|\
				(PUB_PIR_WREYE)			 \
				)

#endif /* DRAM_SETTINGS_GX_H */
