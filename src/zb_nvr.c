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
#include <zephyr/settings/settings.h>
#include <zigbee/zigbee_app_utils.h>

LOG_MODULE_REGISTER(nvr, LOG_LEVEL_INF);

#include <zephyr/logging/log_ctrl.h>

#include "zb_features.h"
#include "zb_nvr.h"
#include "zb_zigbee_ctx.h"
#include "zb_reed.h"
#include "zb_deep_sleep.h"

#define NVR_TASK_STACK_SIZE   2048
#define NVR_TASK_PRIORITY        5

static bool nvr_started = false;
static int nvr_load_result;
static K_MUTEX_DEFINE(nvr_mutex);
static uint32_t nvr_dirty_mask;

#define GM_SETTINGS_COUNTER_KEY 		"gm/counter"
#define GM_SETTINGS_TIME_KEY 				"gm/time"

static struct k_work_delayable nvr_work;

static void nvr_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	uint32_t items;

	k_mutex_lock(&nvr_mutex, K_FOREVER);
	items = nvr_dirty_mask;
	nvr_dirty_mask &= ~items;
	k_mutex_unlock(&nvr_mutex);

	if (items & NVR_ITEM_COUNTER) {
		uint64_t to_save_count = get_current_summ();
		int err = settings_save_one(GM_SETTINGS_COUNTER_KEY, &to_save_count, sizeof(to_save_count));
		if (err != 0) {
			LOG_ERR("Error saving counter to settings: %d", err);
			set_device_extended_status_bit(ZB_ZCL_METERING_NV_MEMORY_ERROR);
			set_device_status_bit(ZB_ZCL_METERING_GAS_CHECK_METER);
		} else {
			LOG_INF("Counter value stored %llu", to_save_count);
		}
#ifdef CONFIG_DEBUG
		reed_led_off();
#endif
	}
	if (items & NVR_ITEM_BAT_TIME) {
		uint32_t to_save_time = get_old_time();
		if (to_save_time > 0) {
			int err = settings_save_one(GM_SETTINGS_TIME_KEY, &to_save_time, sizeof(to_save_time));
			if (err != 0) {
				LOG_ERR("Error saving coordinator time to settings: %d", err);
				set_device_extended_status_bit(ZB_ZCL_METERING_NV_MEMORY_ERROR);
				set_device_status_bit(ZB_ZCL_METERING_GAS_CHECK_METER);
			} else {
				LOG_INF("Battery time stored %d", to_save_time);
			}
		}
	}
	k_mutex_lock(&nvr_mutex, K_FOREVER);
	bool pending = nvr_dirty_mask != 0;
	k_mutex_unlock(&nvr_mutex);

	if (pending) {
		k_work_reschedule(&nvr_work, K_NO_WAIT);
		return;
	}
	
#ifdef FEATURE_DEEP_SLEEP
	poweroff_mgr_block_clear(POF_BLOCK_SAVE_NVS);
#endif
}

static int counter_set_from_u64(uint64_t value)
{
	zb_uint48_t z_value = {
		.high = (value>>32) & 0xFFFFu,
		.low = value & 0xFFFFFFFFu
	};
	set_init_current_summ(z_value);

	LOG_INF("Counter value set to %lld",value);
	return 0;
}

static int time_set_from_u32(uint32_t value)
{
	set_init_old_time(value);

	LOG_INF("Time value set to %d",value);
	return 0;
}

static int counter_load_cb(const char *name, size_t len, settings_read_cb read_cb, void* cb_arg, void *param)
{
	uint64_t saved_count;
	bool *found = param;

	ARG_UNUSED(name);

	if (len != sizeof(saved_count)) {
		LOG_ERR("Invalid stored counter size: %zu", len);
		return -EINVAL;
	}
	int ret = read_cb(cb_arg, &saved_count, sizeof(saved_count));

	if (ret < 0) {
		LOG_ERR("Error reading counter from settings: %d", ret);
		return ret;
	}

	if (ret != sizeof(saved_count)) {
		LOG_ERR("Short read of counter from settings: %d", ret);
		return -EIO;
	}

	ret = counter_set_from_u64(saved_count);
	if (ret != 0) {
		return ret;
	}

	if (found != NULL) {
		*found = true;
	}

	return 0;
}

