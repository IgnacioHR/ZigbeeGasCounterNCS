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

#ifndef ZB_REED_H
#define ZB_REED_H

int reed_gpio_init(int *level);
int reed_get_level(void);
int reed_gpio_wakeup(void);
void check_counter_increment(void);
int reed_read_early_level(void);
int reed_read_stable_level(void);

#ifdef CONFIG_DEBUG
void reed_led_on();
void reed_led_off();
#endif

#endif // Z_REED