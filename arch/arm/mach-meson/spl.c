// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2023, Ferass El Hafidi <vitali64pmemail@protonmail.com>
 */
#include <asm/spl.h>
#include <asm/arch/boot.h>
#include <vsprintf.h>

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
	return BOOT_DEVICE_RESERVED;
}
