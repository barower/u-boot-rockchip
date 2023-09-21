// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2023, Ferass El Hafidi <vitali64pmemail@protonmail.com>
 */
#include <spl.h>
#include <asm/io.h>
#include <asm/spl.h>
#include <asm/arch/boot.h>
#include <image.h>
#include <vsprintf.h>
#include <linux/delay.h>

u32 spl_boot_device(void)
{
	int boot_device = meson_get_boot_device();

	switch (boot_device) {
	case BOOT_DEVICE_EMMC:
		return BOOT_DEVICE_MMC2;
	case BOOT_DEVICE_SD:
		return BOOT_DEVICE_MMC1;
	/*
	 * We don't support booting from Amlogic's proprietary USB protocol,
	 * and probably never will.
	 */
	case BOOT_DEVICE_USB:
		panic("USB boot not supported\n");
	}

	panic("Boot device %d not supported\n", boot_device);
	return BOOT_DEVICE_NONE;
}

/* To be defined in dram-${GENERATION}.c */
__weak int dram_init(void)
{
	debug("spl: Please define your own dram_init() function\n");
	return 0;
}

__weak const char *spl_board_loader_name(u32 boot_device)
{
	/* HACK: use proper name for: Trying to boot from <device> */
	switch (boot_device) {
	case BOOT_DEVICE_MMC1:
		return "SD";
	case BOOT_DEVICE_MMC2:
		return "eMMC";
	default:
		return NULL;
	}
}

__weak struct legacy_img_hdr *spl_get_load_buffer(ssize_t offset, size_t size)
{
	/* HACK: lets use first 4 KB of TZRAM until we have DRAM */
	if (!IS_ENABLED(CONFIG_MESON_GXBB) && size <= 0x1000)
		return (void*)CONFIG_SPL_TEXT_BASE - 0x1000;

	/* HACK: fall back on a DRAM address, @ 64 MB could work ? */
	return (void*)CONFIG_TEXT_BASE + 0x4000000;
}

__weak void *board_spl_fit_buffer_addr(ulong fit_size, int sectors, int bl_len)
{
	/* HACK: use same fit load buffer address as for mmc raw */
	return spl_get_load_buffer(0, fit_size);
}

__weak bool spl_load_simple_fit_skip_processing(void)
{
	if (IS_ENABLED(CONFIG_MESON_GXBB)) {
		/* Disable watchdog before processing FIT */
		clrbits_32(0xc11098d0, ((1<<18)|(1<<25)));

		return false;
	}

	/* HACK: skip fit processing, we do not have any DRAM */
	return true;
}

__weak void spl_board_prepare_for_boot(void)
{
	if (!IS_ENABLED(CONFIG_MESON_GXBB)) {
		/* HACK: stop trying to jump to 0x0 */
		panic("Nothing to do!\n");
	}
}

#if CONFIG_IS_ENABLED(FIT_IMAGE_POST_PROCESS)

#define     AO_SEC_SD_CFG15                                    (0xc8100000 + (0x8f << 2))
#define SEC_HIU_MAILBOX_SET_0                                  (0xda83c400 + (0x01 << 2))
#define SEC_HIU_MAILBOX_STAT_0                                 (0xda83c400 + (0x02 << 2))
#define SEC_HIU_MAILBOX_CLR_0                                  (0xda83c400 + (0x03 << 2))

#define MB_SRAM_BASE 0xd9013800

#define CMD_SHA         0xc0de0001
#define CMD_OP_SHA      0xc0de0002
#define CMD_DATA_LEN    0xc0dec0d0
#define CMD_DATA        0xc0dec0de
#define CMD_END         0xe00de00d

static void mb_send_data(uint32_t val, uint32_t port)
{
	unsigned long  base_addr = SEC_HIU_MAILBOX_SET_0;
	unsigned long  set_addr;

	if (port > 5) {
		printf("Error: Use the error port num!\n");
		return;
	}

	set_addr = base_addr + port*3*4;

	if (!val) {
		printf("Error: mailbox try to send zero val!\n");
		return;
	}

	writel(val, set_addr);

	return;
}

static uint32_t mb_read_data(uint32_t port)
{
	unsigned long base_addr = SEC_HIU_MAILBOX_STAT_0;
	uint32_t val;

	if (port > 5) {
		printf("Error: Use the error port num!\n");
		return 0;
	}

	val = readl(base_addr + port*3*4);

	if (val)
		return val;
	else {
//		print_out("Warning: read mailbox val=0.\n");
		return 0;
	}
}

