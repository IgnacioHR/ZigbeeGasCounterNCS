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
LOG_MODULE_REGISTER(reed, LOG_LEVEL_INF);

// amount of time to ignore a digital input pin interrupt repetition
#define REED_DEBOUNCE_TIMEOUT 100U /* milliseconds */

#include "zb_reed.h"
#include "zb_retained.h"
#include "zb_nvr.h"
#include "zb_main.h"
#include "zb_zigbee.h"
#include "zb_gpio_stable.h"
#include "zb_boot_snapshot.h"
#include "zb_deep_sleep.h"

#define GM_NVRAM_WAIT_TIMEOUT K_SECONDS(5)

#define REED_THREAD_STACK_SIZE	2048
#define REED_THREAD_PRIORITY			 1

static bool reed_started = false;
static K_MUTEX_DEFINE(reed_mutex);

#define REED_INPUT_NODE DT_ALIAS(reed_input)
static const struct gpio_dt_spec reed_input = GPIO_DT_SPEC_GET(REED_INPUT_NODE, gpios);

#ifdef CONFIG_DEBUG
#define LED1_NODE DT_ALIAS(led1)
static const struct gpio_dt_spec led_reed_input = GPIO_DT_SPEC_GET(LED1_NODE, gpios);
#endif

int reed_read_early_level(void)
{
	int err;
	if (!device_is_ready(reed_input.port)) {
		return -ENODEV;
	}
	err = gpio_pin_configure_dt(&reed_input, GPIO_INPUT);
	if (err < 0) {
		return err;
	}
	return gpio_pin_get_dt(&reed_input);
}

int reed_read_stable_level(void)
{
	return gpio_read_stable_level(&reed_input, 5, K_MSEC(10));
}

static int64_t now_ms(void)
{
	return k_uptime_get();
}

static int64_t elapsed_ms_since(int64_t previous_ms)
{
	if (previous_ms == 0) {
		return INT64_MAX;
	}

	return now_ms() - previous_ms;
}

static bool reed_process_interrupt(void)
{
	bool increment = false;

	static int64_t last_interrupt_time_ms;
	static int64_t last_reed_state_change_time_ms;

	if (last_interrupt_time_ms != 0 && elapsed_ms_since(last_interrupt_time_ms) <= REED_DEBOUNCE_TIMEOUT_MS) {
		return false;
	}

	last_interrupt_time_ms = now_ms();

	bool is_ok = (last_reed_state_change_time_ms == 0);
	if (!is_ok) {
		int64_t elapsed = elapsed_ms_since(last_reed_state_change_time_ms);
		is_ok = elapsed > REED_DEBOUNCE_TIMEOUT_MS;
	}

	if (is_ok) {
		last_reed_state_change_time_ms = now_ms();
		increment = true;
	}

	return increment;
}

#ifdef CONFIG_DEBUG
void reed_led_on()
{
	gpio_pin_set_dt(&led_reed_input, 1);
}

void reed_led_off()
{
	gpio_pin_set_dt(&led_reed_input, 0);
}
#endif

static K_SEM_DEFINE(reed_sem, 0, K_SEM_MAX_LIMIT);

static void reed_input_cb(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	k_sem_give(&reed_sem);
}

/**
 * @brief Called from the ISR when the button is pressed via a semaphore
 * 
 * @param p1 
 * @param p2 
 * @param p3 
 */
static void reed_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_DBG("Reed thread started");
	while (true) {
		k_sem_take(&reed_sem, K_FOREVER);
		poweroff_mgr_block_set(POF_BLOCK_COUNTER);
		check_counter_increment();
		poweroff_mgr_block_clear(POF_BLOCK_COUNTER);
	}
}
static K_THREAD_DEFINE(reed_tid, REED_THREAD_STACK_SIZE, reed_thread, NULL, NULL, NULL, REED_THREAD_PRIORITY, 0, K_TICKS_FOREVER);

static void reed_thread_check_started(void) {
	if (!reed_started) {
		k_mutex_lock(&reed_mutex, K_FOREVER);
		if (!reed_started) {
			k_thread_start(reed_tid);
			reed_started = true;
		}
		k_mutex_unlock(&reed_mutex);
	}
}

void check_counter_increment(void)
{
	reed_thread_check_started();
	if (reed_process_interrupt()) {
#ifdef CONFIG_DEBUG
		reed_led_on();
#endif		
		nvr_wait_loaded(GM_NVRAM_WAIT_TIMEOUT);
		zb_counter_increment();
	}
}

/**
 * @brief Configure GPIO to wake up the device.
 * 
 */
int reed_gpio_wakeup(void)
{
	int err;
	int level;

	if (!device_is_ready(reed_input.port)) {
		LOG_ERR("Reed GPIO device not ready");
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&reed_input, GPIO_INPUT);
	if (err < 0) {
		LOG_ERR("GPIO can't configure reed input (err: %d)", err);
		return err;
	}

	level = reed_read_stable_level();

	if (level == 0) {
		err = gpio_pin_interrupt_configure_dt(&reed_input, GPIO_INT_LEVEL_ACTIVE);
		if (err != 0) {
			return err;
		}

		return retained_set_next_reed_level(1);
	}

	if (level == 1) {
		err = gpio_pin_interrupt_configure_dt(&reed_input, GPIO_INT_LEVEL_INACTIVE);
		if (err != 0) {
			return err;
		}

		return retained_set_next_reed_level(0);
	}

	return -EINVAL;
}

static int reed_input_init(int *level)
{
	int err;

	if (!device_is_ready(reed_input.port)) {
		LOG_ERR("Reed GPIO device not ready");
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&reed_input, GPIO_INPUT);
	if (err < 0) {
		LOG_ERR("GPIO can't configure main button (err: %d)", err);
		return err;
	}

	err = gpio_pin_interrupt_configure_dt(&reed_input, GPIO_INT_EDGE_TO_INACTIVE);
	if (err < 0) {
		LOG_ERR("GPIO can't configure reed input interrupt (err: %d)", err);
	}

	static struct gpio_callback reed_input_cb_data;
	gpio_init_callback(&reed_input_cb_data, reed_input_cb, BIT(reed_input.pin));
	*level = gpio_pin_get_dt(&reed_input);
	err = gpio_add_callback(reed_input.port, &reed_input_cb_data);
	if (err < 0) {
		LOG_ERR("GPIO can't add callback of reed input interrupt (err: %d)", err);
	}

	LOG_DBG("Reed input initialized");
	reed_thread_check_started();

	return 0;
}

#ifdef CONFIG_DEBUG
static int reed_input_led_init(void)
{
	int err = gpio_pin_configure_dt(&led_reed_input, GPIO_OUTPUT_ACTIVE);
	if (err < 0) {
		LOG_ERR("GPIO can't configure reed input led (err: %d)", err);
		return err;
	}

	return 0;
}
#endif

/**
 * @brief Hardware configuration of the reed input gpio pin and interrupt
 * 
 * @param level stores the pin level prior to setup the interrupt
 * @return int 
 */
int reed_gpio_init(int *level)
{
	int err;
	err = reed_input_init(level);
	if (err < 0)
		return err;
#ifdef CONFIG_DEBUG
	err = reed_input_led_init();
	if (err < 0)
		return err;
#endif

	return 0;
}

/**
 * @brief Get the logical level of the reed input
 * 
 * @return int 
 */
int reed_get_level(void)
{
	return gpio_pin_get_dt(&reed_input);
}