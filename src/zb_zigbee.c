#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(zigbee, LOG_LEVEL_INF);

#include "zb_features.h"
#include "zb_zigbee.h"
#include "zb_version.h"
#include "zb_adc.h"
#include "zb_report_event.h"
#include "zb_main_loop.h"
#include "zb_nvr.h"
#include "zb_deep_sleep.h"

#include <zboss_api.h>
#include <zigbee/zigbee_error_handler.h>
#include <zigbee/zigbee_app_utils.h>
#include <zigbee/zigbee_fota.h>
#include <zb_nrf_platform.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/sys/reboot.h>

#define ZIGBEE_TASK_STACK_SIZE              10240
#define ZIGBEE_TASK_PRIORITY                   10
#define ZIGBEE_MAIN_LOOP_TASK_STACK_SIZE     4096
#define ZIGBEE_MAIN_LOOP_TASK_PRIORITY          0

/* LED used for device identification. */
// #define IDENTIFY_LED                        DK_LED4

#define TO_PERCENTAGE           (double)(200.0 / ((double)MAX_BATTERY_VOLTAGE - (double)MIN_BATTERY_VOLTAGE))

// #define RADIO_NODE DT_NODELABEL(radio)
// static const struct device *radio = DEVICE_DT_GET(RADIO_NODE);

static bool zigbee_started = false;
static K_MUTEX_DEFINE(zigbee_mutex);

atomic_t _is_leaving_network = ATOMIC_INIT(false);

/* Zigbee device application context storage. */
static gas_meter_device_ctx_t dev_ctx;

static uint32_t last_zcl_time = 0;
static int64_t last_sync_uptime_ms = 0;
static uint32_t old_zcl_time = 0;

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
 * 
 */
#ifdef FEATURE_MEASURE_FLOW_RATE
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
#else
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
    { GAS_METER_ATTR_SET_SUMMATION_ID, \
      ZB_ZCL_ATTR_TYPE_U48, \
      ZB_ZCL_ATTR_ACCESS_READ_WRITE | ZB_ZCL_ATTR_MANUF_SPEC, \
      ZB_ZCL_MANUFACTURER_SPECIFIC, \
      (void*)(&dev_ctx.metering_attr.base.curr_summ_delivered) \
    }, \
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
static ZBOSS_DECLARE_DEVICE_CTX_2_EP(gas_meter_ctx, 
    zigbee_fota_client_ep,
    ep_gas_meter);

