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

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(zigbee, LOG_LEVEL_INF);

#include "zb_features.h"
#include "zb_zigbee.h"
#include "zb_zigbee_ctx.h"
#include "zb_adc.h"
#include "zb_report_event.h"
#include "zb_nvr.h"

/* LED used for device identification. */
// #define IDENTIFY_LED                        DK_LED4
#ifdef FEATURE_MEASURE_BATTERY_LEVEL
#define TO_PERCENTAGE           (double)(200.0 / ((double)MAX_BATTERY_VOLTAGE - (double)MIN_BATTERY_VOLTAGE))
#endif

static uint32_t old_zcl_time = 0;

/* Zigbee device application context storage. */
static gas_meter_device_ctx_t dev_ctx;

static K_MUTEX_DEFINE(counter_mutex);

/**
 * @brief ZCL_BASIC Cluster
 * 
 */
static ZB_ZCL_START_DECLARE_ATTRIB_LIST_CLUSTER_REVISION(basic_attr_list, ZB_ZCL_BASIC) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_BASIC_ZCL_VERSION_ID, (&dev_ctx.basic_attr.base.zcl_version)) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_BASIC_POWER_SOURCE_ID, (&dev_ctx.basic_attr.base.power_source)) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, (&dev_ctx.basic_attr.base.mf_name))\
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, (&dev_ctx.basic_attr.base.model_id)) \
	ZB_ZCL_SET_ATTR_DESC_M( \
		ZB_ZCL_ATTR_BASIC_ALARM_MASK_ID, \
		&dev_ctx.basic_attr.alarm_mask, \
		ZB_ZCL_ATTR_TYPE_8BITMAP, \
		ZB_ZCL_ATTR_ACCESS_READ_WRITE) \
	ZB_ZCL_SET_ATTR_DESC_M( \
		ZB_ZCL_ATTR_BASIC_GENERIC_DEVICE_TYPE_ID, \
		&dev_ctx.basic_attr.generic_device_type_id, \
		ZB_ZCL_ATTR_TYPE_8BIT_ENUM, \
		ZB_ZCL_ATTR_ACCESS_READ_ONLY) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_BASIC_DATE_CODE_ID, (&dev_ctx.basic_attr.base.date_code)) \
	ZB_ZCL_SET_ATTR_DESC_M( \
		ZB_ZCL_ATTR_BASIC_PRODUCT_URL_ID, \
		&dev_ctx.basic_attr.product_url, \
		ZB_ZCL_ATTR_TYPE_CHAR_STRING, \
		ZB_ZCL_ATTR_ACCESS_READ_ONLY) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_BASIC_HW_VERSION_ID, (&dev_ctx.basic_attr.base.hw_version)) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_BASIC_APPLICATION_VERSION_ID, (&dev_ctx.basic_attr.base.app_version)) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_BASIC_STACK_VERSION_ID, (&dev_ctx.basic_attr.base.stack_version)) \
	ZB_ZCL_SET_ATTR_DESC_M( \
		ZB_ZCL_ATTR_BASIC_PRODUCT_CODE_ID, \
		&dev_ctx.basic_attr.product_code_id, \
		ZB_ZCL_ATTR_TYPE_OCTET_STRING, \
		ZB_ZCL_ATTR_ACCESS_READ_ONLY) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_BASIC_SW_BUILD_ID, (&dev_ctx.basic_attr.base.sw_ver)) \
	ZB_ZCL_SET_ATTR_DESC_M( \
		ZB_ZCL_ATTR_BASIC_PRODUCT_LABEL_ID, \
		&dev_ctx.basic_attr.label_id, \
		ZB_ZCL_ATTR_TYPE_CHAR_STRING, \
		ZB_ZCL_ATTR_ACCESS_READ_ONLY) \
	ZB_ZCL_FINISH_DECLARE_ATTRIB_LIST;

/**
 * @brief ZB_ZCL_IDENTIFY Cluster
 * 
 */
static ZB_ZCL_DECLARE_IDENTIFY_ATTRIB_LIST(
	identify_attr_list,
	&dev_ctx.identify_attr.identify_time);

static ZB_ZCL_DECLARE_IDENTIFY_CLIENT_ATTRIB_LIST(identify_client_attr_list);

/**
 * @brief ZB_ZCL_POWER_CONFIG Cluster
 * 
 */
#ifdef FEATURE_MEASURE_BATTERY_LEVEL
static ZB_ZCL_START_DECLARE_ATTRIB_LIST_CLUSTER_REVISION(power_attr_list, ZB_ZCL_POWER_CONFIG) \
	ZB_ZCL_SET_ATTR_DESC_M( \
		ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_ID, \
		&dev_ctx.power_config_attr.battery_voltage, \
		ZB_ZCL_ATTR_TYPE_U8, \
		ZB_ZCL_ATTR_ACCESS_READ_ONLY | ZB_ZCL_ATTR_ACCESS_REPORTING) \
	ZB_ZCL_SET_ATTR_DESC_M( \
		ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID, \
		&dev_ctx.power_config_attr.battery_percentage, \
		ZB_ZCL_ATTR_TYPE_U8, \
		ZB_ZCL_ATTR_ACCESS_READ_ONLY | ZB_ZCL_ATTR_ACCESS_REPORTING) \
	ZB_ZCL_SET_ATTR_DESC_M( \
		ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_ALARM_STATE_ID, \
		&dev_ctx.power_config_attr.battery_alarm_state, \
		ZB_ZCL_ATTR_TYPE_32BITMAP, \
		ZB_ZCL_ATTR_ACCESS_READ_ONLY | ZB_ZCL_ATTR_ACCESS_REPORTING) \
	ZB_ZCL_SET_ATTR_DESC_M( \
		ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_ALARM_MASK_ID, \
		&dev_ctx.power_config_attr.battery_alarm_mask, \
		ZB_ZCL_ATTR_TYPE_8BITMAP, \
		ZB_ZCL_ATTR_ACCESS_READ_WRITE) \
	ZB_ZCL_SET_ATTR_DESC_M( \
		ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_MIN_THRESHOLD_ID, \
		&dev_ctx.power_config_attr.battery_voltage_min, \
		ZB_ZCL_ATTR_TYPE_U8, \
		ZB_ZCL_ATTR_ACCESS_READ_WRITE) \
	ZB_ZCL_SET_ATTR_DESC_M( \
		ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_THRESHOLD1_ID, \
		&dev_ctx.power_config_attr.battery_voltage_th1, \
		ZB_ZCL_ATTR_TYPE_U8, \
		ZB_ZCL_ATTR_ACCESS_READ_WRITE) \
	ZB_ZCL_SET_ATTR_DESC_M( \
		ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_RATED_VOLTAGE_ID, \
		&dev_ctx.power_config_attr.battery_voltage_rated, \
		ZB_ZCL_ATTR_TYPE_U8, \
		ZB_ZCL_ATTR_ACCESS_READ_WRITE) \
	ZB_ZCL_SET_ATTR_DESC_M( \
		ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_QUANTITY_ID, \
		&dev_ctx.power_config_attr.battery_quantity, \
		ZB_ZCL_ATTR_TYPE_U8, \
		ZB_ZCL_ATTR_ACCESS_READ_WRITE) \
	ZB_ZCL_SET_ATTR_DESC_M( \
		ZB_ZCL_ATTR_POWER_CONFIG_MAINS_DWELL_TRIP_POINT, \
		&dev_ctx.power_config_attr.main_voltage_dwell, \
		ZB_ZCL_ATTR_TYPE_U16, \
		ZB_ZCL_ATTR_ACCESS_READ_WRITE) \
	ZB_ZCL_FINISH_DECLARE_ATTRIB_LIST;
