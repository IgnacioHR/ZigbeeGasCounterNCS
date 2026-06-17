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

/* gas_meter_zcl.h */

#ifndef ZB_ZIGBEE_H
#define ZB_ZIGBEE_H

#include "zb_features.h"
#include <zboss_api.h>

#define HW_MANUFACTURER_CODE            0x8888
#define GAS_METER_ENDPOINT                   1
#define GAS_METER_DEVICE_VER                 0

#ifdef FEATURE_WRITE_COUNTER_VALUE
// Private attribute to write a value to CurrentSummationDelivered
#define GAS_METER_ATTR_SET_SUMMATION_ID       0xF000
#endif

#define ZB_MANUFACTURER_NAME           "Custom devices (DiY)"
#define ZB_MODEL_IDENTIFIER            "MiCASAGasCounter" /* Customized model identifier */
#define ZB_PRODUCT_URL                 "https://github.com/IgnacioHR/ZigbeeGasCounter"
#define ZB_PRODUCT_CODE                ""
#define ZB_LOCATION_ID                 ""

// Maximum time to force a device report
#define MUST_SYNC_MINIMUM_TIME          UINT16_C(15 * 60) // 5 minutes in seconds

// Maximum difference between the internal counter value and last reported counter value
#define COUNTER_REPORT_DIFF             UINT32_C(10)


void start_identifying(zb_bufid_t bufid);
void zigbee_start(void);
bool is_zigbee_started(void);
bool is_leaving_network(void);
void leave_action(void);
uint32_t get_current_time(void);

#endif // ZB_ZIGBEE_H