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

#pragma once

#include <stdint.h>
#include <stdbool.h>

struct gm_boot_snapshot {
	bool valid;
	int reset_cause_ret;
	uint32_t reset_cause;
	int supported_ret;
	uint32_t supported_reset_cause;
	int clear_ret;
	int main_button_latch;
	int reed_latch;
};

const struct gm_boot_snapshot *gm_boot_snapshot_get(void);
bool gm_boot_snapshot_was_system_off_gpio(void);
bool gm_boot_snapshot_was_watchdog(void);
bool gm_boot_snapshot_was_software(void);
