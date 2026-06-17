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

#ifndef ZB_MAIN_LOOP_H
#define ZB_MAIN_LOOP_H

#include <zephyr/kernel.h>
#include "zb_features.h"

enum mainloop_events {
#ifdef FEATURE_MEASURE_BATTERY_LEVEL
	SHALL_MEASURE_BATTERY           = BIT(0),
#endif
	SHALL_ENABLE_ZIGBEE             = BIT(1),

// this is not implemented because of lack of support from esp-zigbee-sdk
// see https://github.com/espressif/esp-zigbee-sdk/issues/561
// and also not implemented in nRF52840 due to lack of interest, at this time
// we poweroff the device in order to shut down zigbee radio
	SHALL_DISABLE_ZIGBEE            = BIT(2),
	
	SHALL_CONFIRM_OTA								= BIT(3),
};

#ifdef FEATURE_MEASURE_BATTERY_LEVEL
// 1 hour in seconds
#define MEASURE_BATTERY_VOLTAGE_TIME		(60 * 60)
#endif


void main_loop_post(uint32_t task);
bool is_main_loop_started(void);

#endif // ZB_MAIN_LOOP