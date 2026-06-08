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

#ifndef ZB_ADC_H
#define ZB_ADC_H

#include "zb_features.h"

#ifdef FEATURE_MEASURE_BATTERY_LEVEL
// Vendor defined battery voltage
#define RATED_BATTERY_VOLTAGE   3700
// 1s maximum battery voltage, this will be translated to 200 for the battery percentage (represented as 100%)
#define MAX_BATTERY_VOLTAGE     4200
// 1s minimum battery voltage in mv, this is 0% battery, battery specs says 2500mv but we set to 2600mv for safety, the ESP32C6 will not operate under 3v
#define MIN_BATTERY_VOLTAGE     3050
// limit to set warning on low battery voltage, this is from battery readings, means 4200mv max value
#define WARN_BATTERY_VOLTAGE    3150
// Number of batteries
#define BATTERY_UNITS							 1

void fire_adc(void);
bool check_shall_measure_battery(void);
int adc_gpio_init(void);
#endif

#endif // ZB_ADC_H