#endif

#ifdef FEATURE_MEASURE_BATTERY_LEVEL
// Reporting attributes = PERCENTAGE_REMAINING_ID + VOLTAGE_ID + ALARM_STATE_ID
#define MY_ZB_ZCL_POWER_CONFIG_REPORT_ATTR_COUNT    3
#else
// Reporting attributes = 
#define MY_ZB_ZCL_POWER_CONFIG_REPORT_ATTR_COUNT    0
#endif

/**
 * @brief ZB_ZCL_METERING Cluster
 *        There are 4 possibilities here and all 4 has to be written explicitly as
 *        we are using macros to build the content
 */
#if defined(FEATURE_MEASURE_FLOW_RATE) && defined(FEATURE_WRITE_COUNTER_VALUE)
static ZB_ZCL_START_DECLARE_ATTRIB_LIST_CLUSTER_REVISION(metering_attr_list, ZB_ZCL_METERING) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_METERING_CURRENT_SUMMATION_DELIVERED_ID, (&dev_ctx.metering_attr.base.curr_summ_delivered)) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_METERING_STATUS_ID, (&dev_ctx.metering_attr.base.status)) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_METERING_UNIT_OF_MEASURE_ID, (&dev_ctx.metering_attr.base.unit_of_measure)) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_METERING_SUMMATION_FORMATTING_ID, (&dev_ctx.metering_attr.base.summation_formatting)) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_METERING_METERING_DEVICE_TYPE_ID, (&dev_ctx.metering_attr.base.device_type)) \
	ZB_ZCL_SET_ATTR_DESC_M( \
	ZB_ZCL_ATTR_METERING_EXTENDED_STATUS_ID, \
	&dev_ctx.metering_attr.device_extended_status, \
	ZB_ZCL_ATTR_TYPE_64BITMAP, \
	ZB_ZCL_ATTR_ACCESS_READ_ONLY | ZB_ZCL_ATTR_ACCESS_REPORTING) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_METERING_MULTIPLIER_ID, (&dev_ctx.metering_attr.multiplier)) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_METERING_DIVISOR_ID, (&dev_ctx.metering_attr.divisor)) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_METERING_INSTANTANEOUS_DEMAND_ID, (&dev_ctx.metering_attr.instantaneous_demand)) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_METERING_DEMAND_FORMATTING_ID, (&dev_ctx.metering_attr.demand_formatting)) \
	{ GAS_METER_ATTR_SET_SUMMATION_ID, \
		ZB_ZCL_ATTR_TYPE_U48, \
		ZB_ZCL_ATTR_ACCESS_READ_WRITE, \
		ZB_ZCL_MANUFACTURER_SPECIFIC, \
		(void*)(&dev_ctx.metering_attr.base.curr_summ_delivered) \
	}, \
	ZB_ZCL_FINISH_DECLARE_ATTRIB_LIST;
#endif
#if defined(FEATURE_MEASURE_FLOW_RATE) && !defined(FEATURE_WRITE_COUNTER_VALUE)
static ZB_ZCL_START_DECLARE_ATTRIB_LIST_CLUSTER_REVISION(metering_attr_list, ZB_ZCL_METERING) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_METERING_CURRENT_SUMMATION_DELIVERED_ID, (&dev_ctx.metering_attr.base.curr_summ_delivered)) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_METERING_STATUS_ID, (&dev_ctx.metering_attr.base.status)) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_METERING_UNIT_OF_MEASURE_ID, (&dev_ctx.metering_attr.base.unit_of_measure)) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_METERING_SUMMATION_FORMATTING_ID, (&dev_ctx.metering_attr.base.summation_formatting)) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_METERING_METERING_DEVICE_TYPE_ID, (&dev_ctx.metering_attr.base.device_type)) \
	ZB_ZCL_SET_ATTR_DESC_M( \
	ZB_ZCL_ATTR_METERING_EXTENDED_STATUS_ID, \
	&dev_ctx.metering_attr.device_extended_status, \
	ZB_ZCL_ATTR_TYPE_64BITMAP, \
	ZB_ZCL_ATTR_ACCESS_READ_ONLY | ZB_ZCL_ATTR_ACCESS_REPORTING) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_METERING_MULTIPLIER_ID, (&dev_ctx.metering_attr.multiplier)) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_METERING_DIVISOR_ID, (&dev_ctx.metering_attr.divisor)) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_METERING_INSTANTANEOUS_DEMAND_ID, (&dev_ctx.metering_attr.instantaneous_demand)) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_METERING_DEMAND_FORMATTING_ID, (&dev_ctx.metering_attr.demand_formatting)) \
	ZB_ZCL_FINISH_DECLARE_ATTRIB_LIST;
