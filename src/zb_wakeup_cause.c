#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/logging/log.h>

#include <hal/nrf_power.h>
#include <zboss_api.h>

LOG_MODULE_REGISTER(wakeup_cause, LOG_LEVEL_INF);

#include "zb_wakeup_cause.h"
#include "zb_main_loop.h"
#include "zb_report_event.h"
#include "zb_main_button.h"
#include "zb_reed.h"
#include "zb_zigbee.h"
#include "zb_main.h"
#include "zb_retained.h"
#include "zb_boot_snapshot.h"

static gm_wakeup_cause_t wakeup_cause;

static bool cpu_lockup_is_probable_gpio_wakeup(const struct gm_boot_snapshot *snap)
{
	int expected_reed;
	int reed_now;

	expected_reed = retained_get_next_reed_level();

	if (expected_reed != 0) {
		LOG_WRN("CPU_LOCKUP is not reed inactive wake: expected_reed=%d",
			expected_reed);
		return false;
	}

	if (snap->reed_err != 0) {
		LOG_WRN("CPU_LOCKUP reed workaround rejected: snapshot reed_err=%d",
			snap->reed_err);
		return false;
	}

	if (snap->reed_level != 0) {
		LOG_WRN("CPU_LOCKUP reed workaround rejected: snapshot reed_level=%d expected=0",
			snap->reed_level);
		return false;
	}

	/*
	 * Optional confirmation using a fresh read.
	 * If this read fails, reject the workaround rather than guessing.
	 */
	reed_now = reed_read_early_level();
	if (reed_now < 0) {
		LOG_WRN("CPU_LOCKUP reed workaround rejected: current reed read failed: %d",
			reed_now);
		return false;
	}

	if (reed_now != 0) {
		LOG_WRN("CPU_LOCKUP reed workaround rejected: current reed_level=%d expected=0",
			reed_now);
		return false;
	}

	LOG_WRN("CPU_LOCKUP treated as REED_INACTIVE: snapshot=%d current=%d expected=%d",
		snap->reed_level, reed_now, expected_reed);

	return true;
}

/**
 * @brief Detect wake up reason. Might update some status bits in
 *        the device
 * 
 * @return int 
 */
