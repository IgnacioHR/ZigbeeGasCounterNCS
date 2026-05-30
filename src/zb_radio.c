#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(radio, LOG_LEVEL_INF);

#include <zboss_api.h>
#include <zboss_api_addons.h>
#include <zb_zcl_metering.h>

#include "zb_radio.h"
#include "zb_report_event.h"
#include "zb_zigbee.h"
#include "zb_retained.h"
#include "zb_nvr.h"
#include "zb_deep_sleep.h"

#define MAX_PENDING_REPORTS 8

struct pending_report_ctx {
	bool used;
	zb_bufid_t bufid;
	zb_uint8_t ep;
	zb_uint16_t cluster_id;
	zb_uint16_t attr_id;
	zb_uint8_t attr_type;
};

uint16_t num_time_servers_found = 0;

struct report_result_event {
	zb_uint8_t ep;
	zb_uint16_t cluster_id;
	zb_uint16_t attr_id;
	zb_uint8_t attr_type;
	int status;
};

K_MSGQ_DEFINE(report_result_msgq, sizeof(struct report_result_event), 8, 4);

static void report_result_work_handler(struct k_work *work)
{
	struct report_result_event ev;

	while (k_msgq_get(&report_result_msgq, &ev, K_NO_WAIT) == 0) {
		LOG_DBG("Report result: ep=%u cluster=0x%04x attr=0x%04x status=%d",
			ev.ep,
			ev.cluster_id,
			ev.attr_id,
			ev.status);

		if (ev.status == 0) {
			if (ev.attr_id == ZB_ZCL_ATTR_METERING_CURRENT_SUMMATION_DELIVERED_ID) {
				retained_set_last_summation_sent(get_current_summ());
			}
			if (ev.attr_id == ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID) {
				nvr_schedule_save();
			}
		}
	}
}
K_WORK_DEFINE(report_result_work, report_result_work_handler);

static struct pending_report_ctx pending_reports[MAX_PENDING_REPORTS];
static struct k_mutex pending_reports_lock;