#endif
#if !defined(FEATURE_MEASURE_FLOW_RATE) && defined(FEATURE_WRITE_COUNTER_VALUE)
static ZB_ZCL_START_DECLARE_ATTRIB_LIST_CLUSTER_REVISION(metering_attr_list, ZB_ZCL_METERING) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_METERING_CURRENT_SUMMATION_DELIVERED_ID, (&dev_ctx.metering_attr.base.curr_summ_delivered)) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_METERING_STATUS_ID, (&dev_ctx.metering_attr.base.status)) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_METERING_UNIT_OF_MEASURE_ID, (&dev_ctx.metering_attr.base.unit_of_measure)) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_METERING_SUMMATION_FORMATTING_ID, (&dev_ctx.metering_attr.base.summation_formatting)) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_METERING_METERING_DEVICE_TYPE_ID, (&dev_ctx.metering_attr.base.device_type)) \
	ZB_ZCL_SET_ATTR_DESC_M( \
		ZB_ZCL_ATTR_METERING_EXTENDED_STATUS_ID, \
		&dev_ctx.metering_attr.device_extended_status, \
		ZB_ZCL_ATTR_TYPE_64BITMAP, \
		ZB_ZCL_ATTR_ACCESS_READ_ONLY | ZB_ZCL_ATTR_ACCESS_REPORTING) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_METERING_MULTIPLIER_ID, (&dev_ctx.metering_attr.multiplier)) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_METERING_DIVISOR_ID, (&dev_ctx.metering_attr.divisor)) \
	{ \
		GAS_METER_ATTR_SET_SUMMATION_ID, \
		ZB_ZCL_ATTR_TYPE_U48, \
		ZB_ZCL_ATTR_ACCESS_READ_WRITE | ZB_ZCL_ATTR_MANUF_SPEC, \
		HW_MANUFACTURER_CODE, \
		(void*)(&dev_ctx.metering_attr.base.curr_summ_delivered) \
	}, \
	ZB_ZCL_FINISH_DECLARE_ATTRIB_LIST;
#endif
#if !defined(FEATURE_MEASURE_FLOW_RATE) && !defined(FEATURE_WRITE_COUNTER_VALUE)
static ZB_ZCL_START_DECLARE_ATTRIB_LIST_CLUSTER_REVISION(metering_attr_list, ZB_ZCL_METERING) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_METERING_CURRENT_SUMMATION_DELIVERED_ID, (&dev_ctx.metering_attr.base.curr_summ_delivered)) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_METERING_STATUS_ID, (&dev_ctx.metering_attr.base.status)) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_METERING_UNIT_OF_MEASURE_ID, (&dev_ctx.metering_attr.base.unit_of_measure)) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_METERING_SUMMATION_FORMATTING_ID, (&dev_ctx.metering_attr.base.summation_formatting)) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_METERING_METERING_DEVICE_TYPE_ID, (&dev_ctx.metering_attr.base.device_type)) \
	ZB_ZCL_SET_ATTR_DESC_M( \
		ZB_ZCL_ATTR_METERING_EXTENDED_STATUS_ID, \
		&dev_ctx.metering_attr.device_extended_status, \
		ZB_ZCL_ATTR_TYPE_64BITMAP, \
		ZB_ZCL_ATTR_ACCESS_READ_ONLY | ZB_ZCL_ATTR_ACCESS_REPORTING) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_METERING_MULTIPLIER_ID, (&dev_ctx.metering_attr.multiplier)) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_METERING_DIVISOR_ID, (&dev_ctx.metering_attr.divisor)) \
	ZB_ZCL_FINISH_DECLARE_ATTRIB_LIST;
#endif

#ifdef FEATURE_MEASURE_FLOW_RATE
// Reporting attributes = CURRENT_SUMMATION_DELIVERED + INSTANTANEOUS_DEMAND + STATUS + EXTENDED STATUS
#define MY_ZB_ZCL_METERING_REPORT_ATTR_COUNT    4
#else
// Reporting attributes = CURRENT_SUMMATION_DELIVERED + STATUS + EXTENDED STATUS
#define MY_ZB_ZCL_METERING_REPORT_ATTR_COUNT    3
#endif

/**
 * @brief Cluster list
 * 
 */
static zb_zcl_cluster_desc_t gas_meter_cluster_list[] = {
	ZB_ZCL_CLUSTER_DESC(
		ZB_ZCL_CLUSTER_ID_BASIC,
		ZB_ZCL_ARRAY_SIZE(basic_attr_list, zb_zcl_attr_t),
		(basic_attr_list),
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		HW_MANUFACTURER_CODE
	),
	ZB_ZCL_CLUSTER_DESC(
		ZB_ZCL_CLUSTER_ID_IDENTIFY,
		ZB_ZCL_ARRAY_SIZE(identify_attr_list, zb_zcl_attr_t),
		(identify_attr_list),
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		HW_MANUFACTURER_CODE
	),
	ZB_ZCL_CLUSTER_DESC(
		ZB_ZCL_CLUSTER_ID_IDENTIFY,
		ZB_ZCL_ARRAY_SIZE(identify_client_attr_list, zb_zcl_attr_t),
		(identify_client_attr_list),
		ZB_ZCL_CLUSTER_CLIENT_ROLE,
		HW_MANUFACTURER_CODE
	),
	ZB_ZCL_CLUSTER_DESC(
		ZB_ZCL_CLUSTER_ID_METERING,
		ZB_ZCL_ARRAY_SIZE(metering_attr_list, zb_zcl_attr_t),
		(metering_attr_list),
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		HW_MANUFACTURER_CODE
	),
#ifdef FEATURE_MEASURE_BATTERY_LEVEL
	ZB_ZCL_CLUSTER_DESC(
		ZB_ZCL_CLUSTER_ID_POWER_CONFIG,
		ZB_ZCL_ARRAY_SIZE(power_attr_list, zb_zcl_attr_t),
		(power_attr_list),
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		HW_MANUFACTURER_CODE
	),    
#endif
	ZB_ZCL_CLUSTER_DESC(
		ZB_ZCL_CLUSTER_ID_TIME,
		0,
		NULL,
		ZB_ZCL_CLUSTER_CLIENT_ROLE,
		HW_MANUFACTURER_CODE
	)
};