static void send_bl30x(void *addr, size_t size, const uint8_t * sha2, \
	uint32_t sha2_length, const char * name)
{
	int i;
	*(unsigned int *)MB_SRAM_BASE = size;

	if (0 == strcmp("bl301", name)) {
		/*bl301 must wait bl30 run*/
		printf("Wait bl30...");
		while (0x3 != ((readl(AO_SEC_SD_CFG15) >> 20) & 0x3)) {}
		printf("Done\n");
	}

	printf("Sending ");
	printf(name);
	//printf("time=0x%x size=0x%x\n", readl(0xc1109988),size);

	mb_send_data(CMD_DATA_LEN, 3);
	do {} while(mb_read_data(3));
	memcpy((void *)MB_SRAM_BASE, (const void *)sha2, sha2_length);
	mb_send_data(CMD_SHA, 3);
	do {} while(mb_read_data(3));

	for (i = 0; i < size; i+=1024) {
		printf(".");
		if (size >= i + 1024)
			memcpy((void *)MB_SRAM_BASE,(const void *)(unsigned long)(addr+i),1024);
		else if(size > i)
			memcpy((void *)MB_SRAM_BASE,(const void *)(unsigned long)(addr+i),(size-i));

		mb_send_data(CMD_DATA, 3);
		do {} while(mb_read_data(3));
	}
	mb_send_data(CMD_OP_SHA, 3);

	do {} while(mb_read_data(3));
	printf("OK. \nRun ");
	printf(name);
	printf("...\n");

	/* The BL31 will run after this command */
	mb_send_data(CMD_END,3);//code transfer end.
}

void board_fit_image_post_process(const void *fit, int node, void **p_image, size_t *p_size)
{
	const char *name = fit_get_name(fit, node, NULL);
	int noffset = 0, value_len;
	uint8_t *value;

	if (strcmp("bl30", name) && strcmp("bl301", name))
		return;

	fdt_for_each_subnode(noffset, fit, node) {
		if (strncmp(fit_get_name(fit, noffset, NULL), FIT_HASH_NODENAME, strlen(FIT_HASH_NODENAME)))
			continue;

		if (fit_image_hash_get_value(fit, noffset, &value, &value_len))
			continue;

		send_bl30x(*p_image, *p_size, value, value_len, name);
		break;
	}
}
#endif

static int pwm_voltage_table[][2] = {
	{ 0x1c0000,  860},
	{ 0x1b0001,  870},
	{ 0x1a0002,  880},
	{ 0x190003,  890},
	{ 0x180004,  900},
	{ 0x170005,  910},
	{ 0x160006,  920},
	{ 0x150007,  930},
	{ 0x140008,  940},
	{ 0x130009,  950},
	{ 0x12000a,  960},
	{ 0x11000b,  970},
	{ 0x10000c,  980},
	{ 0x0f000d,  990},
	{ 0x0e000e, 1000},
	{ 0x0d000f, 1010},
	{ 0x0c0010, 1020},
	{ 0x0b0011, 1030},
	{ 0x0a0012, 1040},
	{ 0x090013, 1050},
	{ 0x080014, 1060},
	{ 0x070015, 1070},
	{ 0x060016, 1080},
	{ 0x050017, 1090},
	{ 0x040018, 1100},
	{ 0x030019, 1110},
	{ 0x02001a, 1120},
	{ 0x01001b, 1130},
	{ 0x00001c, 1140}
};

#define P_PIN_MUX_REG3		(*((volatile unsigned *)(0xda834400 + (0x2f << 2))))
#define P_PIN_MUX_REG7		(*((volatile unsigned *)(0xda834400 + (0x33 << 2))))

#define P_PWM_MISC_REG_AB	(*((volatile unsigned *)(0xc1100000 + (0x2156 << 2))))
#define P_PWM_PWM_B		(*((volatile unsigned *)(0xc1100000 + (0x2155 << 2))))
#define P_PWM_MISC_REG_CD	(*((volatile unsigned *)(0xc1100000 + (0x2196 << 2))))
#define P_PWM_PWM_D		(*((volatile unsigned *)(0xc1100000 + (0x2195 << 2))))

enum pwm_id {
    pwm_a = 0,
    pwm_b,
    pwm_c,
    pwm_d,
    pwm_e,
    pwm_f,
};