gas_meter_device_ctx_t *get_dev_ctx_ptr(void)
{
    return &dev_ctx;
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
    dev_ctx.metering_attr.base.curr_summ_delivered = value;
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
    if (battery_percentage > 200) {
        battery_percentage = 200;
        dev_ctx.power_config_attr.battery_alarm_state |= ZB_ZCL_POWER_CONFIG_MAINS_ALARM_MASK_VOLTAGE_HIGH;
    } else {
        dev_ctx.power_config_attr.battery_alarm_state &= ~ZB_ZCL_POWER_CONFIG_MAINS_ALARM_MASK_VOLTAGE_HIGH;
    }
    if (dev_ctx.power_config_attr.battery_alarm_state & ZB_ZCL_POWER_CONFIG_MAINS_ALARM_MASK_VOLTAGE_UNAVAIL) {
        // reset battery alarm state
        dev_ctx.power_config_attr.battery_alarm_state &= ~ZB_ZCL_POWER_CONFIG_MAINS_ALARM_MASK_VOLTAGE_UNAVAIL;
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

void set_battery_unavailable(void)
{
    dev_ctx.power_config_attr.battery_alarm_state |= ZB_ZCL_POWER_CONFIG_MAINS_ALARM_MASK_VOLTAGE_UNAVAIL;
    report_event_post(REPORT_BATTERY);
}

/**
 * @brief Access to the current time in seconds since
 *        2000-01-01T00:00:00 UTC (zcl time)
 *        must be called after reception of the coordinator time
 * 
 * @return uint32_t seconds since 2000-01-01T00:00:00 UTC (zcl time)
 * @return 0 in case the coordinator time is missing
 */
uint32_t get_current_time(void)
{
    if (last_zcl_time == 0)
        return 0;
    int64_t now = k_uptime_get();
    int64_t elapsed = now - last_sync_uptime_ms;
    return last_zcl_time + (elapsed / 1000);
}

/**
 * @brief Get the current summ value as an uint64 to be saved to NVRAM
 * 
 * @return zb_uint64_t 
 */
zb_uint64_t get_current_summ(void)
{
    uint64_t value = dev_ctx.metering_attr.base.curr_summ_delivered.high;
    value <<= 32;
    value |= dev_ctx.metering_attr.base.curr_summ_delivered.low;

    return value;
}

void zb_counter_increment(void)
{
    dev_ctx.metering_attr.base.curr_summ_delivered.low += 1;
    if (dev_ctx.metering_attr.base.curr_summ_delivered.low == 0) {
        dev_ctx.metering_attr.base.curr_summ_delivered.high += 1;
    }
    report_event_post(REPORT_CURRENT_SUMMATION_DELIVERED);
    nvr_schedule_save(NVR_ITEM_COUNTER);
}

static void zb_counter_set(zb_uint48_t value)
{
    dev_ctx.metering_attr.base.curr_summ_delivered = value;
    report_event_post(REPORT_CURRENT_SUMMATION_DELIVERED);
    nvr_schedule_save(NVR_ITEM_COUNTER);
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
		ESP_MANUFACTURER_NAME,
		ZB_ZCL_STRING_CONST_SIZE(ESP_MANUFACTURER_NAME));
    ZB_ZCL_SET_STRING_VAL(
		dev_ctx.basic_attr.base.model_id,
		ESP_MODEL_IDENTIFIER,
		ZB_ZCL_STRING_CONST_SIZE(ESP_MODEL_IDENTIFIER));
    ZB_ZCL_SET_STRING_VAL(
		dev_ctx.basic_attr.base.date_code,
		ESP_DATE_CODE,
		ZB_ZCL_STRING_CONST_SIZE(ESP_DATE_CODE));
	dev_ctx.basic_attr.base.power_source = 
#if defined(FEATURE_DEEP_SLEEP) || defined(FEATURE_LIGHT_SLEEP)
        ZB_ZCL_BASIC_POWER_SOURCE_BATTERY
#else
        ZB_ZCL_BASIC_POWER_SOURCE_DC_SOURCE
#endif
;
    ZB_ZCL_SET_STRING_VAL(
		dev_ctx.basic_attr.base.location_id,
		ESP_LOCATION_ID,
		ZB_ZCL_STRING_CONST_SIZE(ESP_LOCATION_ID));
    dev_ctx.basic_attr.base.ph_env = ZB_ZCL_BASIC_ENV_UNSPECIFIED;
    ZB_ZCL_SET_STRING_VAL(
		dev_ctx.basic_attr.base.location_id,
		ESP_LOCATION_ID,
		ZB_ZCL_STRING_CONST_SIZE(ESP_LOCATION_ID));
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
		ESP_PRODUCT_URL,
		ZB_ZCL_STRING_CONST_SIZE(ESP_PRODUCT_URL));
    ZB_ZCL_SET_STRING_VAL(
		dev_ctx.basic_attr.product_code_id,
		ESP_PRODUCT_CODE,
		ZB_ZCL_STRING_CONST_SIZE(ESP_PRODUCT_CODE));

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
    dev_ctx.metering_attr.instantaneous_demand = (zb_uint24_t){.high=0,.low=0};
    dev_ctx.metering_attr.demand_formatting = ZB_ZCL_METERING_FORMATTING_SET(true, 2, 3);
#endif

    // dev_ctx.ota_attr.ota_upgrade_file_version = OTA_FILE_VERSION;
    // dev_ctx.ota_attr.ota_upgrade_manufacturer = HW_MANUFACTURER_CODE;
    // dev_ctx.ota_attr.ota_upgrade_image_type = OTA_UPGRADE_IMAGE_TYPE;
    // dev_ctx.ota_attr.stack_version = STACK_VERSION;
    // dev_ctx.ota_attr.client_data.timer_query = ZB_ZCL_OTA_UPGRADE_QUERY_TIMER_COUNT_DEF;
    // dev_ctx.ota_attr.client_data.hw_version = OTA_UPGRADE_HW_VERSION;
    // dev_ctx.ota_attr.client_data.max_data_size = OTA_UPGRADE_MAX_DATA_SIZE;
}

