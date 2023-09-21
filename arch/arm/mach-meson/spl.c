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
	}

	ret = dram_init();
	if (ret) {
		panic("dram_init() failed: %d\n", ret);
		return;
	}
}