static void pwm_init(int id)
{
	unsigned int reg;

	/*
	 * TODO: support more pwm controllers, right now only support
	 * PWM_B, PWM_D
	 */

	switch (id) {
	case pwm_b:
		reg = P_PWM_MISC_REG_AB;
		reg &= ~(0x7f << 16);
		reg |=  ((1 << 23) | (1 << 1));
		P_PWM_MISC_REG_AB = reg;
		/*
		 * default set to max voltage
		 */
		P_PWM_PWM_B = pwm_voltage_table[ARRAY_SIZE(pwm_voltage_table) - 1][0];
		reg  = P_PIN_MUX_REG7;
		reg &= ~(1 << 22);
		P_PIN_MUX_REG7 = reg;

		reg  = P_PIN_MUX_REG3;
		reg &= ~(1 << 22);
		reg |=  (1 << 21);		// enable PWM_B
		P_PIN_MUX_REG3 = reg;
		break;

	case pwm_d:
		reg = P_PWM_MISC_REG_CD;
		reg &= ~(0x7f << 16);
		reg |=  ((1 << 23) | (1 << 1));
		P_PWM_MISC_REG_CD = reg;
		/*
		 * default set to max voltage
		 */
		P_PWM_PWM_D = pwm_voltage_table[ARRAY_SIZE(pwm_voltage_table) - 1][0];
		reg  = P_PIN_MUX_REG7;
		reg &= ~(1 << 23);
		P_PIN_MUX_REG7 = reg;

		reg  = P_PIN_MUX_REG3;
		reg |=  (1 << 20);		// enable PWM_D
		P_PIN_MUX_REG3 = reg;
		break;
	default:
		break;
	}

	udelay(200);
}

static void pwm_set_voltage(unsigned int id, unsigned int voltage)
{
	int to;

	for (to = 0; to < ARRAY_SIZE(pwm_voltage_table); to++) {
		if (pwm_voltage_table[to][1] >= voltage) {
			break;
		}
	}
	if (to >= ARRAY_SIZE(pwm_voltage_table)) {
		to = ARRAY_SIZE(pwm_voltage_table) - 1;
	}
	switch (id) {
	case pwm_b:
		P_PWM_PWM_B = pwm_voltage_table[to][0];
		break;

	case pwm_d:
		P_PWM_PWM_D = pwm_voltage_table[to][0];
		break;
	default:
		break;
	}
	udelay(200);
}

#define CONFIG_VCCK_INIT_VOLTAGE 1100
#define CONFIG_VDDEE_INIT_VOLTAGE 1050

static void power_init(void)
{
	pwm_init(pwm_b);
	pwm_init(pwm_d);
	printf("set vcck to %d mv\n", CONFIG_VCCK_INIT_VOLTAGE);
	pwm_set_voltage(pwm_b, CONFIG_VCCK_INIT_VOLTAGE);
	printf("set vddee to %d mv\n", CONFIG_VDDEE_INIT_VOLTAGE);
	pwm_set_voltage(pwm_d, CONFIG_VDDEE_INIT_VOLTAGE);
}

#define HHI_SYS_CPU_CLK_CNTL1		(0xc883c000 + (0x57 << 2))
#define HHI_MPEG_CLK_CNTL			(0xc883c000 + (0x5d << 2))
#define HHI_SYS_CPU_CLK_CNTL		(0xc883c000 + (0x67 << 2))
#define HHI_MPLL_CNTL6				(0xc883c000 + (0xa5 << 2))
#define     HHI_SYS_PLL_CNTL                                   (0xc883c000 + (0xc0 << 2))
#define     HHI_SYS_PLL_CNTL2                                  (0xc883c000 + (0xc1 << 2))
#define     HHI_SYS_PLL_CNTL3                                  (0xc883c000 + (0xc2 << 2))
#define     HHI_SYS_PLL_CNTL4                                  (0xc883c000 + (0xc3 << 2))
#define     HHI_SYS_PLL_CNTL5                                  (0xc883c000 + (0xc4 << 2))
#define     HHI_MPLL_CNTL                                      (0xc883c000 + (0xa0 << 2))
#define     HHI_MPLL_CNTL2                                     (0xc883c000 + (0xa1 << 2))
#define     HHI_MPLL_CNTL3                                     (0xc883c000 + (0xa2 << 2))
#define     HHI_MPLL_CNTL4                                     (0xc883c000 + (0xa3 << 2))
#define     HHI_MPLL_CNTL5                                     (0xc883c000 + (0xa4 << 2))
#define     HHI_MPLL_CNTL6                                     (0xc883c000 + (0xa5 << 2))
#define     HHI_MPLL_CNTL7                                     (0xc883c000 + (0xa6 << 2))
#define     HHI_MPLL_CNTL8                                     (0xc883c000 + (0xa7 << 2))
#define     HHI_MPLL_CNTL9                                     (0xc883c000 + (0xa8 << 2))
#define     HHI_MPLL_CNTL10                                    (0xc883c000 + (0xa9 << 2))