static void app_clusters_attr_set(void)
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

static void app_configure_reporting(void)
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
/**@brief Function to toggle the identify LED
 *
 * @param  bufid  Unused parameter, required by ZBOSS scheduler API.
 */
static void toggle_identify_led(zb_bufid_t bufid)
{
	// static int blink_status;

	// dk_set_led(IDENTIFY_LED, (++blink_status) % 2);
	ZB_SCHEDULE_APP_ALARM(toggle_identify_led, bufid, ZB_MILLISECONDS_TO_BEACON_INTERVAL(100));
}

/**@brief Function to handle identify notification events on the first endpoint.
 *
 * @param  bufid  Unused parameter, required by ZBOSS scheduler API.
 */
static void identify_cb(zb_bufid_t bufid)
{
	zb_ret_t zb_err_code;

	if (bufid) {
		/* Schedule a self-scheduling function that will toggle the LED */
		ZB_SCHEDULE_APP_CALLBACK(toggle_identify_led, bufid);
	} else {
		/* Cancel the toggling function alarm and turn off LED */
		zb_err_code = ZB_SCHEDULE_APP_ALARM_CANCEL(toggle_identify_led, ZB_ALARM_ANY_PARAM);
		ZVUNUSED(zb_err_code);

		// dk_set_led(IDENTIFY_LED, 0);
	}
}

/**@brief Callback function for handling ZCL commands.
 *
 * @param[in]   bufid   Reference to Zigbee stack buffer
 *                      used to pass received data.
 */
static void zcl_device_cb(zb_bufid_t bufid)
{
	zb_uint16_t cluster_id;
	zb_uint16_t attr_id;
	zb_zcl_device_callback_param_t  *device_cb_param = ZB_BUF_GET_PARAM(bufid, zb_zcl_device_callback_param_t);

	LOG_INF("%s id %hd", __func__, device_cb_param->device_cb_id);

	/* Set default response value. */
	device_cb_param->status = RET_OK;

	switch (device_cb_param->device_cb_id) {
	case ZB_ZCL_SET_ATTR_VALUE_CB_ID:
		cluster_id = device_cb_param->cb_param.
			     set_attr_value_param.cluster_id;
		attr_id = device_cb_param->cb_param.
			  set_attr_value_param.attr_id;

        LOG_INF("Unhandled cluster attribute id: %d", cluster_id);
        device_cb_param->status = RET_NOT_IMPLEMENTED;
		break;
	default:
        LOG_INF("zcl_device_cb unhandled");
		break;
	}

	LOG_INF("%s status: %hd", __func__, device_cb_param->status);
}

static void modify_attr_cb(zb_uint8_t endpoint,
                               zb_uint16_t cluster_id,
                               zb_uint16_t attr_id,
                               zb_uint8_t *new_value)
{
    LOG_INF("Modify attr ep=%d cluster=0x%04x attr=0x%04x",
            endpoint, cluster_id, attr_id);

    if (endpoint == GAS_METER_ENDPOINT &&
        cluster_id == ZB_ZCL_CLUSTER_ID_METERING &&
        attr_id == ZB_ZCL_ATTR_METERING_CURRENT_SUMMATION_DELIVERED_ID) {

        zb_uint48_t *v = (zb_uint48_t *)new_value;
        zb_counter_set(*v);
    }
}

static void report_attr_cb(zb_zcl_addr_t *addr,
                           zb_uint8_t endpoint,
                           zb_uint16_t cluster_id,
                           zb_uint16_t attr_id,
                           zb_uint8_t attr_type,
                           zb_uint8_t *value)
{
    LOG_INF("Report attr callback param");

    /*
     * Aquí normalmente hay que parsear el buffer ZBOSS.
     * Depende de si quieres recibir reports de otros dispositivos
     * o sólo gestionar reports que envías tú.
     */
}

void leave_callback(zb_uint8_t bufid) {
    atomic_set(&_is_leaving_network, false);
    zb_zdo_mgmt_leave_res_t *resp = (zb_zdo_mgmt_leave_res_t *)zb_buf_begin(bufid);
    zb_buf_free(bufid);
    LOG_INF("Leave request status %d", resp->status);
}

