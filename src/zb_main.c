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
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#include <zephyr/logging/log_ctrl.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/dfu/mcuboot.h>

#include <zboss_api.h>

#include <zigbee/zigbee_error_handler.h>
#include <zigbee/zigbee_app_utils.h>

#include <zb_nrf_platform.h>
#include <hal/nrf_power.h>
#include <ram_pwrdn.h>

#include "zb_features.h"
#include "zb_main_button.h"
#include "zb_zigbee.h"
#include "zb_main_loop.h"
#include "zb_nvr.h"
#include "zb_adc.h"
#include "zb_wakeup_cause.h"
#include "zb_report_event.h"
#include "zb_retained.h"
#include "zb_reed.h"
#include "zb_deep_sleep.h"
#include "zb_xiao.h"

/* Device endpoint, used to receive ZCL commands. */
#define APP_TEMPLATE_ENDPOINT               10

/* Type of power sources available for the device.
 * For possible values see section 3.2.2.2.8 of ZCL specification.
 */
#define TEMPLATE_INIT_BASIC_POWER_SOURCE    ZB_ZCL_BASIC_POWER_SOURCE_DC_SOURCE

/* LED indicating that device successfully joined Zigbee network. */
// #define ZIGBEE_NETWORK_STATE_LED            DK_LED3

/* Button used to enter the Identify mode. */
// #define IDENTIFY_MODE_BUTTON                DK_BTN4_MSK

/* Button to start Factory Reset */
// #define FACTORY_RESET_BUTTON                IDENTIFY_MODE_BUTTON

#define SLEEP_TIME_MS (10 * 60 * 1000)

static gm_wakeup_cause_t wakeup_cause;

gm_wakeup_cause_t get_wakeup_cause(void)
{
	return wakeup_cause;
}

static int configure_gpio(int *main_button_level, int *reed_level)
{
	int err;

	xiao_battery_frontend_disable();

	err = main_button_gpio_init(main_button_level);
	if (err < 0)
		return err;
	err = reed_gpio_init(reed_level);
	if (err < 0)
		return err;

#ifdef FEATURE_MEASURE_BATTERY_LEVEL
	err = adc_gpio_init();
	if (err < 0)
		return err;
#endif
#if HAS_XIAO_RGB_LED && IS_ENABLED(CONFIG_DEBUG)
	err = xiao_leds_init();
	if (err < 0)
		return err;
#endif

	return 0;
}

static bool wakeup_cause_requires_radio(gm_wakeup_cause_t wakeup_cause)
{
	return wakeup_cause == GM_WAKEUP_OTHER ||
	       wakeup_cause == GM_WAKEUP_GPIO_MAIN_BTN_PRESS;
}

static bool reed_inactive_requires_radio(uint64_t *not_reported)
{
	uint64_t current;
	uint64_t last;

	nvr_wait_loaded(K_SECONDS(2));

	current = get_current_summ();
	last = retained_get_last_summation_sent();

	*not_reported = current - last;

	if (*not_reported >= COUNTER_REPORT_DIFF) {
		LOG_INF("Radio shall be started due to counter rebased max difference current=%lld last=%lld",
			current, last);
		return true;
	}

	return false;
}

static void start_radio_and_report(bool main_button)
{
	main_loop_post(SHALL_ENABLE_ZIGBEE);
	report_event_post(REPORT_CURRENT_SUMMATION_DELIVERED);
#ifdef FEATURE_DEEP_SLEEP
	poweroff_mgr_user_window_extend(deep_sleep_eval_time_ms(main_button, !main_button));
#endif
}

static void log_radio_not_needed_for_reed_inactive(uint64_t not_reported)
{
	int remaining = COUNTER_REPORT_DIFF - not_reported;

	LOG_INF("Still %d pulses remaining to start radio", remaining);
}

static void check_shall_enable_radio(gm_wakeup_cause_t wakeup_cause, bool b_missed_reed)
{
	uint64_t not_reported = 0;
	bool enable_radio;

	enable_radio = wakeup_cause_requires_radio(wakeup_cause);

	if (wakeup_cause == GM_WAKEUP_GPIO_REED_PULSE_INACTIVE || b_missed_reed) {
		enable_radio = reed_inactive_requires_radio(&not_reported);
	} else if (enable_radio) {
		LOG_INF("Radio shall be started due to wakeup_cause");
	}

	if (enable_radio) {
		start_radio_and_report(wakeup_cause == GM_WAKEUP_GPIO_MAIN_BTN_PRESS);
		return;
	}

	if (wakeup_cause == GM_WAKEUP_GPIO_REED_PULSE_INACTIVE || b_missed_reed) {
		log_radio_not_needed_for_reed_inactive(not_reported);
	}
}

