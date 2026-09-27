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
#include "zb_version.h"
#include "zb_report_event.h"
#include "zb_main_loop.h"
#include "zb_nvr.h"
#include "zb_deep_sleep.h"
#include "zb_led.h"
#include "zb_ota.h"

#ifdef FEATURE_MEASURE_FLOW_RATE
#include "zb_instantaneous_demand.h"
#endif

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


// #define RADIO_NODE DT_NODELABEL(radio)
// static const struct device *radio = DEVICE_DT_GET(RADIO_NODE);

static bool zigbee_started = false;
static K_MUTEX_DEFINE(zigbee_mutex);

static atomic_t _is_leaving_network = ATOMIC_INIT(false);

static uint32_t last_zcl_time = 0;
static int64_t last_sync_uptime_ms = 0;

extern zb_af_device_ctx_t gas_meter_ctx;

/**
 * @brief returns true when the device is leaving the network for any reason
 *        used to prevent sending values that are not relevant anymore
 * 
 * @return true 
 * @return false 
 */
bool is_leaving_network(void)
{
    return (bool)atomic_get(&_is_leaving_network);
}

/**
 * @brief Function to toggle the identify LED
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
    zb_uint8_t endpoint;
	zb_zcl_device_callback_param_t  *device_cb_param = ZB_BUF_GET_PARAM(bufid, zb_zcl_device_callback_param_t);

    zigbee_fota_zcl_cb(bufid);
    if (device_cb_param->device_cb_id == ZB_ZCL_OTA_UPGRADE_VALUE_CB_ID) {
        return;
    }

	LOG_INF("%s id %hd", __func__, device_cb_param->device_cb_id);

	/* Set default response value. */
	device_cb_param->status = RET_OK;

	switch (device_cb_param->device_cb_id) {
	case ZB_ZCL_SET_ATTR_VALUE_CB_ID:
		cluster_id = device_cb_param->cb_param.
			     set_attr_value_param.cluster_id;
		attr_id = device_cb_param->cb_param.
			  set_attr_value_param.attr_id;
        endpoint = device_cb_param->endpoint;
        LOG_INF("SET_ATTR endpoint=0x%02x cluster=0x%04x attr=0x%04x",
                endpoint, cluster_id, attr_id);
        if (endpoint == GAS_METER_ENDPOINT && cluster_id == ZB_ZCL_CLUSTER_ID_METERING && attr_id == GAS_METER_ATTR_SET_SUMMATION_ID) {
            zb_uint48_t v = device_cb_param->cb_param.set_attr_value_param.values.data48;
            zb_counter_set(v);
            device_cb_param->status = RET_OK;
        } else {
            device_cb_param->status = RET_NOT_IMPLEMENTED;
        }
		break;
	default:
        LOG_INF("zcl_device_cb unhandled");
		break;
	}

	LOG_INF("%s status: %hd", __func__, device_cb_param->status);
}

// static void modify_attr_cb(zb_uint8_t endpoint,
//                                zb_uint16_t cluster_id,
//                                zb_uint16_t attr_id,
//                                zb_uint8_t *new_value)
// {
//     LOG_INF("Modify attr ep=%d cluster=0x%04x attr=0x%04x",
//             endpoint, cluster_id, attr_id);
// #ifdef FEATURE_WRITE_COUNTER_VALUE
//     if (endpoint == GAS_METER_ENDPOINT &&
//         cluster_id == ZB_ZCL_CLUSTER_ID_METERING &&
//         attr_id == GAS_METER_ATTR_SET_SUMMATION_ID) {

//         zb_uint48_t v;
//         memcpy(&v, new_value, sizeof(v));
//         zb_counter_set(v);
//     }
// #endif
// }

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
    poweroff_mgr_block_clear(POF_BLOCK_LEAVE);
    LOG_INF("Leave request status %d", resp->status);
    zb_buf_free(bufid);
}

/**
 * @brief Initiates the process to leave the network.
 * 
 *        Called as a response to main button action of clicking 5 times
 * 
 */