void leave_action(void)
{
    if (ZB_JOINED()) {
        LOG_INF("Leaving network");
// #ifdef FEATURE_DEEP_SLEEP
//         main_loop_post(SHALL_STOP_DEEP_SLEEP);
// #endif
        atomic_set(&_is_leaving_network, true);
        zb_bufid_t bufid = zb_buf_get_out();
        if (!bufid) {
            LOG_ERR("No ZBOSS buffer for report");
            return;
        }
        zb_zdo_mgmt_leave_param_t *req = ZB_BUF_GET_PARAM(bufid, zb_zdo_mgmt_leave_param_t);
        ZB_MEMSET(req->device_address, 0, sizeof(zb_ieee_addr_t));
        req->remove_children = ZB_FALSE;
        req->rejoin = ZB_FALSE;
        req->dst_addr = 1;
        zb_get_long_address(req->device_address);
        zdo_mgmt_leave_req(bufid, leave_callback);
    }
}

// top level comissioning callback
void bdb_start_top_level_commissioning_cb(uint8_t mode_mask)
{
    LOG_INF("On start top level commissioning callback...");
    bdb_start_top_level_commissioning(mode_mask);
}

static void fota_evt_handler(const struct zigbee_fota_evt *evt)
{
	switch (evt->id) {
	case ZIGBEE_FOTA_EVT_PROGRESS:
		LOG_INF("Zigbee FOTA progress");
        poweroff_mgr_block_set(POF_BLOCK_OTA);
		break;
	case ZIGBEE_FOTA_EVT_FINISHED:
		poweroff_mgr_block_clear(POF_BLOCK_OTA);
		LOG_INF("Zigbee FOTA finished, reboot required");
        log_panic();
        k_sleep(K_MSEC(100));
	    sys_reboot(SYS_REBOOT_COLD);
		break;
	case ZIGBEE_FOTA_EVT_ERROR:
		LOG_ERR("Zigbee FOTA error");
        poweroff_mgr_block_clear(POF_BLOCK_OTA);
		break;
	default:
		LOG_WRN("Unknown Zigbee FOTA evt=%d", evt->id);
		break;
	}
}

static int app_zigbee_fota_init(void)
{
	int err;
	err = zigbee_fota_init(fota_evt_handler);
	if (err) {
		LOG_ERR("zigbee_fota_init failed: %d", err);
		return err;
	}
	return 0;
}

