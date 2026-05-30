#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zigbee/zigbee_app_utils.h>

LOG_MODULE_REGISTER(save_counter, LOG_LEVEL_INF);

#include <zephyr/logging/log_ctrl.h>

#include "zb_features.h"
#include "zb_save_counter.h"
#include "zb_zigbee.h"
#include "zb_reed.h"
#include "zb_deep_sleep.h"

#define SAVE_COUNTER_TASK_STACK_SIZE   2048
#define SAVE_COUNTER_TASK_PRIORITY        5

static bool save_counter_started = false;
static K_MUTEX_DEFINE(save_counter_mutex);

#define GM_SETTINGS_COUNTER_KEY "gm/counter"

static struct gm_nvram_state {
	struct k_sem loaded_sem;
	struct k_mutex lock;
	bool load_done;
	int load_result;
	// uint64_t counter_value;
	bool counter_valid;
} nvram = {
	.loaded_sem = Z_SEM_INITIALIZER(nvram.loaded_sem, 0, 1),
	.lock = Z_MUTEX_INITIALIZER(nvram.lock),
};

static void gm_nvram_mark_loaded(int result)
{
	k_mutex_lock(&nvram.lock, K_FOREVER);
	nvram.load_result = result;
	nvram.load_done = true;
	k_mutex_unlock(&nvram.lock);
	/*
	 * Semáforo binario: límite 1.
	 * Si el main todavía no está esperando, queda disponible.
	 * Si ya estaba esperando, lo despierta.
	 */
	k_sem_give(&nvram.loaded_sem);
}

static struct k_work_delayable save_counter_work;

static void save_counter_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	uint64_t to_save_count = get_current_summ();

	int err = settings_save_one(GM_SETTINGS_COUNTER_KEY, &to_save_count, sizeof(to_save_count));
	if (err == 0) {
		LOG_INF("Counter value stored %lld", to_save_count);
		reed_led_off();
		poweroff_mgr_block_clear(POF_BLOCK_SAVE_NVS);
		/*
			* Equivalente conceptual a reprogramar deep sleep:
			* aquí podrías reprogramar tu k_work_delayable de PM,
			* heartbeat o entrada a System OFF.
			*/
		return;
	}

	LOG_ERR("Error saving counter to settings: %d", err);
	set_device_extended_status_bit(ZB_ZCL_METERING_NV_MEMORY_ERROR);
	set_device_status_bit(ZB_ZCL_METERING_GAS_CHECK_METER);
    // /*
    //  * En vez de xEventGroupSetBits(...), en Zephyr normalmente:
    //  * - marcas flags atómicos
    //  * - haces submit de un work de reporting Zigbee
    //  */

    // atomic_or(&report_flags, REPORT_STATUS | REPORT_EXTENDED_STATUS);
    // k_work_submit(&zigbee_report_work);	
#ifdef FEATURE_DEEP_SLEEP
	poweroff_mgr_block_clear(POF_BLOCK_SAVE_NVS);
#endif
	reed_led_off();
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

static int counter_load_cb(const char *name, size_t len, settings_read_cb read_cb, void* cb_arg, void *param)
{
	uint64_t saved_count;
	bool *found = param;

	ARG_UNUSED(name);

	if (found != NULL) {
		*found = true;
	}

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
		LOG_ERR("Short read from settings: %d", ret);
		return -EIO;
	}

	ret = counter_set_from_u64(saved_count);
	gm_nvram_mark_loaded(ret);

	return ret;
}

static int load_current_summ_from_nvs(void)
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
		return 0;
	}

	return 0;
}

/**
 * @brief initializes tasks to save counter value to NVR and load counter value from NVR
 * 
 */
static void save_counter_init(void)
{
	if (save_counter_started)
		return;
	k_mutex_lock(&save_counter_mutex, K_FOREVER);
	if (save_counter_started) {
		k_mutex_unlock(&save_counter_mutex);
		return;
	}
	
	k_work_init_delayable(&save_counter_work, save_counter_work_handler);
	log_filter_set(NULL, 0, log_source_id_get("fs_nvs"), LOG_LEVEL_WRN);
	int	err = settings_subsys_init();
	if (err != 0) {
		LOG_ERR("settings_subsys_init failed: (err: %d)",err);
		// TODO: shall start zigbee radio to report the status change
		set_device_extended_status_bit(ZB_ZCL_METERING_NV_MEMORY_ERROR);
		set_device_status_bit(ZB_ZCL_METERING_GAS_CHECK_METER);
	} else {
		err = load_current_summ_from_nvs();
		if (err != 0) {
			LOG_ERR("Error loading current sum from NVR (err: %d)", err);
		}
	}
	save_counter_started = true;
	k_mutex_unlock(&save_counter_mutex);
}

/**
 * @brief wait until the NVR has been fully read or return
 *        inmediately if it is already read
 * 
 * @param timeout 
 * @return int 
 */
int save_counter_wait_loaded(k_timeout_t timeout)
{
	int err;
	int result;
	save_counter_init();
	k_mutex_lock(&nvram.lock, K_FOREVER);
	if (nvram.load_done) {
		result = nvram.load_result;
		k_mutex_unlock(&nvram.lock);
		return result;
	}
	k_mutex_unlock(&nvram.lock);
	err = k_sem_take(&nvram.loaded_sem, timeout);
	if (err != 0) {
		return err; /* -EAGAIN si timeout */
	}
	k_mutex_lock(&nvram.lock, K_FOREVER);
	result = nvram.load_result;
	k_mutex_unlock(&nvram.lock);
	return result;
}

/**
 * @brief Schedules a save of counter value to NVS as soon as possible
 * 
 */
void counter_schedule_save(void)
{
	poweroff_mgr_block_set(POF_BLOCK_SAVE_NVS);
  k_work_reschedule(&save_counter_work, K_NO_WAIT);
}