zb_zcl_status_t radio_report_values(uint32_t events)
{
	zb_zcl_status_t status = ZB_ZCL_STATUS_SUCCESS;
	if (events & REPORT_CURRENT_SUMMATION_DELIVERED) {
		status = zb_zcl_set_attr_val(
			GAS_METER_ENDPOINT,
			ZB_ZCL_CLUSTER_ID_METERING,
			ZB_ZCL_CLUSTER_SERVER_ROLE,
			ZB_ZCL_ATTR_METERING_CURRENT_SUMMATION_DELIVERED_ID,
			(zb_uint8_t *)&(get_dev_ctx_ptr()->metering_attr).base.curr_summ_delivered,
			false);
		if (status != ZB_ZCL_STATUS_SUCCESS) {
			LOG_ERR("Updating value of current summation delivered: 0x%04x", status);
			return status;
		}
#ifdef FEATURE_WRITE_COUNTER_VALUE
		status = zb_zcl_set_attr_val_manuf(
			GAS_METER_ENDPOINT,
			ZB_ZCL_CLUSTER_ID_METERING,
			ZB_ZCL_CLUSTER_SERVER_ROLE,
			GAS_METER_ATTR_SET_SUMMATION_ID,
			ZB_ZCL_MANUFACTURER_SPECIFIC,
			(zb_uint8_t *)&(get_dev_ctx_ptr()->metering_attr).base.curr_summ_delivered,
			false);
		if (status != ZB_ZCL_STATUS_SUCCESS) {
			LOG_ERR("Updating value of current summation delivered (p): 0x%04x", status);
			return status;
		}
#endif
	}
#ifdef FEATURE_MEASURE_FLOW_RATE
	if (events & REPORT_INSTANTANEOUS_DEMAND) {
		status = zb_zcl_set_attr_val(
			GAS_METER_ENDPOINT,
			ZB_ZCL_CLUSTER_ID_METERING,
			ZB_ZCL_CLUSTER_SERVER_ROLE,
			ZB_ZCL_ATTR_METERING_INSTANTANEOUS_DEMAND_ID, 
			(zb_uint8_t *)&(get_dev_ctx_ptr()->metering_attr).instantaneous_demand,
			false);
		if (status != ZB_ZCL_STATUS_SUCCESS) {
			LOG_ERR("Updating value of instantaneous demand: 0x%04x", status);
			return status;
		}
	}
#endif
#ifdef FEATURE_MEASURE_BATTERY_LEVEL
	if (events & REPORT_BATTERY) {
		status = zb_zcl_set_attr_val(GAS_METER_ENDPOINT,
			ZB_ZCL_CLUSTER_ID_POWER_CONFIG, ZB_ZCL_CLUSTER_SERVER_ROLE,
			ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID, 
			(zb_uint8_t *)&(get_dev_ctx_ptr()->power_config_attr).battery_percentage,
			false);
		if (status != ZB_ZCL_STATUS_SUCCESS) {
			LOG_ERR("Updating value of battery percentage: 0x%04x", status);
			return status;
		}
		status = zb_zcl_set_attr_val(GAS_METER_ENDPOINT,
			ZB_ZCL_CLUSTER_ID_POWER_CONFIG, ZB_ZCL_CLUSTER_SERVER_ROLE,
			ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_ID, 
			(zb_uint8_t *)&(get_dev_ctx_ptr()->power_config_attr).battery_voltage, 
			false);
		if (status != ZB_ZCL_STATUS_SUCCESS) {
			LOG_ERR("Updating value of battery voltage: 0x%04x", status);
			return status;
		}
		status = zb_zcl_set_attr_val(GAS_METER_ENDPOINT,
			ZB_ZCL_CLUSTER_ID_POWER_CONFIG, ZB_ZCL_CLUSTER_SERVER_ROLE,
			ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_ALARM_STATE_ID, 
			(zb_uint8_t *)&(get_dev_ctx_ptr()->power_config_attr).battery_alarm_state,
			false);
		if (status != ZB_ZCL_STATUS_SUCCESS) {
			LOG_ERR("Updating value of battery alarm state: 0x%04x", status);
			return status;
		}		
	}
#endif
	if (events & REPORT_STATUS) {
		status = zb_zcl_set_attr_val(GAS_METER_ENDPOINT,
			ZB_ZCL_CLUSTER_ID_METERING, ZB_ZCL_CLUSTER_SERVER_ROLE,
			ZB_ZCL_ATTR_METERING_STATUS_ID, 
			(zb_uint8_t *)&(get_dev_ctx_ptr()->metering_attr).base.status,
			false);
		if (status != ZB_ZCL_STATUS_SUCCESS) {
			LOG_ERR("Updating value of status: 0x%04x", status);
			return status;
		}
	}
	if (events & REPORT_EXTENDED_STATUS) {
		status = zb_zcl_set_attr_val(GAS_METER_ENDPOINT,
			ZB_ZCL_CLUSTER_ID_METERING, ZB_ZCL_CLUSTER_SERVER_ROLE,
			ZB_ZCL_ATTR_METERING_EXTENDED_STATUS_ID, 
			(zb_uint8_t *)&(get_dev_ctx_ptr()->metering_attr).device_extended_status,
			false);
		if (status != ZB_ZCL_STATUS_SUCCESS) {
			LOG_ERR("Updating value of extended status: 0x%04x", status);
			return status;
		}
	}	
	return status;
}

static bool pending_report_take(zb_bufid_t bufid, struct pending_report_ctx *out)
{
	bool found = false;

	k_mutex_lock(&pending_reports_lock, K_FOREVER);
	for (size_t i = 0; i < ARRAY_SIZE(pending_reports); i++) {
		if (pending_reports[i].used &&
		    pending_reports[i].bufid == bufid) {
			*out = pending_reports[i];
			pending_reports[i].used = false;
			found = true;
		}
	}
	bool more_pending = false;
	for (size_t i = 0; i < ARRAY_SIZE(pending_reports); i++) {
		if (pending_reports[i].used) {
			more_pending = true;
			break;
		}
	}
	k_mutex_unlock(&pending_reports_lock);
	
	if (!more_pending)
		poweroff_mgr_zigbee_tx_done();

	return found;
}