void leave_action(void)
{
    if (ZB_JOINED()) {
        LOG_INF("Leaving network");
        poweroff_mgr_block_set(POF_BLOCK_LEAVE);
        atomic_set(&_is_leaving_network, true);
        zb_bufid_t bufid = zb_buf_get_out();
        if (!bufid) {
            poweroff_mgr_block_clear(POF_BLOCK_LEAVE);
            atomic_set(&_is_leaving_network, false);
            LOG_ERR("No ZBOSS buffer for report");
            return;
        }
        zb_zdo_mgmt_leave_param_t *req = ZB_BUF_GET_PARAM(bufid, zb_zdo_mgmt_leave_param_t);
        ZB_MEMSET(req->device_address, 0, sizeof(zb_ieee_addr_t));
        req->remove_children = ZB_FALSE;
        req->rejoin = ZB_FALSE;
        req->dst_addr = 1;
        zb_get_long_address(req->device_address);
        int seq = zdo_mgmt_leave_req(bufid, leave_callback);
        if (seq == 0xFF) {
            atomic_set(&_is_leaving_network, false);
            poweroff_mgr_block_clear(POF_BLOCK_LEAVE);
            LOG_ERR("Leave request returned error 0xFF");
        }
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
#ifdef FEATURE_DEEP_SLEEP
        poweroff_mgr_block_set(POF_BLOCK_OTA);
#endif
		break;
	case ZIGBEE_FOTA_EVT_FINISHED:
#ifdef FEATURE_DEEP_SLEEP
        poweroff_mgr_user_window_extend(500);
		poweroff_mgr_block_clear(POF_BLOCK_OTA);
#endif
		LOG_INF("Zigbee FOTA finished, reboot required");
#if (IS_ENABLED(CONFIG_LOG))
        log_panic();
#endif
        k_sleep(K_MSEC(100));
	    sys_reboot(SYS_REBOOT_COLD);
		break;
	case ZIGBEE_FOTA_EVT_ERROR:
		LOG_ERR("Zigbee FOTA error");
#ifdef FEATURE_DEEP_SLEEP
        poweroff_mgr_user_window_extend(500);
        poweroff_mgr_block_clear(POF_BLOCK_OTA);
#endif
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
            break;
        case ZB_ZDO_SIGNAL_PRODUCTION_CONFIG_READY:
            break;
        case ZB_COMMON_SIGNAL_CAN_SLEEP:
            break;
        case ZB_SIGNAL_JOIN_DONE:
            LOG_INF("Zigbee join done");
#if HAS_XIAO_RGB_LED && IS_ENABLED(CONFIG_DEBUG)
            xiao_led_blue_set(false);
#endif
            report_event_post(REPORT_REQUEST_TIMING);
#ifdef FEATURE_DEEP_SLEEP
            poweroff_mgr_user_window_extend(10000);
#endif
            if (ota_is_new_image()) {
                main_loop_post(SHALL_CONFIRM_OTA);
            }
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
 * @brief The implementation of the basic cluster on the OTA endpoint does not match the
 *        one we have in our end point so we need to point it to ours.
 * 
 */
static void fix_ota_basic_ep(void)
{
    zb_af_endpoint_desc_t *ota_ep = zb_af_get_endpoint_desc(CONFIG_ZIGBEE_FOTA_ENDPOINT);
    if (ota_ep == NULL) {
        LOG_ERR("OTA EP is null");
        return;
    }
    zb_af_endpoint_desc_t *gas_meter_ep = zb_af_get_endpoint_desc(GAS_METER_ENDPOINT);
    if (gas_meter_ep == NULL) {
        LOG_ERR("GAS METER EP is null");
        return;
    }
    zb_zcl_cluster_desc_t *ota_basic_cluster = get_cluster_desc(ota_ep, ZB_ZCL_CLUSTER_ID_BASIC, ZB_ZCL_CLUSTER_SERVER_ROLE);
    if (ota_basic_cluster == NULL) {
        LOG_ERR("OTA BASIC CLUSTER is null");
        return;
    }
    zb_zcl_cluster_desc_t *gas_basic_cluster = get_cluster_desc(gas_meter_ep, ZB_ZCL_CLUSTER_ID_BASIC, ZB_ZCL_CLUSTER_SERVER_ROLE);
    if (gas_basic_cluster == NULL) {
        LOG_ERR("GAS BASIC CLUSTER is null");
        return;
    }
    ota_basic_cluster->attr_count = gas_basic_cluster->attr_count;
    ota_basic_cluster->attr_desc_list = gas_basic_cluster->attr_desc_list;
    ota_basic_cluster->manuf_code = gas_basic_cluster->manuf_code;
}

static void zb_task(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

    LOG_INF("Configuring zigbee device");
#if HAS_XIAO_RGB_LED && IS_ENABLED(CONFIG_DEBUG)
    xiao_led_blue_set(true);
#endif

    app_zigbee_fota_init();

	/* Register device context (endpoints). */
	ZB_AF_REGISTER_DEVICE_CTX(&gas_meter_ctx);

    fix_ota_basic_ep();

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
    // ZB_ZCL_SET_MODIFY_ATTR_VALUE_CB(modify_attr_cb);
    ZB_ZCL_SET_REPORT_ATTR_CB(report_attr_cb);
    ZB_AF_SET_ENDPOINT_HANDLER(GAS_METER_ENDPOINT, zcl_endpoint_cb);

    zb_zcl_time_init_client();

	zigbee_enable();
}
K_THREAD_DEFINE(zigbee_tid, ZIGBEE_TASK_STACK_SIZE, zb_task, NULL, NULL, NULL, ZIGBEE_TASK_PRIORITY, 0, K_TICKS_FOREVER);

/**
 * @brief Starts identifying the device.
 * 
 * This is doing nothing at this time
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
		if (get_dev_ctx_ptr()->identify_attr.identify_time ==
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
 * @brief Evaluates if the zigbee thread has been started.
 *        
 * Note: This method will not try to start the zigbee thread if it is not started
 * 
 * @return true if the thread was started
 * @return false if the thread has not been started
 */
bool is_zigbee_started(void)
{
    k_mutex_lock(&zigbee_mutex, K_FOREVER);
    bool started = zigbee_started;
    k_mutex_unlock(&zigbee_mutex);
    return started;
}

/**
 * @brief Starts the thread that will set up the zigbee device
 * 
 *        Called from main loop as a response to SHALL_START_ZIGBEE event
 * 
 */
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