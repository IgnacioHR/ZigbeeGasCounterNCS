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

LOG_MODULE_REGISTER(main_loop, LOG_LEVEL_INF);

#include <zephyr/dfu/mcuboot.h>

#include "zb_features.h"
#include "zb_main_loop.h"
#include "zb_zigbee.h"
#include "zb_adc.h"
#include "zb_deep_sleep.h"

#define MAIN_LOOP_TASK_STACK_SIZE   2048
#define MAIN_LOOP_TASK_PRIORITY        5

/* The event queue */
K_EVENT_DEFINE(main_events);

static bool main_loop_started = false;
static K_MUTEX_DEFINE(main_loop_mutex);

static uint32_t main_loop_event_mask(void)
{
	uint32_t mask = SHALL_ENABLE_ZIGBEE | SHALL_DISABLE_ZIGBEE;
#ifdef FEATURE_MEASURE_BATTERY_LEVEL
	mask |= SHALL_MEASURE_BATTERY;
#endif
// #ifdef FEATURE_DEEP_SLEEP
// 	mask |= SHALL_START_DEEP_SLEEP | SHALL_STOP_DEEP_SLEEP;
// #endif
	return mask;
}

static int confirm_new_ota_image(void)
{
	int err = RET_OK;

	if (!boot_is_img_confirmed()) {
    LOG_WRN("MCUboot image is not confirmed, confirming now");

    err = boot_write_img_confirmed();
    if (err != 0) {
      LOG_ERR("Failed to confirm MCUboot image: %d", err);
    } else {
    	LOG_INF("MCUboot image confirmed");
		}
	}
	return err;
}

static void main_loop_task(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("Main button action task started");
	while (true) {
		uint32_t mask = main_loop_event_mask();
		uint32_t events = k_event_wait_safe(&main_events, mask, false, K_FOREVER);

		if (events & SHALL_ENABLE_ZIGBEE) {
			if (!is_zigbee_started()) {
				LOG_INF("Start processing: SHALL_ENABLE_ZIGBEE event");
				zigbee_start();
			}
		}
#ifdef FEATURE_MEASURE_BATTERY_LEVEL
		if (events & SHALL_MEASURE_BATTERY) {
			LOG_INF("Start processing: SHALL_MEASURE_BATTERY event");
			fire_adc();
		}
#endif
		if (events & SHALL_CONFIRM_OTA) {
			if (confirm_new_ota_image()) {
#ifdef FEATURE_DEEP_SLEEP
				poweroff_mgr_block_clear(POF_BLOCK_OTA_CONFIRM);
#endif				
			}
		}
	}
}
static K_THREAD_DEFINE(main_loop_tid, MAIN_LOOP_TASK_STACK_SIZE, main_loop_task, NULL, NULL, NULL, MAIN_LOOP_TASK_PRIORITY, 0, K_TICKS_FOREVER);

void main_loop_post(uint32_t task)
{
	if (!main_loop_started) {
		k_mutex_lock(&main_loop_mutex, K_FOREVER);
		if (!main_loop_started) {
			k_thread_start(main_loop_tid);
			main_loop_started = true;
		}
		k_mutex_unlock(&main_loop_mutex);
	}
	k_event_post(&main_events, task);
}

bool is_main_loop_started(void)
{
	k_mutex_lock(&main_loop_mutex, K_FOREVER);
	bool ret = main_loop_started;
	k_mutex_unlock(&main_loop_mutex);	
	return ret;
}