/**
 * @brief This value depends on the features enabled/disabled
 *        and adjusting this value is tricky. 
 *        This value can't be calculated so it must be right in the source code.
 *        In case of compilation errors, this value must equal the sum of
 *        MY_ZB_ZCL_POWER_CONFIG_REPORT_ATTR_COUNT and MY_ZB_ZCL_METERING_REPORT_ATTR_COUNT
 *        I use VSCode while holding the mouse over the defines and calculate the proper value.
 *        
 */
#define GAS_NUMBER_REPORTING_ATTRIBUTES 6

BUILD_ASSERT(
	GAS_NUMBER_REPORTING_ATTRIBUTES ==
	(MY_ZB_ZCL_POWER_CONFIG_REPORT_ATTR_COUNT +
	 MY_ZB_ZCL_METERING_REPORT_ATTR_COUNT),
	"Wrong GAS_NUMBER_REPORTING_ATTRIBUTES value"
);

#define ZB_DECLARE_SIMPLE_DESC_EXPAND(in_count, out_count) \
	ZB_DECLARE_SIMPLE_DESC(in_count, out_count)

#define ZB_AF_SIMPLE_DESC_TYPE_EXPAND(in_count, out_count) \
	ZB_AF_SIMPLE_DESC_TYPE(in_count, out_count)

#define ZBOSS_DEVICE_DECLARE_REPORTING_CTX_EXPAND(rep_ctx, in_count) \
  ZBOSS_DEVICE_DECLARE_REPORTING_CTX(rep_ctx, in_count)

#define GAS_METER_IN_CLUSTER_COUNT 4

#ifdef FEATURE_MEASURE_BATTERY_LEVEL
#define GAS_METER_OUT_CLUSTER_COUNT 2
#else
#define GAS_METER_OUT_CLUSTER_COUNT 1
#endif

ZB_DECLARE_SIMPLE_DESC_EXPAND(GAS_METER_IN_CLUSTER_COUNT, GAS_METER_OUT_CLUSTER_COUNT);
static ZB_AF_SIMPLE_DESC_TYPE_EXPAND(GAS_METER_IN_CLUSTER_COUNT, GAS_METER_OUT_CLUSTER_COUNT) simple_desc_gas_meter = {
	GAS_METER_ENDPOINT,
	ZB_AF_HA_PROFILE_ID,
	ZB_HA_METER_INTERFACE_DEVICE_ID,
	GAS_METER_DEVICE_VER,
	0,
	GAS_METER_IN_CLUSTER_COUNT,
	GAS_METER_OUT_CLUSTER_COUNT,
	{
		ZB_ZCL_CLUSTER_ID_BASIC,
		ZB_ZCL_CLUSTER_ID_IDENTIFY,
		ZB_ZCL_CLUSTER_ID_METERING,
		ZB_ZCL_CLUSTER_ID_POWER_CONFIG,

		ZB_ZCL_CLUSTER_ID_IDENTIFY,
		// ZB_ZCL_CLUSTER_ID_OTA_UPGRADE,

		ZB_ZCL_CLUSTER_ID_TIME,
	}
};
static ZBOSS_DEVICE_DECLARE_REPORTING_CTX_EXPAND(reporting_ctx, GAS_NUMBER_REPORTING_ATTRIBUTES);
static ZBOSS_DEVICE_DECLARE_LEVEL_CONTROL_CTX(cvc_alarm_info_gas_meter, 0);
static ZB_AF_DECLARE_ENDPOINT_DESC(
	ep_gas_meter, 
	GAS_METER_ENDPOINT, 
	ZB_AF_HA_PROFILE_ID, 
	0, 
	NULL, 
	ZB_ZCL_ARRAY_SIZE(gas_meter_cluster_list, zb_zcl_cluster_desc_t), 
	gas_meter_cluster_list, 
	(zb_af_simple_desc_1_1_t *)&simple_desc_gas_meter, 
	GAS_NUMBER_REPORTING_ATTRIBUTES, 
	reporting_ctx, 
	0, 
	cvc_alarm_info_gas_meter);

extern zb_af_endpoint_desc_t zigbee_fota_client_ep;
ZBOSS_DECLARE_DEVICE_CTX_2_EP(gas_meter_ctx, 
	zigbee_fota_client_ep,
	ep_gas_meter);

/**
 * @brief Obtains a pointer to the device context structure
 * 
 * @return gas_meter_device_ctx_t* 
 */
gas_meter_device_ctx_t *get_dev_ctx_ptr(void)
{
  return &dev_ctx;
}

/**
 * @brief Function for initializing all clusters attributes.
 *        This is called prior to NVM been initialized so do
 *        not use any other subsystems during this call
 * 
 */
