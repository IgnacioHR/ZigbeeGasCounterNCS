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

// Main group events
#ifdef FEATURE_MEASURE_BATTERY_LEVEL
#define SHALL_MEASURE_BATTERY           (1U << 0)
// 1 hour in seconds
#define MEASURE_BATTERY_VOLTAGE_TIME		(60 * 60)
#endif
#define SHALL_ENABLE_ZIGBEE             (1U << 1)
// this is not implemented because of lack of support from esp-zigbee-sdk
// see https://github.com/espressif/esp-zigbee-sdk/issues/561
#define SHALL_DISABLE_ZIGBEE            (1U << 2) 

// #ifdef FEATURE_DEEP_SLEEP
// // Command to stop the deep sleep functionality at all. This is required
// // while OTA is updating the firmware. New events shall no reschedule the
// // deep sleep timer
// #define SHALL_STOP_DEEP_SLEEP						(1U << 3)

// // Command to start the deel sleep functionality. When the main loop starts
// // and when the OTA is cancelled
// #define SHALL_START_DEEP_SLEEP					(1U << 4)
// #endif

void main_loop_post(uint32_t task);
bool is_main_loop_started(void);

#endif // ZB_MAIN_LOOP