static void report_send_cb(zb_bufid_t bufid)
{
	struct pending_report_ctx ctx;
	struct report_result_event ev;
	if (!pending_report_take(bufid, &ctx)) {
		LOG_WRN("Report send cb for unknown bufid=%u", bufid);
		zb_buf_free(bufid);
		return;
	}
	ev.ep = ctx.ep;
	ev.cluster_id = ctx.cluster_id;
	ev.attr_id = ctx.attr_id;
	ev.attr_type = ctx.attr_type;
	ev.status = 0; /* sustituir por status real si lo obtienes */
	(void)k_msgq_put(&report_result_msgq, &ev, K_NO_WAIT);
	k_work_submit(&report_result_work);

	zb_buf_free(bufid);
	LOG_DBG("Report send cb bufid=%d", bufid);
}

static int pending_report_put(zb_bufid_t bufid, zb_uint8_t ep, zb_uint16_t cluster_id, zb_uint16_t attr_id, zb_uint8_t attr_type)
{
	int ret = -ENOMEM;
	k_mutex_lock(&pending_reports_lock, K_FOREVER);
	for (size_t i = 0; i < ARRAY_SIZE(pending_reports); i++) {
		if (!pending_reports[i].used) {
			pending_reports[i].used = true;
			pending_reports[i].bufid = bufid;
			pending_reports[i].ep = ep;
			pending_reports[i].cluster_id = cluster_id;
			pending_reports[i].attr_id = attr_id;
			pending_reports[i].attr_type = attr_type;
			ret = 0;
			break;
		}
	}
	k_mutex_unlock(&pending_reports_lock);
	return ret;
}

static zb_ret_t radio_write_attr(zb_uint8_t ep, zb_uint16_t cluster_id, zb_uint16_t attr_id, zb_uint8_t attr_type, zb_uint8_t *value_ptr)
{
	zb_bufid_t bufid;
	zb_uint8_t *ptr;
	zb_addr_u dst_addr;

	bufid = zb_buf_get_out();
	if (!bufid) {
		LOG_ERR("No ZBOSS buffer for report");
		return -ENOBUFS;
	}

	int ret = pending_report_put(bufid, ep, cluster_id, attr_id, attr_type);
	if (ret != 0) {
		LOG_ERR("No pending-report slot for bufid=%u", bufid);
		zb_buf_free(bufid);
		return ret;
	}

	ptr = ZB_ZCL_START_PACKET(bufid);
	ZB_ZCL_CONSTRUCT_GENERAL_COMMAND_REQ_FRAME_CONTROL(ptr, ZB_ZCL_DISABLE_DEFAULT_RESPONSE);
	ZB_ZCL_CONSTRUCT_COMMAND_HEADER(ptr, ZB_ZCL_GET_SEQ_NUM(), ZB_ZCL_CMD_REPORT_ATTRIB);
	ZB_ZCL_PACKET_PUT_DATA16_VAL(ptr, attr_id);
	ZB_ZCL_PACKET_PUT_DATA8(ptr, attr_type);
	switch (attr_type) {
	case ZB_ZCL_ATTR_TYPE_U48: 
	{
		zb_uint48_t value;
		ZB_MEMCPY(&value, value_ptr, zb_zcl_get_attribute_size(attr_type, value_ptr));
		ZB_ZCL_PACKET_PUT_DATA48_VAL(ptr, value);
		break;
	}
	case ZB_ZCL_ATTR_TYPE_S24:
	{
		zb_int24_t value;
		ZB_MEMCPY(&value, value_ptr, zb_zcl_get_attribute_size(attr_type, value_ptr));
		ZB_ZCL_PACKET_PUT_DATA24_VAL(ptr, value);
		break;
	}
	case ZB_ZCL_ATTR_TYPE_U8:
		ZB_ZCL_PACKET_PUT_DATA8(ptr, *value_ptr);
		break;
	case ZB_ZCL_ATTR_TYPE_32BITMAP:
		{
			zb_uint32_t value;
			ZB_MEMCPY(&value, value_ptr, zb_zcl_get_attribute_size(attr_type, value_ptr));
			ZB_ZCL_PACKET_PUT_DATA32_VAL(ptr, value);
		}
		break;
	case ZB_ZCL_ATTR_TYPE_8BITMAP:
		ZB_ZCL_PACKET_PUT_DATA8(ptr, *value_ptr);
		break;
	case ZB_ZCL_ATTR_TYPE_64BITMAP:
		ZB_ZCL_PACKET_PUT_DATA64(ptr, value_ptr);
		break;
	default:
		LOG_ERR("Unsupported attrubute type");
		return -EINVAL;
	}
	dst_addr.addr_short = 0x0000;
	ret =  zb_zcl_finish_and_send_packet(bufid, ptr, &dst_addr, ZB_APS_ADDR_MODE_16_ENDP_PRESENT, 
		1, ep, ZB_AF_HA_PROFILE_ID, cluster_id, report_send_cb);

	if (ret != RET_OK) {
		struct pending_report_ctx dummy;
		LOG_ERR("zb_zcl_finish_and_send_packet failed: %d", ret);
		(void)pending_report_take(bufid, &dummy);
		/*
		 * Revisa en tu versión si zb_zcl_finish_and_send_packet()
		 * libera el buffer cuando falla. La documentación advierte
		 * usar esta función para poder comprobar el retorno; si tú
		 * has obtenido el buffer manualmente, evita fugas. Si dudas,
		 * revisa el contrato exacto de tu header/implementación.
		 */
		zb_buf_free(bufid);
		return ret;
	}
	return ret;
}