void app_device_ctx_init(void)
{
	/* Basic cluster attributes data */
	dev_ctx.basic_attr.base.zcl_version = ZB_ZCL_VERSION;
    dev_ctx.basic_attr.base.app_version = APP_VERSION;
    dev_ctx.basic_attr.base.stack_version = STACK_VERSION;
    dev_ctx.basic_attr.base.hw_version = HARDWARE_VERSION;
    ZB_ZCL_SET_STRING_VAL(
		dev_ctx.basic_attr.base.mf_name,
		ZB_MANUFACTURER_NAME,
		ZB_ZCL_STRING_CONST_SIZE(ZB_MANUFACTURER_NAME));
    ZB_ZCL_SET_STRING_VAL(
		dev_ctx.basic_attr.base.model_id,
		ZB_MODEL_IDENTIFIER,
		ZB_ZCL_STRING_CONST_SIZE(ZB_MODEL_IDENTIFIER));
    ZB_ZCL_SET_STRING_VAL(
		dev_ctx.basic_attr.base.date_code,
		ZB_DATE_CODE,
		ZB_ZCL_STRING_CONST_SIZE(ZB_DATE_CODE));
	dev_ctx.basic_attr.base.power_source = 
#if defined(FEATURE_DEEP_SLEEP) || defined(FEATURE_LIGHT_SLEEP)
        ZB_ZCL_BASIC_POWER_SOURCE_BATTERY
#else
        ZB_ZCL_BASIC_POWER_SOURCE_DC_SOURCE
#endif
;
    ZB_ZCL_SET_STRING_VAL(
		dev_ctx.basic_attr.base.location_id,
		ZB_LOCATION_ID,
		ZB_ZCL_STRING_CONST_SIZE(ZB_LOCATION_ID));
    dev_ctx.basic_attr.base.ph_env = ZB_ZCL_BASIC_ENV_UNSPECIFIED;
    ZB_ZCL_SET_STRING_VAL(
		dev_ctx.basic_attr.base.location_id,
		ZB_LOCATION_ID,
		ZB_ZCL_STRING_CONST_SIZE(ZB_LOCATION_ID));
    ZB_ZCL_SET_STRING_VAL(
		dev_ctx.basic_attr.base.sw_ver,
		SW_BUILD_ID,
		ZB_ZCL_STRING_CONST_SIZE(SW_BUILD_ID));
    dev_ctx.basic_attr.alarm_mask = 0x03;
    dev_ctx.basic_attr.generic_device_type_id = 0xFF;
    ZB_ZCL_SET_STRING_VAL(
		dev_ctx.basic_attr.label_id,
		PRODUCT_LABEL,
		ZB_ZCL_STRING_CONST_SIZE(PRODUCT_LABEL));
    ZB_ZCL_SET_STRING_VAL(
		dev_ctx.basic_attr.product_url,
		ZB_PRODUCT_URL,
		ZB_ZCL_STRING_CONST_SIZE(ZB_PRODUCT_URL));
    ZB_ZCL_SET_STRING_VAL(
		dev_ctx.basic_attr.product_code_id,
		ZB_PRODUCT_CODE,
		ZB_ZCL_STRING_CONST_SIZE(ZB_PRODUCT_CODE));

	/* Identify cluster attributes data. */
	dev_ctx.identify_attr.identify_time =
		ZB_ZCL_IDENTIFY_IDENTIFY_TIME_DEFAULT_VALUE;

#ifdef FEATURE_MEASURE_BATTERY_LEVEL
    dev_ctx.power_config_attr.battery_voltage = 0;
    dev_ctx.power_config_attr.battery_percentage = 0;
    dev_ctx.power_config_attr.battery_alarm_state = 0;
    dev_ctx.power_config_attr.battery_alarm_mask = 
        ZB_ZCL_POWER_CONFIG_MAINS_ALARM_MASK_VOLTAGE_LOW | ZB_ZCL_POWER_CONFIG_MAINS_ALARM_MASK_VOLTAGE_HIGH | ZB_ZCL_POWER_CONFIG_MAINS_ALARM_MASK_VOLTAGE_UNAVAIL;
    dev_ctx.power_config_attr.battery_voltage_min = UINT8_C(MIN_BATTERY_VOLTAGE/100);
    dev_ctx.power_config_attr.battery_voltage_th1 = UINT8_C(WARN_BATTERY_VOLTAGE/100);
    dev_ctx.power_config_attr.battery_voltage_rated = RATED_BATTERY_VOLTAGE / 100;
    dev_ctx.power_config_attr.battery_quantity = BATTERY_UNITS;
#endif
    dev_ctx.metering_attr.base.curr_summ_delivered = (zb_uint48_t){.low=0, .high = 0};
    dev_ctx.metering_attr.base.status = 0x0;
    dev_ctx.metering_attr.base.unit_of_measure = ZB_ZCL_METERING_UNIT_M3_M3H_BINARY;
    dev_ctx.metering_attr.base.summation_formatting = ZB_ZCL_METERING_FORMATTING_SET(true, 7, 2);
    dev_ctx.metering_attr.base.device_type = ZB_ZCL_METERING_GAS_METERING;
    dev_ctx.metering_attr.device_extended_status = 0x0;
    dev_ctx.metering_attr.divisor = (zb_uint24_t){.high=0,.low=100};
    dev_ctx.metering_attr.multiplier = (zb_uint24_t){.high=0,.low=1};
#ifdef FEATURE_MEASURE_FLOW_RATE
    dev_ctx.metering_attr.instantaneous_demand = (zb_int24_t){.high=0,.low=0};
    dev_ctx.metering_attr.demand_formatting = ZB_ZCL_METERING_FORMATTING_SET(true, 2, 3);
#endif
}

