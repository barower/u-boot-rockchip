// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2023, Ferass El Hafidi <vitali64pmemail@protonmail.com>
 */
#include <asm/spl.h>
#include <asm/arch/boot.h>
#include <image.h>
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
	if (size <= 0x1000)
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
	/* HACK: skip fit processing, we do not have any DRAM */
	return true;
}

__weak void spl_board_prepare_for_boot(void)
{
	/* HACK: stop trying to jump to 0x0 */
	panic("Nothing to do!\n");
}