zb_zcl_status_t radio_send_values(uint32_t events)
{
	zb_zcl_status_t status = ZB_ZCL_STATUS_SUCCESS;
	zb_ret_t ret;

	if (events & REPORT_CURRENT_SUMMATION_DELIVERED) {
		ret = radio_write_attr(
			GAS_METER_ENDPOINT,
			ZB_ZCL_CLUSTER_ID_METERING,
			ZB_ZCL_ATTR_METERING_CURRENT_SUMMATION_DELIVERED_ID, 
			ZB_ZCL_ATTR_TYPE_U48, 
			(zb_uint8_t *)&(get_dev_ctx_ptr()->metering_attr).base.curr_summ_delivered
		);
		if (ret != RET_OK) {
			LOG_ERR("Write attribute ZB_ZCL_ATTR_METERING_CURRENT_SUMMATION_DELIVERED_ID failed (err: %d)", ret);
			status = ZB_ZCL_STATUS_FAIL;
		}
#ifdef FEATURE_WRITE_COUNTER_VALUE
		ret = radio_write_attr(
			GAS_METER_ENDPOINT,
			ZB_ZCL_CLUSTER_ID_METERING,
			GAS_METER_ATTR_SET_SUMMATION_ID, 
			ZB_ZCL_ATTR_TYPE_U48, 
			(zb_uint8_t *)&(get_dev_ctx_ptr()->metering_attr).base.curr_summ_delivered
		);
		if (ret != RET_OK) {
			LOG_ERR("Write attribute GAS_METER_ATTR_SET_SUMMATION_ID failed (err: %d)", ret);
			status = ZB_ZCL_STATUS_FAIL;
		}
#endif
	}
#ifdef FEATURE_MEASURE_FLOW_RATE
	if (events & REPORT_INSTANTANEOUS_DEMAND) {
		ret = radio_write_attr(
			GAS_METER_ENDPOINT,
			ZB_ZCL_CLUSTER_ID_METERING,
			ZB_ZCL_ATTR_METERING_INSTANTANEOUS_DEMAND_ID, 
			ZB_ZCL_ATTR_TYPE_S24, 
			(zb_uint8_t *)&(get_dev_ctx_ptr()->metering_attr).instantaneous_demand
		);
		if (ret != RET_OK) {
			LOG_ERR("Write attribute GAS_METER_ATTR_SET_SUMMATION_ID failed (err: %d)", ret);
			status = ZB_ZCL_STATUS_FAIL;
		}
	}
#endif
#ifdef FEATURE_MEASURE_BATTERY_LEVEL
	if (events & REPORT_BATTERY) {
		ret = radio_write_attr(
			GAS_METER_ENDPOINT,
			ZB_ZCL_CLUSTER_ID_POWER_CONFIG,
			ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID, 
			ZB_ZCL_ATTR_TYPE_U8, 
			(zb_uint8_t *)&(get_dev_ctx_ptr()->power_config_attr).battery_percentage
		);
		if (ret != RET_OK) {
			LOG_ERR("Write attribute ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID failed (err: %d)", ret);
			status = ZB_ZCL_STATUS_FAIL;
		}
		ret = radio_write_attr(
			GAS_METER_ENDPOINT,
			ZB_ZCL_CLUSTER_ID_POWER_CONFIG,
			ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_ID, 
			ZB_ZCL_ATTR_TYPE_U8, 
			(zb_uint8_t *)&(get_dev_ctx_ptr()->power_config_attr).battery_voltage
		);
		if (ret != RET_OK) {
			LOG_ERR("Write attribute ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_ID failed (err: %d)", ret);
			status = ZB_ZCL_STATUS_FAIL;
		}
		ret = radio_write_attr(
			GAS_METER_ENDPOINT,
			ZB_ZCL_CLUSTER_ID_POWER_CONFIG,
			ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_ALARM_STATE_ID, 
			ZB_ZCL_ATTR_TYPE_32BITMAP, 
			(zb_uint8_t *)&(get_dev_ctx_ptr()->power_config_attr).battery_alarm_state
		);
		if (ret != RET_OK) {
			LOG_ERR("Write attribute ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_ALARM_STATE_ID failed (err: %d)", ret);
			status = ZB_ZCL_STATUS_FAIL;
		}
	}
#endif
	if (events & REPORT_STATUS) {
		ret = radio_write_attr(
			GAS_METER_ENDPOINT,
			ZB_ZCL_CLUSTER_ID_METERING,
			ZB_ZCL_ATTR_METERING_STATUS_ID, 
			ZB_ZCL_ATTR_TYPE_8BITMAP, 
			(zb_uint8_t *)&(get_dev_ctx_ptr()->metering_attr).base.status
		);
		if (ret != RET_OK) {
			LOG_ERR("Write attribute ZB_ZCL_ATTR_METERING_STATUS_ID failed (err: %d)", ret);
			status = ZB_ZCL_STATUS_FAIL;
		}
	}
	if (events & REPORT_EXTENDED_STATUS) {
		ret = radio_write_attr(
			GAS_METER_ENDPOINT,
			ZB_ZCL_CLUSTER_ID_METERING,
			ZB_ZCL_ATTR_METERING_EXTENDED_STATUS_ID, 
			ZB_ZCL_ATTR_TYPE_64BITMAP, 
			(zb_uint8_t *)&(get_dev_ctx_ptr()->metering_attr).device_extended_status
		);
		if (ret != RET_OK) {
			LOG_ERR("Write attribute ZB_ZCL_ATTR_METERING_EXTENDED_STATUS_ID failed (err: %d)", ret);
			status = ZB_ZCL_STATUS_FAIL;
		}
	}
	return status;
}