static bool is_reed_wakeup_cause(gm_wakeup_cause_t wakeup_cause)
{
	return wakeup_cause == GM_WAKEUP_GPIO_REED_PULSE_INACTIVE || wakeup_cause == GM_WAKEUP_GPIO_REED_PULSE_ACTIVE;
}

static void app_confirm_mcuboot_image(void)
{
    int err;

    if (boot_is_img_confirmed()) {
        return;
    }

    LOG_WRN("MCUboot image is not confirmed, confirming now");

    err = boot_write_img_confirmed();
    if (err != 0) {
        LOG_ERR("Failed to confirm MCUboot image: %d", err);
        return;
    }

    LOG_INF("MCUboot image confirmed");
}

int poweroff(void) {
	int err;
	err = retained_set_boot_to_system_off(false);
	if (err != 0) {
		LOG_ERR("Can't set retained to system off to false (err: %d)", err);
		return err;
	}

	err = main_button_gpio_wakeup();
	if (err != 0) {
		LOG_ERR("Can't configure main button for wakeup (err: %d)", err);
		return err;
	}
	err = reed_gpio_wakeup(); // Internally this is calling retained_flush_sync();
	if (err != 0) {
		LOG_ERR("Can't configure reed for wakeup (err: %d)", err);
		return err;
	}
	if (err == 0) {
		LOG_INF("Powering off now");
#if (IS_ENABLED(CONFIG_LOG))
		log_panic();
#endif
		sys_poweroff();
	}
	return 0;
}

int main(void)
{
	int err;

	LOG_DBG("Starting Zigbee Gas Counter");

	// set ctx initial values before zigbee radio is started so values can be changed anytime from this moment if needed
	retained_init();

	if (IS_ENABLED(CONFIG_RAM_POWER_DOWN_LIBRARY)) {
		power_down_unused_ram();
	}

	app_confirm_mcuboot_image();

	if (retained_shall_power_off()) {
		LOG_DBG("Requesting WDT reset before system_off path");
		err = poweroff();
		if (err != 0) {
			sys_reboot(SYS_REBOOT_COLD);
		}
		CODE_UNREACHABLE;
	}

	app_device_ctx_init();

	int main_button_level;
	int reed_level;
	err = configure_gpio(&main_button_level, &reed_level);
	if (err < 0) {
		return err;
	}

	err = detect_cause_init();
	if (err != 0) {
		LOG_WRN("detect_cause_init returned %d", err);
	}

	wakeup_cause = wakeup_cause_init();
#ifdef FEATURE_DEEP_SLEEP
	poweroff_mgr_enable_for_wakeup(wakeup_cause);
#endif
	// check for missing interrupts, happens if the user releases the button prior to setting up
	// the interrupts
	if (wakeup_cause == GM_WAKEUP_GPIO_MAIN_BTN_PRESS && main_button_level == 0) {
		// the user pressed the button, device was wake up but the user released the button
		// and the interrupt has not been able to catch. so fire event manually now
		main_button_fire_release();
	}
	bool b_missed_reed = false;
	// LOG_INF("wakeup_cause = %d reed_level = %d", wakeup_cause, reed_level);
	if (wakeup_cause == GM_WAKEUP_GPIO_REED_PULSE_ACTIVE && reed_level == 0) {
		// the user has release the button and we missed the event
		b_missed_reed = true;
	}	
	if (wakeup_cause == GM_WAKEUP_GPIO_REED_PULSE_INACTIVE || b_missed_reed)
		check_counter_increment();

	if (!is_reed_wakeup_cause(wakeup_cause)) {
 		main_button_start();
	}
#ifdef FEATURE_MEASURE_FLOW_RATE
	// TODO, timer to reset instantaneous demand to 0
#endif
	check_shall_enable_radio(wakeup_cause, b_missed_reed);
	LOG_DBG("Zigbee Gas Counter started");

#ifdef CONFIG_DEBUG
	reed_led_off();
#endif

	// if main loop has not started we can safely sleep now
	if (!is_main_loop_started()) {
		LOG_INF("Powering off now: direct system_off path");
		err = poweroff();
		if (err != 0) {
			sys_reboot(SYS_REBOOT_COLD);
		}
	}

#ifdef FEATURE_DEEP_SLEEP
	poweroff_mgr_block_clear(POF_BLOCK_APP_NOT_READY);
	poweroff_mgr_try_poweroff_now();
#endif
	return 0;
}