static void clocks_set_sys_cpu_clk(uint32_t freq, uint32_t pclk_ratio, uint32_t aclkm_ratio, uint32_t atclk_ratio )
{
	uint32_t	control = 0;
	uint32_t	dyn_pre_mux = 0;
	uint32_t	dyn_post_mux = 0;
	uint32_t	dyn_div = 0;

	// Make sure not busy from last setting and we currently match the last setting
	do {
		control = readl(HHI_SYS_CPU_CLK_CNTL);
	} while( (control & (1 << 28)) );

	control = control | (1 << 26);				// Enable

	// Switching to System PLL...just change the final mux
	if ( freq == 1 ) {
		// wire			cntl_final_mux_sel		= control[11];
		control = control | (1 << 11);
	} else {
		switch ( freq ) {
			case	0:		// If Crystal
							dyn_pre_mux		= 0;
							dyn_post_mux	= 0;
							dyn_div			= 0;	// divide by 1
							break;
			case	1000:	// fclk_div2
							dyn_pre_mux		= 1;
							dyn_post_mux	= 0;
							dyn_div			= 0;	// divide by 1
							break;
			case	667:	// fclk_div3
							dyn_pre_mux		= 2;
							dyn_post_mux	= 0;
							dyn_div			= 0;	// divide by 1
							break;
			case	500:	// fclk_div2/2
							dyn_pre_mux		= 1;
							dyn_post_mux	= 1;
							dyn_div			= 1;	// Divide by 2
							break;
			case	333:	// fclk_div3/2
							dyn_pre_mux		= 2;
							dyn_post_mux	= 1;
							dyn_div			= 1;	// divide by 2
							break;
			case	250:	// fclk_div2/4
							dyn_pre_mux		= 1;
							dyn_post_mux	= 1;
							dyn_div			= 3;	// divide by 4
							break;
		}
		if ( control & (1 << 10) ) { 	// if using Dyn mux1, set dyn mux 0
			// Toggle bit[10] indicating a dynamic mux change
			control = (control & ~((1 << 10) | (0x3f << 4)	| (1 << 2)	| (0x3 << 0)))
					| ((0 << 10)
					| (dyn_div << 4)
					| (dyn_post_mux << 2)
					| (dyn_pre_mux << 0));
		} else {
			// Toggle bit[10] indicating a dynamic mux change
			control = (control & ~((1 << 10) | (0x3f << 20) | (1 << 18) | (0x3 << 16)))
					| ((1 << 10)
					| (dyn_div << 20)
					| (dyn_post_mux << 18)
					| (dyn_pre_mux << 16));
		}
		// Select Dynamic mux
		control = control & ~(1 << 11);
	}
	writel(control, HHI_SYS_CPU_CLK_CNTL);
	//
	// Now set the divided clocks related to the System CPU
	//
	// This function changes the clock ratios for the
	// PCLK, ACLKM (AXI) and ATCLK
	//		.clk_clken0_i	( {clk_div2_en,clk_div2}	),
	//		.clk_clken1_i	( {clk_div3_en,clk_div3}	),
	//		.clk_clken2_i	( {clk_div4_en,clk_div4}	),
	//		.clk_clken3_i	( {clk_div5_en,clk_div5}	),
	//		.clk_clken4_i	( {clk_div6_en,clk_div6}	),
	//		.clk_clken5_i	( {clk_div7_en,clk_div7}	),
	//		.clk_clken6_i	( {clk_div8_en,clk_div8}	),

	uint32_t	control1 = readl(HHI_SYS_CPU_CLK_CNTL1);

	//		.cntl_PCLK_mux				( hi_sys_cpu_clk_cntl1[5:3]	 ),
	if ( (pclk_ratio >= 2) && (pclk_ratio <= 8) ) { control1 = (control1 & ~(0x7 << 3)) | ((pclk_ratio-2) << 3) ; }
	//		.cntl_ACLKM_clk_mux		 ( hi_sys_cpu_clk_cntl1[11:9]	),	// AXI matrix
	if ( (aclkm_ratio >= 2) && (aclkm_ratio <= 8) ) { control1 = (control1 & ~(0x7 << 9)) | ((aclkm_ratio-2) << 9) ; }
	//		.cntl_ATCLK_clk_mux		 ( hi_sys_cpu_clk_cntl1[8:6]	 ),
	if ( (atclk_ratio >= 2) && (atclk_ratio <= 8) ) { control1 = (control1 & ~(0x7 << 6)) | ((atclk_ratio-2) << 6) ; }
	writel(control1, HHI_SYS_CPU_CLK_CNTL1);
}

unsigned lock_check_loop = 0;

