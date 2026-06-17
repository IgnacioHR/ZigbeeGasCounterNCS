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
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(instantaneous_demand, LOG_LEVEL_INF);

#include "zb_features.h"

#ifdef FEATURE_MEASURE_FLOW_RATE

#include <zboss_api.h>

#include "zb_instantaneous_demand.h"
#include "zb_zigbee_ctx.h"
#include "zb_report_event.h"
#include "zb_deep_sleep.h"

#define TIME_TO_RESET_INSTANTANEOUS_D_MS 12000

static void timer_reset_instantaneous_demand(struct k_work *work)
{
		set_instantaneous_demand(0);
		report_event_post(REPORT_INSTANTANEOUS_DEMAND);
}
static K_WORK_DELAYABLE_DEFINE(reset_instantaneous_demand, timer_reset_instantaneous_demand);

// After two consecutive values of current_summation_delivered this method
// computes the instantaneous_demand
// NOTE: there is one special task to set instantaneous demand to 0 when no
// values arrive
void zb_compute_instantaneous_demand()
{
	static int64_t last_time_counter_incremented;
	int64_t now = k_uptime_get();
	int64_t time_diff_ms = now - last_time_counter_incremented;
	last_time_counter_incremented = now;
	if (time_diff_ms == now)
		return;

	float time_diff_h = time_diff_ms / (1000.0f * 3600.0f); // Convert time to hours/100
	int32_t _instantaneous_demand;
	if (time_diff_h > 0)
	{
			_instantaneous_demand = (int32_t)((1 / time_diff_h) + 0.5f); // compute flow in m³/h
	}
	else
	{
			_instantaneous_demand = 0;
	}

	if (get_instantaneous_demand() != _instantaneous_demand)
	{
		set_instantaneous_demand(_instantaneous_demand);
		
		report_event_post(REPORT_INSTANTANEOUS_DEMAND);
#ifdef FEATURE_DEEP_SLEEP
		poweroff_mgr_user_window_extend(TIME_TO_RESET_INSTANTANEOUS_D_MS+200);
#endif
		k_work_reschedule(&reset_instantaneous_demand, K_MSEC(TIME_TO_RESET_INSTANTANEOUS_D_MS));
	}
}

#endif