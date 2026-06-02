#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <sys/time.h>

LOG_MODULE_REGISTER(report_event, LOG_LEVEL_INF);

#include <zboss_api.h>

#include "zb_features.h"
#include "zb_report_event.h"
#include "zb_zigbee.h"
#include "zb_deep_sleep.h"
#include "zb_radio.h"
#include "zb_adc.h"

#define REPORT_EVENT_TASK_STACK_SIZE   2048
#define REPORT_EVENT_TASK_PRIORITY        5

/* The report event queue */
K_EVENT_DEFINE(report_events);

void report_event_post(uint8_t task)
{
	k_event_post(&report_events, task);
}

static uint8_t report_out_event_mask(void)
{
	uint8_t mask = REPORT_CURRENT_SUMMATION_DELIVERED | REPORT_STATUS | REPORT_EXTENDED_STATUS;
#ifdef FEATURE_MEASURE_FLOW_RATE
	mask |= REPORT_INSTANTANEOUS_DEMAND;
#endif
#ifdef FEATURE_MEASURE_BATTERY_LEVEL
	mask |= REPORT_BATTERY;
#endif
	return mask;
}

static uint8_t report_in_event_mask(void)
{
	uint8_t mask = REPORT_REQUEST_TIMING;
	return mask;
}

static uint8_t report_internal_event_mask(void)
{
	uint8_t mask = REPORT_NEW_TIME_ADQUIRED;
	return mask;
}

static void report_event_re_schedule_events_cb(uint8_t param) 
{

}

static void report_event_task(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("Report event action task started");
	radio_report_ctx_init();
	while (true) {
		uint8_t mask_out = report_out_event_mask();
		uint8_t mask_in  = report_in_event_mask();
		uint8_t mask_internal = report_internal_event_mask();
		uint8_t events = k_event_wait_safe(&report_events, mask_out | mask_in | mask_internal, false, K_FOREVER) & 0x000000FF;

		if (!is_leaving_network() && ZB_JOINED()) {
			zb_zcl_status_t status;
			if (events & mask_out) {
				poweroff_mgr_zigbee_tx_begin();
				status = radio_report_values(events);
				if (status == ZB_ZCL_STATUS_SUCCESS) {
					status = radio_send_values(events);
				} else {
					poweroff_mgr_zigbee_tx_done();
				}
			}
			if (events & mask_in) {
				poweroff_mgr_zigbee_rx_begin();
				status = radio_request_values(events);
				if (status != ZB_ZCL_STATUS_SUCCESS) {
					poweroff_mgr_zigbee_rx_done();
				}
			}
			if (events & mask_internal) {
				if (events & REPORT_NEW_TIME_ADQUIRED) {
#ifdef FEATURE_MEASURE_BATTERY_LEVEL
					LOG_DBG("Check if battery shall be measured");
					if (check_shall_measure_battery()) {
						fire_adc();
					}
#endif
				}
			}
			#ifdef FEATURE_DEEP_SLEEP
			poweroff_mgr_user_window_extend(deep_sleep_eval_time_ms(false, false));
			#endif
		} else {
			zb_schedule_app_alarm((zb_callback_t)report_event_re_schedule_events_cb,events, 1000);
			LOG_WRN("Events re-scheduled %d", events);
		}
	}
}
static K_THREAD_DEFINE(report_event_tid, REPORT_EVENT_TASK_STACK_SIZE, report_event_task, NULL, NULL, NULL, REPORT_EVENT_TASK_PRIORITY, 0, K_TICKS_FOREVER);

/**
 * @brief start the thread to process commands specific for the zigbee device
 * 
 */
void report_event_start(void)
{
	k_thread_start(report_event_tid);
}