unsigned pll_lock_check(unsigned long pll_reg, const char *pll_name){
	/*locked: return 0, else return 1*/
	unsigned lock = ((readl(pll_reg) >> 31) & 0x1);
	if (lock) {
		lock_check_loop = 0;
	}
	else{
		lock_check_loop++;
		printf("%s lock check %u\n", pll_name, lock_check_loop);
	}
	return !lock;
}

static void pll_init(void)
{
	clrbits_32(HHI_MPEG_CLK_CNTL, 1 << 8);
	clocks_set_sys_cpu_clk(0, 0, 0, 0);

	setbits_32(HHI_MPLL_CNTL6, 1 << 26);
	udelay(100);

	unsigned int sys_pll_cntl = 0;
	sys_pll_cntl = (0<<16/*OD*/) | (1<<9/*N*/) | (1536 / 24/*M*/);
	do {
		setbits_32(HHI_SYS_PLL_CNTL, 1 << 29);
		writel(0x5ac80000, HHI_SYS_PLL_CNTL2);
		writel(0x8e452015, HHI_SYS_PLL_CNTL3);
		writel(0x0401d40c, HHI_SYS_PLL_CNTL4);
		writel(0x00000870, HHI_SYS_PLL_CNTL5);
		writel(((1<<30)|(1<<29)|sys_pll_cntl), HHI_SYS_PLL_CNTL); // A9 clock
		clrbits_32(HHI_SYS_PLL_CNTL, 1 << 29);
		udelay(20);
	} while (pll_lock_check(HHI_SYS_PLL_CNTL, "SYS PLL"));
	clocks_set_sys_cpu_clk( 1, 0, 0, 0); // Connect SYS CPU to the PLL divider output

	sys_pll_cntl = readl(HHI_SYS_PLL_CNTL);
	unsigned cpu_clk = (24/ \
		((sys_pll_cntl>>9)&0x1F)* \
		(sys_pll_cntl&0x1FF)/ \
		(1<<((sys_pll_cntl>>16)&0x3)));
	/* cpu clk = 24/N*M/2^OD */
	printf("CPU clk: %dMHz\n", cpu_clk);

	writel(0x00010007, HHI_MPLL_CNTL4);
	setbits_32(HHI_MPLL_CNTL, 1 << 29);
	udelay(200);
	writel(0x59C80000, HHI_MPLL_CNTL2);
	writel(0xCA45B822, HHI_MPLL_CNTL3);
	writel(0xB5500E1A, HHI_MPLL_CNTL5);
	writel(0xFC454545, HHI_MPLL_CNTL6);
	writel(((1 << 30) | (1<<29) | (3 << 9) | (250 << 0)), HHI_MPLL_CNTL);
	clrbits_32(HHI_MPLL_CNTL, 1 << 29);
	udelay(800);
	setbits_32(HHI_MPLL_CNTL4, 1 << 14);
	do {
		if ((readl(HHI_MPLL_CNTL)&(1<<31)) != 0)
			break;
		setbits_32(HHI_MPLL_CNTL, 1 << 29);
		udelay(1000);
		clrbits_32(HHI_MPLL_CNTL, 1 << 29);
		udelay(1000);
	} while (pll_lock_check(HHI_MPLL_CNTL, "FIX PLL"));

	writel(0xFFF << 16, HHI_MPLL_CNTL10);
	writel(((7 << 16) | (1 << 15) | (1 << 14) | (4681 << 0)), HHI_MPLL_CNTL7);
	writel(((readl(HHI_MPEG_CLK_CNTL) & (~((0x7 << 12) | (1 << 7) | (0x7F << 0)))) | ((5 << 12) | (1 << 7)	| (2 << 0))), HHI_MPEG_CLK_CNTL);
	setbits_32(HHI_MPEG_CLK_CNTL, 1 << 8);
	writel(((5 << 16) | (1 << 15) | (1 << 14) | (12524 << 0)), HHI_MPLL_CNTL8);

	udelay(200);
}

void board_init_f(ulong dummy)
{
	int ret;

	/* BL1 doesn't append a newline char after the end of its output. */
	putc('\n');

	icache_enable();

	if (CONFIG_IS_ENABLED(OF_CONTROL)) {
		ret = spl_early_init();
		if (ret) {
			panic("spl_early_init() failed: %d\n", ret);
			return;
		}
	}

	preloader_console_init();

	if (IS_ENABLED(CONFIG_MESON_GXBB)) {
		power_init();
		pll_init();
	}

	ret = dram_init();
	if (ret) {
		panic("dram_init() failed: %d\n", ret);
		return;
	}
}
