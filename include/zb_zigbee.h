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

#include <zboss_api.h>
#include <zboss_api_addons.h>
#include <zb_zcl_metering.h>
#include <zb_zcl_power_config.h>
#include <zb_zcl_ota_upgrade.h>

#define HW_MANUFACTURER_CODE            0x8888
#define GAS_METER_ENDPOINT                   1
#define GAS_METER_DEVICE_VER                 0

#ifdef FEATURE_WRITE_COUNTER_VALUE
// Private attribute to write a value to CurrentSummationDelivered
#define GAS_METER_ATTR_SET_SUMMATION_ID       0xF000
#endif

#define ESP_MANUFACTURER_NAME           "Custom devices (DiY)"
#define ESP_MODEL_IDENTIFIER            "MiCASAGasCounter" /* Customized model identifier */
#define ESP_DATE_CODE                   "20250301"
#define ESP_PRODUCT_URL                 "https://github.com/IgnacioHR/ZigbeeGasCounter"
#define ESP_PRODUCT_CODE                ""
#define ESP_LOCATION_ID                 ""

// Maximum time to force a device report
#define MUST_SYNC_MINIMUM_TIME          UINT16_C(15 * 60) // 5 minutes in seconds

// Maximum difference between the internal counter value and last reported counter value
#define COUNTER_REPORT_DIFF             UINT32_C(10)


void start_identifying(zb_bufid_t bufid);
void zigbee_start(void);
void app_device_ctx_init(void);
void set_device_extended_status_bit(zb_uint64_t bit);
void clear_device_extended_status_bit(zb_uint64_t bit);
void reset_device_extended_status();
void set_device_status_bit(zb_uint64_t bit);
void clear_device_status_bit(zb_uint64_t bit);
void reset_device_status();
void set_init_current_summ(zb_uint48_t value);
void set_init_old_time(uint32_t value);
zb_uint64_t get_current_summ(void);
void zb_counter_increment(void);
bool is_zigbee_started(void);
bool is_leaving_network(void);
void leave_action(void);
uint32_t get_current_time(void);
uint32_t get_old_time(void);
void set_battery_voltage_mv(int32_t voltage_mv);
void set_battery_unavailable(void);

typedef struct {
    zb_zcl_basic_attrs_ext_t base;

    zb_uint8_t alarm_mask;
    zb_uint8_t generic_device_type_id;
    zb_char_t label_id[33];
    zb_char_t product_url[51];
    zb_char_t product_code_id[33];
} zb_zcl_my_basic_attrs_ext_t;

typedef struct {
    zb_uint8_t battery_voltage;
    zb_uint8_t battery_percentage;
    zb_uint32_t battery_alarm_state;
    zb_uint8_t battery_alarm_mask;
    zb_uint8_t battery_voltage_min;
    zb_uint8_t battery_voltage_th1;
    zb_uint8_t battery_voltage_rated;
    zb_uint8_t battery_quantity;
    zb_uint16_t main_voltage_dwell;
} zb_zcl_power_config_attrs_t;

// typedef struct {
//     zb_uint32_t ota_upgrade_file_version;
//     zb_uint16_t ota_upgrade_manufacturer;
//     zb_uint16_t ota_upgrade_image_type;
//     zb_uint16_t stack_version;
//     zb_zcl_ota_upgrade_client_variable_t client_data;
// } zb_zcl_ota_cluster_attrs_t;

typedef struct zb_zcl_my_metering_attrs_s
{
    zb_zcl_metering_attrs_t base;
    zb_uint64_t device_extended_status;
    zb_uint24_t multiplier;
    zb_uint24_t divisor;

#ifdef FEATURE_MEASURE_FLOW_RATE
    zb_int24_t instantaneous_demand;
    zb_uint8_t demand_formatting;
#endif
} zb_zcl_my_metering_attrs_t;

/* Main application customizable context.
 * Stores all settings and static values.
 */
typedef struct {
	zb_zcl_my_basic_attrs_ext_t basic_attr;
	zb_zcl_identify_attrs_t identify_attr;
#ifdef FEATURE_MEASURE_BATTERY_LEVEL
	zb_zcl_power_config_attrs_t power_config_attr;
#endif
	zb_zcl_my_metering_attrs_t metering_attr;
	// zb_zcl_ota_cluster_attrs_t ota_attr;
} gas_meter_device_ctx_t;

gas_meter_device_ctx_t *get_dev_ctx_ptr(void);

#endif // ZB_ZIGBEE_H