void zboss_signal_handler(zb_bufid_t bufid)
{
    // LOG_INF("zboss_signal_handler");
    zb_zdo_app_signal_hdr_t *sig_h = NULL;
    zb_zdo_app_signal_type_t sig = zb_get_app_signal(bufid, &sig_h);
    zb_ret_t status = ZB_GET_APP_SIGNAL_STATUS(bufid);
    // LOG_INF("Zigbee signal: %d status: %d", sig, status);

	/* Update network status LED. */
	// zigbee_led_status_update(bufid, ZIGBEE_NETWORK_STATE_LED);

	/* No application-specific behavior is required.
	 * Call default signal handler.
	 */
    bool skip_default = false;
    switch (sig) {
        case ZB_ZDO_SIGNAL_DEFAULT_START:
            LOG_INF("ZDO DEFAULT START - status: %d", status);
            break;
        case ZB_ZDO_SIGNAL_SKIP_STARTUP:
            LOG_INF("Zigbee commissioning");
            break;
        case ZB_BDB_SIGNAL_DEVICE_FIRST_START:
        case ZB_BDB_SIGNAL_DEVICE_REBOOT:
            if (status == 0) {
#ifdef FEATURE_LIGHT_SLEEP
                gettimeofday(&time_commisioning_started, NULL);
#endif
                LOG_INF("Device started up in%s factory-reset mode", zb_bdb_is_factory_new() ? "" : " non");
                if (zb_bdb_is_factory_new()) {
                    LOG_INF("Start network steering from factory new");
#ifdef FEATURE_DEEP_SLEEP
                    poweroff_mgr_user_window_extend(10000);
#endif
                } else {
                    LOG_INF("Device rebooted");
                }
                report_event_start();
                LOG_INF("Deferred driver initialization successful");
            } else {
#ifdef FEATURE_DEEP_SLEEP
                poweroff_mgr_user_window_extend(10000);
#endif
                LOG_WRN("Failed with status: %d, retrying", status);
                zb_schedule_app_alarm((zb_callback_t)bdb_start_top_level_commissioning_cb, ZB_BDB_INITIALIZATION, 1000);
            }
            break;
        case ZB_BDB_SIGNAL_STEERING:
            #ifdef FEATURE_LIGHT_SLEEP
            gettimeofday(&time_commisioning_started, NULL);
            #endif
            if (status == 0) {
                LOG_INF("Signal steering successful");
                zb_ieee_addr_t extended_pan_id;
                zb_get_extended_pan_id(extended_pan_id);
                LOG_INF("Joined network successfully (Extended PAN ID: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x, PAN ID: 0x%04hx, Channel:%d, Short Address: 0x%04hx)",
                        extended_pan_id[7], extended_pan_id[6], extended_pan_id[5], extended_pan_id[4],
                        extended_pan_id[3], extended_pan_id[2], extended_pan_id[1], extended_pan_id[0],
                        zb_get_pan_id(), zb_get_current_channel(), zb_get_short_address());
            } else {
                LOG_ERR("Network steering was not successful (status: %d)", status);
                zb_schedule_app_alarm((zb_callback_t)bdb_start_top_level_commissioning_cb, ZB_BDB_NETWORK_STEERING, 1000);
            }
            break;
        case ZB_ZDO_SIGNAL_LEAVE_INDICATION:
            LOG_INF("Leave indication signal received");
            break;
        case ZB_ZDO_SIGNAL_LEAVE:
            LOG_INF("Signal leave received");
            zb_zdo_signal_leave_params_t *leave_params = (zb_zdo_signal_leave_params_t *)ZB_ZDO_SIGNAL_GET_PARAMS(sig_h, zb_zdo_signal_leave_params_t);
            if (leave_params && leave_params->leave_type == ZB_NWK_LEAVE_TYPE_RESET) {
                zb_nvram_erase();
                bdb_start_top_level_commissioning(ZB_BDB_NETWORK_STEERING); // steering a new network.
            }
//            atomic_set(_is_leaving_network, false);// not needed as it is handled in the callback function
            break;
        case ZB_ZDO_SIGNAL_PRODUCTION_CONFIG_READY:
            // zb_set_node_descriptor_manufacturer_code(manufacturer_code);
            break;
        case ZB_COMMON_SIGNAL_CAN_SLEEP:
            break;
        case ZB_SIGNAL_JOIN_DONE:
            LOG_INF("Zigbee join done");
            report_event_post(REPORT_REQUEST_TIMING);
            poweroff_mgr_user_window_extend(10000);
            skip_default = true;
            break;
        case ZB_ZDO_DEVICE_UNAVAILABLE:
            LOG_WRN("Device unavailable signal received");
            zb_zdo_device_unavailable_params_t *unavail_params = (zb_zdo_device_unavailable_params_t *)ZB_ZDO_SIGNAL_GET_PARAMS(sig_h, zb_zdo_device_unavailable_params_t);
            LOG_WRN("Device with short address 0x%04x is unavailable", unavail_params->short_addr);
            LOG_WRN("Long address IEEE Address: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
                unavail_params->long_addr[7], unavail_params->long_addr[6], unavail_params->long_addr[5], unavail_params->long_addr[4],
                unavail_params->long_addr[3], unavail_params->long_addr[2], unavail_params->long_addr[1], unavail_params->long_addr[0]);
            skip_default = true;
            break;
        default:
            LOG_INF("ZDO signal: %d, status: %d", sig, status);
            break;
    }

    if (!skip_default) {
	    ZB_ERROR_CHECK(zigbee_default_signal_handler(bufid));
    }

    zigbee_fota_signal_handler(bufid);

	/* All callbacks should either reuse or free passed buffers.
	 * If bufid == 0, the buffer is invalid (not passed).
	 */
	if (bufid) {
		zb_buf_free(bufid);
	}
}

