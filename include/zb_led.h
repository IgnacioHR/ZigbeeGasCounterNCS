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

#ifndef ZB_LED_H
#define ZB_LED_H

#if DT_NODE_HAS_STATUS(DT_ALIAS(xiao_led_red), okay)
#define HAS_XIAO_RGB_LED 1
#else
#define HAS_XIAO_RGB_LED 0
#endif

#if HAS_XIAO_RGB_LED && IS_ENABLED(CONFIG_DEBUG)
int xiao_leds_init(void);
void xiao_led_red_set(bool on);
void xiao_led_green_set(bool on);
void xiao_led_blue_set(bool on);
#endif

#endif // ZB_LED_H