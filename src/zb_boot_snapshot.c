/*
 * Zigbee Gas Counter - An open-source Zigbee gas counter project.
 * Copyright (c) 2026 Ignacio Hernández-Ros.
 *
 * This work is licensed under the Creative Commons Attribution-NonCommercial-ShareAlike 4.0
 * International License. To view a copy of this license, visit
 * https://creativecommons.org/licenses/by-nc-sa/4.0/
 *
 * You may use, modify, and share this work for personal and non-commercial purposes, as long
 * as you credit the original author(s) and share any derivatives under the same license.
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(boot_snapshot, LOG_LEVEL_INF);

#include "zb_boot_snapshot.h"
#include "zb_main_button.h"
#include "zb_reed.h"

static struct gm_boot_snapshot boot_snapshot = {
	.valid = false,
	.reset_cause_ret = -ENODATA,
	.supported_ret = -ENODATA,
	.clear_ret = -ENODATA,
	.main_button_latch = -1,
	.reed_latch = -1,
};

/**
 * @brief Gives access to the boot_snapshot report collected early during boot
 * 
 * @return const struct gm_boot_snapshot* 
 */
const struct gm_boot_snapshot *gm_boot_snapshot_get(void)
{
	return &boot_snapshot;
}

static int gm_boot_snapshot_init(void)
{
	int ret;

	ret = hwinfo_get_reset_cause(&boot_snapshot.reset_cause);
	boot_snapshot.reset_cause_ret = ret;

	ret = hwinfo_get_supported_reset_cause(
		&boot_snapshot.supported_reset_cause);
	boot_snapshot.supported_ret = ret;

	boot_snapshot.valid = false;
	uint32_t boot_latch = NRF_P0->LATCH;
	boot_snapshot.main_button_latch = (boot_latch & BIT(2)) == BIT(2) ? 1 : 0;
	boot_snapshot.reed_latch = (boot_latch & BIT(3)) == BIT(3) ? 1 : 0;
	NRF_P0->LATCH = boot_latch;
	boot_snapshot.clear_ret = hwinfo_clear_reset_cause();
	boot_snapshot.valid = true;

	return 0;
}
SYS_INIT(gm_boot_snapshot_init, POST_KERNEL, CONFIG_GAS_DEVICE_INIT_PRIORITY);

bool gm_boot_snapshot_was_system_off_gpio(void)
{
	return boot_snapshot.valid &&
	       boot_snapshot.reset_cause_ret == 0 &&
	       (boot_snapshot.reset_cause & RESET_LOW_POWER_WAKE);
}

bool gm_boot_snapshot_was_watchdog(void)
{
	return boot_snapshot.valid &&
	       boot_snapshot.reset_cause_ret == 0 &&
	       (boot_snapshot.reset_cause & RESET_WATCHDOG);
}

bool gm_boot_snapshot_was_software(void)
{
	return boot_snapshot.valid &&
	       boot_snapshot.reset_cause_ret == 0 &&
	       (boot_snapshot.reset_cause & RESET_SOFTWARE);
}