void app_clusters_attr_set(void)
{
    zb_zcl_status_t ret = ZB_ZCL_STATUS_FAIL;
#ifdef FEATURE_MEASURE_BATTERY_LEVEL
    ZB_ZCL_SET_ATTRIBUTE(
		GAS_METER_ENDPOINT,
		ZB_ZCL_CLUSTER_ID_POWER_CONFIG,
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_ID,
		(zb_uint8_t *)&dev_ctx.power_config_attr.battery_voltage,
		ZB_FALSE);
    ZB_ZCL_SET_ATTRIBUTE(
		GAS_METER_ENDPOINT,
		ZB_ZCL_CLUSTER_ID_POWER_CONFIG,
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID,
		(zb_uint8_t *)&dev_ctx.power_config_attr.battery_percentage,
		ZB_FALSE);
    ZB_ZCL_SET_ATTRIBUTE(
		GAS_METER_ENDPOINT,
		ZB_ZCL_CLUSTER_ID_POWER_CONFIG,
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_ALARM_STATE_ID,
		(zb_uint8_t *)&dev_ctx.power_config_attr.battery_alarm_state,
		ZB_FALSE);
    ZB_ZCL_SET_ATTRIBUTE(
		GAS_METER_ENDPOINT,
		ZB_ZCL_CLUSTER_ID_POWER_CONFIG,
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_ALARM_MASK_ID,
		(zb_uint8_t *)&dev_ctx.power_config_attr.battery_alarm_mask,
		ZB_FALSE);
    ZB_ZCL_SET_ATTRIBUTE(
		GAS_METER_ENDPOINT,
		ZB_ZCL_CLUSTER_ID_POWER_CONFIG,
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_MIN_THRESHOLD_ID,
		(zb_uint8_t *)&dev_ctx.power_config_attr.battery_voltage_min,
		ZB_FALSE);
    ZB_ZCL_SET_ATTRIBUTE(
		GAS_METER_ENDPOINT,
		ZB_ZCL_CLUSTER_ID_POWER_CONFIG,
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_THRESHOLD1_ID,
		(zb_uint8_t *)&dev_ctx.power_config_attr.battery_voltage_th1,
		ZB_FALSE);
    ZB_ZCL_SET_ATTRIBUTE(
		GAS_METER_ENDPOINT,
		ZB_ZCL_CLUSTER_ID_POWER_CONFIG,
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_RATED_VOLTAGE_ID,
		(zb_uint8_t *)&dev_ctx.power_config_attr.battery_voltage_rated,
		ZB_FALSE);
    ZB_ZCL_SET_ATTRIBUTE(
		GAS_METER_ENDPOINT,
		ZB_ZCL_CLUSTER_ID_POWER_CONFIG,
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_QUANTITY_ID,
		(zb_uint8_t *)&dev_ctx.power_config_attr.battery_quantity,
		ZB_FALSE);
    ZB_ZCL_SET_ATTRIBUTE(
		GAS_METER_ENDPOINT,
		ZB_ZCL_CLUSTER_ID_POWER_CONFIG,
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		ZB_ZCL_ATTR_POWER_CONFIG_MAINS_DWELL_TRIP_POINT,
		(zb_uint8_t *)&dev_ctx.power_config_attr.main_voltage_dwell,
		ZB_FALSE);
#endif

    nvr_wait_loaded(K_SECONDS(2));
	ZB_ZCL_SET_ATTRIBUTE(
		GAS_METER_ENDPOINT,
		ZB_ZCL_CLUSTER_ID_METERING,
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		ZB_ZCL_ATTR_METERING_CURRENT_SUMMATION_DELIVERED_ID,
		(zb_uint8_t *)&dev_ctx.metering_attr.base.curr_summ_delivered,
		ZB_FALSE);
#ifdef FEATURE_WRITE_COUNTER_VALUE
    ret = zb_zcl_set_attr_val_manuf(
		GAS_METER_ENDPOINT,
		ZB_ZCL_CLUSTER_ID_METERING,
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		GAS_METER_ATTR_SET_SUMMATION_ID,
        ZB_ZCL_MANUFACTURER_SPECIFIC,
		(zb_uint8_t *)&dev_ctx.metering_attr.base.curr_summ_delivered,
		ZB_FALSE);
    if (ret != ZB_ZCL_STATUS_SUCCESS) {
        LOG_ERR("Can't set GAS_METER_ATTR_SET_SUMMATION_ID attribute value");
    }
#endif
	ZB_ZCL_SET_ATTRIBUTE(
		GAS_METER_ENDPOINT,
		ZB_ZCL_CLUSTER_ID_METERING,
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		ZB_ZCL_ATTR_METERING_STATUS_ID,
		(zb_uint8_t *)&dev_ctx.metering_attr.base.status,
		ZB_FALSE);
	ZB_ZCL_SET_ATTRIBUTE(
		GAS_METER_ENDPOINT,
		ZB_ZCL_CLUSTER_ID_METERING,
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		ZB_ZCL_ATTR_METERING_UNIT_OF_MEASURE_ID,
		(zb_uint8_t *)&dev_ctx.metering_attr.base.unit_of_measure,
		ZB_FALSE);
	ZB_ZCL_SET_ATTRIBUTE(
		GAS_METER_ENDPOINT,
		ZB_ZCL_CLUSTER_ID_METERING,
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		ZB_ZCL_ATTR_METERING_SUMMATION_FORMATTING_ID,
		(zb_uint8_t *)&dev_ctx.metering_attr.base.summation_formatting,
		ZB_FALSE);
	ZB_ZCL_SET_ATTRIBUTE(
		GAS_METER_ENDPOINT,
		ZB_ZCL_CLUSTER_ID_METERING,
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		ZB_ZCL_ATTR_METERING_METERING_DEVICE_TYPE_ID,
		(zb_uint8_t *)&dev_ctx.metering_attr.base.device_type,
		ZB_FALSE);
	ZB_ZCL_SET_ATTRIBUTE(
		GAS_METER_ENDPOINT,
		ZB_ZCL_CLUSTER_ID_METERING,
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		ZB_ZCL_ATTR_METERING_EXTENDED_STATUS_ID,
		(zb_uint8_t *)&dev_ctx.metering_attr.device_extended_status,
		ZB_FALSE);
	ZB_ZCL_SET_ATTRIBUTE(
		GAS_METER_ENDPOINT,
		ZB_ZCL_CLUSTER_ID_METERING,
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		ZB_ZCL_ATTR_METERING_MULTIPLIER_ID,
		(zb_uint8_t *)&dev_ctx.metering_attr.multiplier,
		ZB_FALSE);
	ZB_ZCL_SET_ATTRIBUTE(
		GAS_METER_ENDPOINT,
		ZB_ZCL_CLUSTER_ID_METERING,
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		ZB_ZCL_ATTR_METERING_DIVISOR_ID,
		(zb_uint8_t *)&dev_ctx.metering_attr.divisor,
		ZB_FALSE);
#ifdef FEATURE_MEASURE_FLOW_RATE
	ZB_ZCL_SET_ATTRIBUTE(
		GAS_METER_ENDPOINT,
		ZB_ZCL_CLUSTER_ID_METERING,
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		ZB_ZCL_ATTR_METERING_INSTANTANEOUS_DEMAND_ID,
		(zb_uint8_t *)&dev_ctx.metering_attr.instantaneous_demand,
		ZB_FALSE);
	ZB_ZCL_SET_ATTRIBUTE(
		GAS_METER_ENDPOINT,
		ZB_ZCL_CLUSTER_ID_METERING,
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		ZB_ZCL_ATTR_METERING_DEMAND_FORMATTING_ID,
		(zb_uint8_t *)&dev_ctx.metering_attr.demand_formatting,
		ZB_FALSE);
#endif
}