static bool handle_time_read_attr_response(zb_bufid_t bufid, const zb_zcl_parsed_hdr_t *cmd_info)
{
	zb_zcl_read_attr_res_t *attr_resp;
	bool handled = false;

	while (zb_buf_len(bufid) > 0) {
		ZB_ZCL_GENERAL_GET_NEXT_READ_ATTR_RES(bufid, attr_resp);

		if (attr_resp == NULL) {
			LOG_WRN("Malformed Time Read Attributes Response");
			break;
		}

		if (attr_resp->status != ZB_ZCL_STATUS_SUCCESS) {
			LOG_WRN("Time attr 0x%04x read failed status=0x%02x",
				attr_resp->attr_id,
				attr_resp->status);
			handled = true;
			continue;
		}

		switch (attr_resp->attr_id) {
		case ZB_ZCL_ATTR_TIME_TIME_STATUS_ID: {
			uint8_t time_status;

			if (attr_resp->attr_type != ZB_ZCL_ATTR_TYPE_8BITMAP) {
				LOG_WRN("Unexpected TimeStatus type=0x%02x",
					attr_resp->attr_type);
				break;
			}

			time_status = attr_resp->attr_value[0];

			LOG_DBG("TimeStatus=0x%02x", time_status);

			handled = true;
			break;
		}

		case ZB_ZCL_ATTR_TIME_TIME_ID: {
			uint32_t zcl_time;

			if (attr_resp->attr_type != ZB_ZCL_ATTR_TYPE_UTC_TIME) {
				LOG_WRN("Unexpected Time type=0x%02x",
					attr_resp->attr_type);
				break;
			}

			memcpy(&zcl_time, attr_resp->attr_value, sizeof(zcl_time));

			LOG_DBG("ZCL Time=%u", zcl_time);

			last_zcl_time = zcl_time;
			last_sync_uptime_ms = k_uptime_get();

            report_event_post(REPORT_NEW_TIME_ADQUIRED);

			handled = true;
			break;
		}

		default:
			LOG_INF("Unhandled Time attr 0x%04x type=0x%02x",
				attr_resp->attr_id,
				attr_resp->attr_type);
			handled = true;
			break;
		}
	}
	return handled;
}

static zb_uint8_t zcl_endpoint_cb(zb_bufid_t bufid)
{
	zb_uint8_t *payload = zb_buf_begin(bufid);
	zb_uint8_t len = zb_buf_len(bufid);

	zb_zcl_parsed_hdr_t cmd_info;
	ZB_ZCL_COPY_PARSED_HEADER(bufid, &cmd_info);
    zb_zcl_device_callback_param_t *device_cb_param = ZB_BUF_GET_PARAM(bufid, zb_zcl_device_callback_param_t);

    if (cmd_info.cluster_id == ZB_ZCL_CLUSTER_ID_TIME && 
        cmd_info.profile_id == ZB_AF_HA_PROFILE_ID &&
        cmd_info.cmd_direction == ZB_ZCL_FRAME_DIRECTION_TO_CLI &&
        cmd_info.cmd_id == ZB_ZCL_CMD_READ_ATTRIB_RESP &&
        cmd_info.is_common_command
    ) {
        handle_time_read_attr_response(bufid, &cmd_info);
    } else if (
        cmd_info.cluster_id == ZB_ZCL_CLUSTER_ID_METERING && 
        cmd_info.profile_id == ZB_AF_HA_PROFILE_ID &&
        cmd_info.cmd_direction == ZB_ZCL_FRAME_DIRECTION_TO_CLI &&
        cmd_info.cmd_id == ZB_ZCL_CMD_DEFAULT_RESP &&
        cmd_info.is_common_command
    ) {
        zb_zcl_default_resp_payload_t *res = ZB_ZCL_READ_DEFAULT_RESP(bufid);
        if (res->command_id == ZB_ZCL_CMD_REPORT_ATTRIB &&
            res->status == ZB_ZCL_STATUS_SUCCESS
        ) {
            LOG_DBG("Metering report accepted");
        }
    } else if (
        cmd_info.cluster_id == ZB_ZCL_CLUSTER_ID_OTA_UPGRADE &&
        device_cb_param->device_cb_id == ZB_ZCL_OTA_UPGRADE_VALUE_CB_ID
    ) {
        zigbee_fota_zcl_cb(bufid);
    } else {
        LOG_HEXDUMP_INF(payload, len, "zcl_endpoint_cb: ");
        LOG_INF("EP handler: ep=%u cluster=0x%04x cmd=0x%02x profile=0x%04x dir=%u",
            cmd_info.addr_data.common_data.dst_endpoint,
            cmd_info.cluster_id,
            cmd_info.cmd_id,
            cmd_info.profile_id,
            cmd_info.cmd_direction);
    }

	return ZB_FALSE;
}