int detect_cause_init(void)
{
	const struct gm_boot_snapshot *snap = gm_boot_snapshot_get();
	uint32_t reset_cause;
	uint32_t supported;

	bool from_low_power;
	bool by_debug;
	bool by_software;
	bool by_watchdog;
	bool by_lockup;
	bool by_brownout;
	bool by_pin;

	wakeup_cause = GM_WAKEUP_OTHER;

	if (!snap->valid) {
		LOG_ERR("Boot snapshot is not valid");
		return -EIO;
	}

	if (snap->reset_cause_ret != 0) {
		LOG_ERR("Reset cause not supported on this device: ret=%d",
			snap->reset_cause_ret);
		return snap->reset_cause_ret;
	}

	reset_cause = snap->reset_cause;
	supported = snap->supported_reset_cause;

	LOG_DBG("Reset cause raw=0x%08x supported=0x%08x clear_ret=%d",
		reset_cause, supported, snap->clear_ret);

	if (snap->supported_ret != 0) {
		LOG_WRN("Supported reset cause unavailable: ret=%d",
			snap->supported_ret);
	} else if ((reset_cause & ~supported) != 0) {
		LOG_WRN("Reset cause contains unsupported bits: cause=0x%08x supported=0x%08x unsupported=0x%08x",
			reset_cause, supported, reset_cause & ~supported);
	}

	if (reset_cause == 0) {
		LOG_WRN("Reset cause is zero");
	}

	from_low_power = (reset_cause & RESET_LOW_POWER_WAKE) != 0;
	by_debug       = (reset_cause & RESET_DEBUG) != 0;
	by_software    = (reset_cause & RESET_SOFTWARE) != 0;
	by_watchdog    = (reset_cause & RESET_WATCHDOG) != 0;
	by_lockup      = (reset_cause & RESET_CPU_LOCKUP) != 0;
	by_brownout    = (reset_cause & RESET_BROWNOUT) != 0;
	by_pin         = (reset_cause & RESET_PIN) != 0;

	if (by_debug) {
		LOG_DBG("Reset flag: debugger");
	}
	if (by_software) {
		LOG_DBG("Reset flag: software");
	}
	if (from_low_power) {
		LOG_DBG("Reset flag: low power wake");
	}
	if (by_watchdog) {
		LOG_DBG("Reset flag: watchdog");
	}
	if (by_lockup) {
		LOG_DBG("Reset flag: CPU lockup");
	}
	if (by_brownout) {
		LOG_DBG("Reset flag: brownout / power failure");
	}
	if (by_pin) {
		LOG_DBG("Reset flag: reset pin");
	}

	/*
	 * Functional classification:
	 * LOW_POWER_WAKE wins because this application needs to inspect
	 * GPIO state after System OFF.
	 */
	if (from_low_power) {
		LOG_DBG("Wakeup from System OFF by GPIO");
		wakeup_cause = GM_WAKEUP_GPIO_UNKNOWN;
		return 0;
	}

	if (by_watchdog) {
		LOG_DBG("Wakeup from watchdog");
		wakeup_cause = GM_WAKEUP_OTHER;
		set_device_extended_status_bit(ZB_ZCL_METERING_WATCHDOG_ERROR);
		set_device_status_bit(ZB_ZCL_METERING_GAS_CHECK_METER);
		return 0;
	}

	if (by_lockup) {
		if (cpu_lockup_is_probable_gpio_wakeup(snap)) {
			LOG_WRN("Wakeup from reed inactive reported as CPU lockup");
			wakeup_cause = GM_WAKEUP_GPIO_UNKNOWN;
			return 0;
		}
		LOG_DBG("Wakeup from CPU lockup");
		wakeup_cause = GM_WAKEUP_OTHER;
		set_device_extended_status_bit(ZB_ZCL_METERING_PROGRAM_MEMORY_ERROR);
		set_device_status_bit(ZB_ZCL_METERING_GAS_CHECK_METER);
		return 0;
	}

	if (by_debug) {
		LOG_DBG("Reset by debugger");
		wakeup_cause = GM_WAKEUP_OTHER;
		return 0;
	}

	if (by_software) {
		LOG_DBG("Reset by software");
		wakeup_cause = GM_WAKEUP_OTHER;
		return 0;
	}

	if (by_brownout) {
		LOG_DBG("Wakeup from brownout / power failure");
		wakeup_cause = GM_WAKEUP_OTHER;
		set_device_extended_status_bit(ZB_ZCL_METERING_BATTERY_FAILURE);
		set_device_status_bit(ZB_ZCL_METERING_GAS_CHECK_METER);
		return 0;
	}

	if (by_pin) {
		LOG_DBG("Wakeup from reset pin");
		wakeup_cause = GM_WAKEUP_OTHER;
		return 0;
	}

	LOG_DBG("Wakeup from cause 0x%08X", reset_cause);
	wakeup_cause = GM_WAKEUP_OTHER;
	return 0;
}

/**
 * @brief Detects which button was pressed when the system started up and
 *        fires actions accordingly
 * 
 * @return true when check_counter_increment shall be called
 * @return false when no futher actions are needed
 */
gm_wakeup_cause_t wakeup_cause_init(void)
{
	const struct gm_boot_snapshot *snap = gm_boot_snapshot_get();

	switch (wakeup_cause)
	{
	case GM_WAKEUP_GPIO_UNKNOWN:
		bool is_main_button = snap->main_button_level == 1;
		bool is_reed = snap->reed_level == retained_get_next_reed_level();
		if (is_reed) {
			LOG_INF("Seed pulse detected during wakeup");
			wakeup_cause = snap->reed_level == 1 ? GM_WAKEUP_GPIO_REED_PULSE_ACTIVE : GM_WAKEUP_GPIO_REED_PULSE_INACTIVE;
		}
		if (is_main_button) {
			LOG_INF("Main button press detected during wakeup");
			wakeup_cause = snap->main_button_level == GM_WAKEUP_GPIO_MAIN_BTN_PRESS;
			#ifdef FEATURE_DEEP_SLEEP
				set_started_from_deep_sleep(true);
			#endif
			main_button_fire_from_start();
			main_loop_post(SHALL_ENABLE_ZIGBEE);
			report_event_post(REPORT_CURRENT_SUMMATION_DELIVERED);
			#ifdef FEATURE_MEASURE_BATTERY_LEVEL
				main_loop_post(SHALL_MEASURE_BATTERY);
			#endif
		}
		if (!is_main_button && !is_reed) {
			LOG_INF("None of the buttons are detected");
			main_loop_post(SHALL_ENABLE_ZIGBEE);
			report_event_post(REPORT_CURRENT_SUMMATION_DELIVERED);
		}
		break;
	case GM_WAKEUP_OTHER:
		
	default:
		LOG_INF("WAKEUP other");
		main_loop_post(SHALL_ENABLE_ZIGBEE);
		report_event_post(REPORT_CURRENT_SUMMATION_DELIVERED);
		break;
	}
	return wakeup_cause;
}