/**
 * @brief Set bits of the meter device extended status directly to the device context
 *        and prior to initialize the zigbee device.
 * 
 * @param bit 
 */
void set_device_extended_status_bit(zb_uint64_t bit)
{
    dev_ctx.metering_attr.device_extended_status |= bit;
}

/**
 * @brief Clear bits of the meter device extended status directly to the device context
 *        and prior to initialize the zigbee device.
 * 
 * @param bit 
 */
void clear_device_extended_status_bit(zb_uint64_t bit)
{
    dev_ctx.metering_attr.device_extended_status &= ~bit;
}

/**
 * @brief Reset extended device status to 0
 * 
 */
void reset_device_extended_status()
{
    dev_ctx.metering_attr.device_extended_status = 0;
}

/**
 * @brief Set bits of the meter device status directly to the device context
 *        and prior to initialize the zigbee device.
 * 
 * @param bit 
 */
void set_device_status_bit(zb_uint64_t bit)
{
    dev_ctx.metering_attr.base.status |= bit;
}

/**
 * @brief Clear bits of the meter device status directly to the device context
 *        and prior to initialize the zigbee device.
 * 
 * @param bit 
 */
void clear_device_status_bit(zb_uint64_t bit)
{
    dev_ctx.metering_attr.base.status &= ~bit;
}

/**
 * @brief Reset device status to 0
 * 
 */
void reset_device_status()
{
    dev_ctx.metering_attr.base.status = 0;
}

/**
 * @brief Set the initial value of the current sum delivered. Used from NVS
 * 
 * @param value 
 */
void set_init_current_summ(zb_uint48_t value)
{
		k_mutex_lock(&counter_mutex, K_FOREVER);
    dev_ctx.metering_attr.base.curr_summ_delivered = value;
		k_mutex_unlock(&counter_mutex);
}

/**
 * @brief Used from the NVR subsystem when the last stored time is loaded
 * 
 * @param value 
 */
void set_init_old_time(uint32_t value)
{
    old_zcl_time = value;
}

/**
 * @brief Access to the last timestamp stored in the nvr subsystem
 * 
 * @return uint32_t 
 */
uint32_t get_old_time(void)
{
    return old_zcl_time;
}

#ifdef FEATURE_MEASURE_BATTERY_LEVEL
/**
 * @brief Set the battery voltage mv and updates all power attributes
 *        accordingly
 * 
 * @param voltage_mv 
 */
void set_battery_voltage_mv(int32_t voltage_mv)
{
    float bat_voltage_f = (float)(voltage_mv);
    uint8_t voltage_8 = (uint8_t)(bat_voltage_f/100.0f+0.5f);
    if (voltage_8 > 42u)
        voltage_8 = 42u;
    dev_ctx.power_config_attr.battery_voltage = voltage_8;
    if (voltage_mv < MIN_BATTERY_VOLTAGE) voltage_mv = MIN_BATTERY_VOLTAGE;
    uint8_t battery_percentage = (uint8_t)(((double)voltage_mv - MIN_BATTERY_VOLTAGE)*TO_PERCENTAGE);
    dev_ctx.power_config_attr.battery_percentage = battery_percentage;
    int32_t shall_report = REPORT_BATTERY;
    if (dev_ctx.power_config_attr.battery_alarm_state & ZB_ZCL_POWER_CONFIG_MAINS_ALARM_MASK_VOLTAGE_UNAVAIL) {
        // reset battery alarm state
        dev_ctx.power_config_attr.battery_alarm_state &= ~ZB_ZCL_POWER_CONFIG_MAINS_ALARM_MASK_VOLTAGE_UNAVAIL;
    }
    if (battery_percentage > 200) {
        battery_percentage = 200;
        dev_ctx.power_config_attr.battery_alarm_state |= ZB_ZCL_POWER_CONFIG_MAINS_ALARM_MASK_VOLTAGE_HIGH;
    } else {
        dev_ctx.power_config_attr.battery_alarm_state &= ~ZB_ZCL_POWER_CONFIG_MAINS_ALARM_MASK_VOLTAGE_HIGH;
    }
    if (voltage_mv < WARN_BATTERY_VOLTAGE) {
        // BatteryVoltageMinThreshold or BatteryPercentageMinThreshold reached for Battery Source 1
        dev_ctx.power_config_attr.battery_alarm_state |= ZB_ZCL_POWER_CONFIG_MAINS_ALARM_MASK_VOLTAGE_LOW; 
    } else {
        dev_ctx.power_config_attr.battery_alarm_state &= ~ZB_ZCL_POWER_CONFIG_MAINS_ALARM_MASK_VOLTAGE_LOW;
    }
    if (dev_ctx.power_config_attr.battery_alarm_state != 0 && (dev_ctx.metering_attr.base.status & ZB_ZCL_METERING_GAS_LOW_BATTERY) == 0) {
        dev_ctx.metering_attr.base.status |= ZB_ZCL_METERING_GAS_LOW_BATTERY;
        shall_report |= REPORT_STATUS;
    } else if (dev_ctx.power_config_attr.battery_alarm_state == 0 && (dev_ctx.metering_attr.base.status & ZB_ZCL_METERING_GAS_LOW_BATTERY) != 0) {
        dev_ctx.metering_attr.base.status &= ~ZB_ZCL_METERING_GAS_LOW_BATTERY;
        shall_report |= REPORT_STATUS;
    }
    report_event_post(shall_report);
}