#include "zb_deep_sleep.h"

static void zb_task(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

    // int err = device_init(radio);
    // if (err && err != -EALREADY) {
    //     LOG_ERR("device_init(radio) failed: %d", err);
    //     return;
    // }
    LOG_INF("Configuring zigbee device");

    app_zigbee_fota_init();

	/* Register device context (endpoints). */
	ZB_AF_REGISTER_DEVICE_CTX(&gas_meter_ctx);

#ifdef FEATURE_LIGHT_SLEEP
    // zb_sleep_enable(true);
    zb_set_rx_on_when_idle(true);
    zb_sleep_set_threshold(50);
    // sleep_enable_gpio_wakeup();
#endif
#ifdef FEATURE_DEEP_SLEEP
    zb_set_rx_on_when_idle(false);
#endif

	app_clusters_attr_set();
    app_configure_reporting();

	/* Register handlers to identify notifications */
	ZB_AF_SET_IDENTIFY_NOTIFICATION_HANDLER(GAS_METER_ENDPOINT, identify_cb);

    ZB_ZCL_REGISTER_DEVICE_CB(zcl_device_cb);
    ZB_ZCL_SET_MODIFY_ATTR_VALUE_CB(modify_attr_cb);
    ZB_ZCL_SET_REPORT_ATTR_CB(report_attr_cb);
    ZB_AF_SET_ENDPOINT_HANDLER(GAS_METER_ENDPOINT, zcl_endpoint_cb);

    zb_zcl_time_init_client();

	zigbee_enable();
}
K_THREAD_DEFINE(zigbee_tid, ZIGBEE_TASK_STACK_SIZE, zb_task, NULL, NULL, NULL, ZIGBEE_TASK_PRIORITY, 0, K_TICKS_FOREVER);

/**@brief Starts identifying the device.
 *
 * @param bufid Unused parameter, required by ZBOSS scheduler API.
 */
void start_identifying(zb_bufid_t bufid)
{
	ZVUNUSED(bufid);

	if (ZB_JOINED()) {
		/* Check if endpoint is in identifying mode,
		 * if not put desired endpoint in identifying mode.
		 */
		if (dev_ctx.identify_attr.identify_time ==
		ZB_ZCL_IDENTIFY_IDENTIFY_TIME_DEFAULT_VALUE) {

			zb_ret_t zb_err_code = zb_bdb_finding_binding_target(GAS_METER_ENDPOINT);

			if (zb_err_code == RET_OK) {
				LOG_INF("Enter identify mode");
			} else if (zb_err_code == RET_INVALID_STATE) {
				LOG_WRN("RET_INVALID_STATE - Cannot enter identify mode");
			} else {
				ZB_ERROR_CHECK(zb_err_code);
			}
		} else {
			LOG_INF("Cancel identify mode");
			zb_bdb_finding_binding_target_cancel();
		}
	} else {
		LOG_WRN("Device not in a network - cannot enter identify mode");
	}
}

/**
 * @brief evaluates if the zigbee thread has been started
 * 
 * @return true 
 * @return false 
 */
bool is_zigbee_started(void)
{
    k_mutex_lock(&zigbee_mutex, K_FOREVER);
    bool started = zigbee_started;
    k_mutex_unlock(&zigbee_mutex);
    return started;
}

bool is_leaving_network(void)
{
    return (bool)atomic_get(&_is_leaving_network);
}

void zigbee_start(void)
{
    if (zigbee_started)
        return;
    k_mutex_lock(&zigbee_mutex, K_FOREVER);
    if (zigbee_started) {
        k_mutex_unlock(&zigbee_mutex);
        return;
    }
    k_thread_start(zigbee_tid);
    zigbee_started = true;
    k_mutex_unlock(&zigbee_mutex);
#ifdef FEATURE_DEEP_SLEEP
    poweroff_mgr_user_window_open(10000);
#endif
}