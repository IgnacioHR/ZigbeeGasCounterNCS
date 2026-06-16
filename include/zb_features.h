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

// ******************************************************************************
// ***                         FEATURES CONFIGURATION                        ****
// ******************************************************************************
#ifndef ZB_FEATURES_H
#define ZB_FEATURES_H

// if defined, the device reports power and energy. This requires to compute the
// time elapsed between ticks and might to drain the battery more. Enable if you
// are powering the unit from external power
// #define FEATURE_MEASURE_FLOW_RATE				1

// define if the device is powered from batteries and you want the device to
// measure the battery voltage. Note battery voltage is not reportable. Use the GUI
// refresh button to obtain the latest measured value from the device.
#define FEATURE_MEASURE_BATTERY_LEVEL			1

// Decide if you are going to use DEEP_SLEEP or LIGHT_SLEEP. Only one can be 
// defined. It is possible also to disable both in case the unit is not battery
// powered

// In DEEP_SLEEP mode the device wakes up once every hour, connect to the network
// report the values (the counter) and goes to sleep again. This means it is not
// likely the device will react to commands send from the user interface. But the
// benefit is a extended battery life
// #define FEATURE_DEEP_SLEEP								1

// In LIGHT_SLEEP the ticks are reported more fulently to the coordinator and the
// device can react to the user interface (the sleep window is set in 30 seconds)
// #define FEATURE_LIGHT_SLEEP							1

// In zigbee2mqtt it is not possible to write the value to the real gas counter.
// The Zigbee Cluster Library Specification in Table 10-57 states that 
// CurrentSummationDelivered is read only. So, the trick is to implement a custom
// cluster so values written to it are transferred to the current counter value.
#define FEATURE_WRITE_COUNTER_VALUE				1

#if defined(FEATURE_DEEP_SLEEP) && defined(FEATURE_LIGHT_SLEEP)
BUILD_ASSERT(true,"Only one of FEATURE_DEEP_SLEEP or FEATURE_LIGHT_SLEEP can be defined!")
#endif

#endif // ZB_FEATURES_H