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

#ifndef ZB_ZIGBEE_CTX_H
#define ZB_ZIGBEE_CTX_H

#include <zboss_api.h>
#include <zboss_api_addons.h>
#include <zb_zcl_metering.h>
#include <zb_zcl_power_config.h>

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
void app_configure_reporting(void);
void app_device_ctx_init(void);
void app_clusters_attr_set(void);
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
uint32_t get_old_time(void);
void zb_counter_set(zb_uint48_t value);

#ifdef FEATURE_MEASURE_BATTERY_LEVEL
void set_battery_voltage_mv(int32_t voltage_mv);
void set_battery_unavailable(void);
#endif

#ifdef FEATURE_MEASURE_FLOW_RATE
int32_t get_instantaneous_demand(void);
void set_instantaneous_demand(int32_t value);
#endif


#endif // ZB_ZIGBEE_CTX_H