static int time_load_cb(const char *name, size_t len, settings_read_cb read_cb, void* cb_arg, void *param)
{
	uint32_t saved_time;
	bool *found = param;

	ARG_UNUSED(name);

	if (len != sizeof(saved_time)) {
		LOG_ERR("Invalid stored time size: %zu", len);
		return -EINVAL;
	}
	int ret = read_cb(cb_arg, &saved_time, sizeof(saved_time));

	if (ret < 0) {
		LOG_ERR("Error reading time from settings: %d", ret);
		return ret;
	}

	if (ret != sizeof(saved_time)) {
		LOG_ERR("Short read of time from settings: %d", ret);
		return -EIO;
	}

	ret = time_set_from_u32(saved_time);
	if (ret != 0) {
		return ret;
	}

	if (found != NULL) {
		*found = true;
	}

	return 0;
}

static int load_current_summ_from_nvr(void)
{
	bool found = false;
	int err = settings_load_subtree_direct(GM_SETTINGS_COUNTER_KEY, counter_load_cb, &found);

	if (err != 0) {
			LOG_ERR("Error loading counter from settings: %d", err);
			set_device_extended_status_bit(ZB_ZCL_METERING_NV_MEMORY_ERROR);
			set_device_status_bit(ZB_ZCL_METERING_GAS_CHECK_METER);
			return err;
	}

	if (!found) {
		LOG_INF("Counter not found in memory so starting from 0");
		counter_set_from_u64(0);
	}

	return 0;
}

static int load_current_time_from_nvr(void)
{
	bool found = false;
	int err = settings_load_subtree_direct(GM_SETTINGS_TIME_KEY, time_load_cb, &found);

	if (err != 0) {
			LOG_ERR("Error loading time from settings: %d", err);
			set_device_extended_status_bit(ZB_ZCL_METERING_NV_MEMORY_ERROR);
			set_device_status_bit(ZB_ZCL_METERING_GAS_CHECK_METER);
			return err;
	}

	if (!found) {
		LOG_INF("Time not found in memory");
	}

	return 0;
}

/**
 * @brief initializes tasks to save counter value to NVR and load counter value from NVR
 * 
 */
static void nvr_init(void)
{
	if (nvr_started) {
		return;
	}

	k_mutex_lock(&nvr_mutex, K_FOREVER);
	
	if (nvr_started) {
		k_mutex_unlock(&nvr_mutex);
		return;
	}
	
	k_work_init_delayable(&nvr_work, nvr_work_handler);
#if IS_ENABLED(CONFIG_LOG)
	log_filter_set(NULL, 0, log_source_id_get("fs_nvs"), LOG_LEVEL_WRN);
#endif

	int result = 0;
	
	int	err = settings_subsys_init();
	if (err != 0) {
		LOG_ERR("settings_subsys_init failed: (err: %d)",err);
		// TODO: shall start zigbee radio to report the status change
		set_device_extended_status_bit(ZB_ZCL_METERING_NV_MEMORY_ERROR);
		set_device_status_bit(ZB_ZCL_METERING_GAS_CHECK_METER);
	} else {
		err = load_current_summ_from_nvr();
		if (err != 0) {
			LOG_ERR("Error loading current sum from NVR (err: %d)", err);
			if (result == 0) {
				result = err;
			}
		}
		err = load_current_time_from_nvr();
		if (err != 0) {
			LOG_ERR("Error loading current time from NVR (err: %d)", err);
			if (result == 0) {
				result = err;
			}
		}
	}
	nvr_load_result = result;
	nvr_started = true;

	k_mutex_unlock(&nvr_mutex);
}

/**
 * @brief wait until the NVR has been fully read or return
 *        inmediately if it is already read
 * 
 * @param timeout 
 * @return int 
 */
int nvr_ensure_loaded()
{
	nvr_init();
	return nvr_load_result;
}

/**
 * @brief Schedules a save of counter value to NVS as soon as possible
 * 
 */
void nvr_schedule_save(uint32_t nvr_items_mask)
{
#ifdef FEATURE_DEEP_SLEEP
	poweroff_mgr_block_set(POF_BLOCK_SAVE_NVS);
#endif
	k_mutex_lock(&nvr_mutex, K_FOREVER);
	nvr_dirty_mask |= nvr_items_mask;
	k_mutex_unlock(&nvr_mutex);
  k_work_reschedule(&nvr_work, K_NO_WAIT);
}