static void time_read_attr_send_cb(zb_uint8_t param)
{
	LOG_INF("Time Read Attributes request sent, param=%u", param);
	if (param) {
		zb_buf_free(param);
	}
	poweroff_mgr_zigbee_rx_done();
}

static void time_server_start_search_cb(zb_uint8_t param)
{
  zb_zdo_match_desc_resp_t *resp = (zb_zdo_match_desc_resp_t *)zb_buf_begin(param);
  zb_uint8_t dst_endpoint;
  zb_uint16_t short_addr;
  zb_uint8_t *match_ep;
  zb_apsde_data_indication_t *ind = ZB_BUF_GET_PARAM(param, zb_apsde_data_indication_t);
  zb_uint8_t *cmd_ptr;

  if (resp->status == ZB_ZDP_STATUS_SUCCESS && resp->match_len > 0)
  {
		num_time_servers_found++;
		LOG_INF("Time server found cb and is OK");
    /* Match EP list follows right after response header */
    match_ep = (zb_uint8_t*)(resp + 1);

    /* set EP value directly to attribute value */
    /* we are searching for exact cluster, so only 1 EP maybe found */
    dst_endpoint = *match_ep;
    short_addr = ind->src_addr;
    /* ZB_BUF_CLEAR_PARAM(ZB_BUF_FROM_REF(param)); */

    /* Send Read time status and time attributes */
		LOG_INF("Requesting time to short=%d", short_addr);

    ZB_ZCL_GENERAL_INIT_READ_ATTR_REQ(param, cmd_ptr, ZB_ZCL_ENABLE_DEFAULT_RESPONSE);
    ZB_ZCL_GENERAL_ADD_ID_READ_ATTR_REQ(cmd_ptr, (ZB_ZCL_ATTR_TIME_TIME_STATUS_ID));
    ZB_ZCL_GENERAL_ADD_ID_READ_ATTR_REQ(cmd_ptr, (ZB_ZCL_ATTR_TIME_TIME_ID));
    ZB_ZCL_GENERAL_SEND_READ_ATTR_REQ(
        param, cmd_ptr, short_addr, ZB_APS_ADDR_MODE_16_ENDP_PRESENT, dst_endpoint, GAS_METER_ENDPOINT,
         ZB_AF_HA_PROFILE_ID, (ZB_ZCL_CLUSTER_ID_TIME), time_read_attr_send_cb);
  } else if (resp->status == ZB_ZDP_STATUS_TIMEOUT) {
		if (num_time_servers_found == 0) {
			poweroff_mgr_zigbee_rx_done();
		}
	} else {
		LOG_ERR("Time server found cb ERROR (err: %d)",resp->status);
    zb_buf_free(param);
  }
}