/**
 * @brief Set the alarm state as battery unavailable
 * 
 */
void set_battery_unavailable(void)
{
    dev_ctx.power_config_attr.battery_alarm_state |= ZB_ZCL_POWER_CONFIG_MAINS_ALARM_MASK_VOLTAGE_UNAVAIL;
    report_event_post(REPORT_BATTERY);
}
#endif

#ifdef FEATURE_MEASURE_FLOW_RATE
/**
 * @brief Get the instantaneous demand value as int32_t
 * 
 * @return int32_t 
 */
int32_t get_instantaneous_demand(void)
{
    return zb_int24_to_int32(&dev_ctx.metering_attr.instantaneous_demand);
}

/**
 * @brief Set the instantaneous demand value from int32_t
 * 
 * @param value 
 */
void set_instantaneous_demand(int32_t value)
{
    zb_int32_to_int24(value, &dev_ctx.metering_attr.instantaneous_demand);
}
#endif

/**
 * @brief Get the current summ value as an uint64 to be saved to NVRAM
 * 
 * @return zb_uint64_t 
 */
zb_uint64_t get_current_summ(void)
{
		uint64_t value;
		k_mutex_lock(&counter_mutex, K_FOREVER);
    value = dev_ctx.metering_attr.base.curr_summ_delivered.high;
    value <<= 32;
    value |= dev_ctx.metering_attr.base.curr_summ_delivered.low;
		k_mutex_unlock(&counter_mutex);

    return value;
}

void zb_counter_increment(void)
{
		k_mutex_lock(&counter_mutex, K_FOREVER);
    dev_ctx.metering_attr.base.curr_summ_delivered.low += 1;
    if (dev_ctx.metering_attr.base.curr_summ_delivered.low == 0) {
        dev_ctx.metering_attr.base.curr_summ_delivered.high += 1;
    }
		k_mutex_unlock(&counter_mutex);
    report_event_post(REPORT_CURRENT_SUMMATION_DELIVERED);
    nvr_schedule_save(NVR_ITEM_COUNTER);

#ifdef FEATURE_MEASURE_FLOW_RATE
    zb_compute_instantaneous_demand();
#endif
}

void zb_counter_set(zb_uint48_t value)
{
		k_mutex_lock(&counter_mutex, K_FOREVER);
    dev_ctx.metering_attr.base.curr_summ_delivered = value;
		k_mutex_unlock(&counter_mutex);

    report_event_post(REPORT_CURRENT_SUMMATION_DELIVERED);
    nvr_schedule_save(NVR_ITEM_COUNTER);
}

static void update_reporting_info(zb_uint8_t ep, zb_uint16_t cluster_id, zb_uint8_t cluster_role, zb_uint16_t attr_id, uint32_t min_change)
{
    zb_zcl_reporting_info_t *reporting_info = zb_zcl_find_reporting_info(ep, cluster_id, cluster_role, attr_id);
    if (reporting_info == NULL) {
        LOG_ERR("Reporting info pointer is NULL (attr_id: %d)",attr_id);
        return;
    }
    // zb_zcl_reporting_info_t new_reporting_info = {0};
    // memcpy(&new_reporting_info, reporting_info, sizeof(zb_zcl_reporting_info_t));
    reporting_info->u.send_info.max_interval = (uint16_t)(MUST_SYNC_MINIMUM_TIME + 60);
    reporting_info->u.send_info.min_interval = (uint16_t)MUST_SYNC_MINIMUM_TIME;
    reporting_info->u.send_info.delta.u48 = (zb_uint48_t){
        .low = min_change,
        .high = 0
    };
    
    zb_zcl_update_reporting_info(reporting_info);
}

void app_configure_reporting(void)
{
    update_reporting_info(
        GAS_METER_ENDPOINT,
        ZB_ZCL_CLUSTER_ID_POWER_CONFIG,
        ZB_ZCL_CLUSTER_SERVER_ROLE,
        ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID,
        (zb_uint32_t)0
    );
    update_reporting_info(
        GAS_METER_ENDPOINT,
        ZB_ZCL_CLUSTER_ID_POWER_CONFIG,
        ZB_ZCL_CLUSTER_SERVER_ROLE,
        ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_ALARM_STATE_ID,
        (zb_uint32_t)0
    );
    update_reporting_info(
        GAS_METER_ENDPOINT,
        ZB_ZCL_CLUSTER_ID_METERING,
        ZB_ZCL_CLUSTER_SERVER_ROLE,
        ZB_ZCL_ATTR_METERING_CURRENT_SUMMATION_DELIVERED_ID,
        (zb_uint32_t)COUNTER_REPORT_DIFF
    );
#ifdef FEATURE_MEASURE_FLOW_RATE
    update_reporting_info(
        GAS_METER_ENDPOINT,
        ZB_ZCL_CLUSTER_ID_METERING,
        ZB_ZCL_CLUSTER_SERVER_ROLE,
        ZB_ZCL_ATTR_METERING_INSTANTANEOUS_DEMAND_ID,
        (zb_uint32_t)0
    );
#endif
    update_reporting_info(
        GAS_METER_ENDPOINT,
        ZB_ZCL_CLUSTER_ID_METERING,
        ZB_ZCL_CLUSTER_SERVER_ROLE,
        ZB_ZCL_ATTR_METERING_STATUS_ID,
        (zb_uint32_t)0
    );
    update_reporting_info(
        GAS_METER_ENDPOINT,
        ZB_ZCL_CLUSTER_ID_METERING,
        ZB_ZCL_CLUSTER_SERVER_ROLE,
        ZB_ZCL_ATTR_METERING_EXTENDED_STATUS_ID,
        (zb_uint32_t)0
    );
    update_reporting_info(
        GAS_METER_ENDPOINT,
        ZB_ZCL_CLUSTER_ID_POWER_CONFIG,
        ZB_ZCL_CLUSTER_SERVER_ROLE,
        ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_ID,
        (zb_uint32_t)0
    );
}
