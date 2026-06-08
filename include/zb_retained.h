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

#ifndef ZB_RETAINED_H
#define ZB_RETAINED_H

#include <zephyr/kernel.h>

#define REED_DEBOUNCE_TIMEOUT_MS 150

int retained_init(void);
int retained_set_next_reed_level(uint16_t level);
uint16_t retained_get_next_reed_level(void);
int retained_set_boot_to_system_off(bool value);
bool retained_shall_power_off();
void retained_set_last_summation_sent(uint64_t value);
uint64_t retained_get_last_summation_sent(void);

#endif // ZB_RETAINED_H