static void time_server_start_search(zb_uint8_t param)
{
  zb_zdo_match_desc_param_t *req;

  req = zb_buf_initial_alloc(param, sizeof(zb_zdo_match_desc_param_t) + (1) * sizeof(zb_uint16_t));

  req->nwk_addr = ZB_NWK_BROADCAST_RX_ON_WHEN_IDLE;
  req->addr_of_interest = ZB_NWK_BROADCAST_RX_ON_WHEN_IDLE;
  req->profile_id = ZB_AF_HA_PROFILE_ID;
  req->num_in_clusters = 1;
  req->num_out_clusters = 0;
  req->cluster_list[0] = ZB_ZCL_CLUSTER_ID_TIME;

	LOG_INF("Broadcasting network for time servers...");
	num_time_servers_found = 0;
  zb_uint8_t seq = zb_zdo_match_desc_req(param, time_server_start_search_cb);
	if (seq == 0xFF) {
		LOG_ERR("zb_zdo_match_desc_req returned 0xFF");
	}
}

zb_zcl_status_t radio_request_values(uint32_t events)
{
	zb_zcl_status_t status = ZB_ZCL_STATUS_SUCCESS;
	if (events & REPORT_REQUEST_TIMING) {
		LOG_INF("Report Request Timing started");
		zb_bufid_t bufid = zb_buf_get_out();
		if (!bufid) {
			LOG_ERR("No ZBOSS buffer for report");
			return -ENOBUFS;
		}
		time_server_start_search(bufid);
	}
	return status;
}

void radio_report_ctx_init(void)
{
	k_mutex_init(&pending_reports_lock);
}
