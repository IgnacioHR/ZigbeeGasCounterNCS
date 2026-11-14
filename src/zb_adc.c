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
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(adc, LOG_LEVEL_INF);

#include "zb_features.h"
#include "zb_adc.h"
#include "zb_zigbee.h"
#include "zb_zigbee_ctx.h"

#ifdef FEATURE_MEASURE_BATTERY_LEVEL

/**
 * @brief The ADC will be measured when the user press main button or when the
 *        elapsed time since the previous measure is higher than the value in
 * 				the constant TIME_TO_EXPIRE_ADC_SECONDS (adjusted to 12 hours)
 *        Note, in order to compute the time this device will use the Time cluster
 *        to ask for a time server (the coordinador) and will use that date.
 *        the last time ADC was measured is stored in NVRAM
 */
#define TIME_TO_EXPIRE_ADC_SECONDS			(12 * 60 * 60)

#define ADC_TASK_STACK_SIZE   2048
#define ADC_TASK_PRIORITY        5

static K_SEM_DEFINE(adc_sem, 0, K_SEM_MAX_LIMIT);

static K_MUTEX_DEFINE(adc_mutex);
static bool adc_started = false;

#define BATT_ENABLE_NODE DT_ALIAS(xiao_read_bat_enable)

#if !DT_NODE_HAS_STATUS(BATT_ENABLE_NODE, okay)
#error "Missing XIAO battery frontend in devicetree"
#endif

static const struct gpio_dt_spec batt_enable =
    GPIO_DT_SPEC_GET(BATT_ENABLE_NODE, enable_gpios);
static const struct adc_dt_spec batt_adc = 
	ADC_DT_SPEC_GET(DT_PATH(zephyr_user));

// #define BATT_DIVIDER_R_TOP_OHM     1000000LL
// #define BATT_DIVIDER_R_BOTTOM_OHM   510000LL

/*
 * Tiempo para que el nodo ADC se estabilice tras activar los FET.
 *
 * Si el divisor es de alta impedancia, el nodo ADC y el condensador de
 * muestreo del SAADC pueden necesitar más tiempo. Empieza conservador.
 */
#define BATT_DIVIDER_SETTLE_TIME   K_MSEC(100)
#define BATT_DIVIDER_NXWAIT_TIME   K_MSEC(2)

/**
 * @brief evaluates if it is time to masure battery parameters
 * 
 * @return true 
 * @return false 
 */
bool check_shall_measure_battery(void)
{
	// obtain last time adc value was sent from NVR
	uint32_t old_time = get_old_time();
	// obtain current time in ms since 2000-01-01T00:00:00UTC
	uint32_t current_time = get_current_time();
	// if treshold passed put action to measure battery values
	return old_time == 0 || current_time - old_time > TIME_TO_EXPIRE_ADC_SECONDS;
}

static int adc_enable_init(void)
{
    if (!gpio_is_ready_dt(&batt_enable)) {
        LOG_ERR("XIAO battery GPIO not ready");
        return -ENODEV;
    }

    /* Keep P0.14 in Hi-Z until a measurement is requested. */
    int err = gpio_pin_configure_dt(&batt_enable, GPIO_INPUT);

    if (err != 0) {
        LOG_ERR("Failed to configure battery GPIO: %d", err);
    }

    return err;
}

static int adc_init(void)
{
	if (!adc_is_ready_dt(&batt_adc)) {
		LOG_ERR("Battery ADC device not ready");
		return -ENODEV;
	}
	int err = adc_channel_setup_dt(&batt_adc);
	if (err) {
		LOG_ERR("Failed to setup battery ADC channel: %d", err);
		return err;
	}
	return 0;
}

int adc_gpio_init(void)
{
	int err = adc_enable_init();
	if (err < 0)
		return err;
	err = adc_init();
	return err;
}

static int battery_adc_read_pin_mv(int32_t *adc_mv)
{
	int err;
	int16_t raw;
	struct adc_sequence sequence = {
		.buffer = &raw,
		.buffer_size = sizeof(raw),
	};
	adc_sequence_init_dt(&batt_adc, &sequence);
	err = adc_read_dt(&batt_adc, &sequence);
	if (err) {
		LOG_ERR("adc_read_dt failed: %d", err);
		return err;
	}
	int32_t mv = raw;
	err = adc_raw_to_millivolts_dt(&batt_adc, &mv);
	if (err) {
		LOG_ERR("adc_raw_to_millivolts_dt failed: %d raw=%d", err, raw);
		return err;
	}
	*adc_mv = mv;
	LOG_DBG("Battery ADC raw=%d adc_mv=%d", raw, mv);
	return 0;
}

static int battery_adc_read_mv(int32_t *battery_mv)
{
    if (battery_mv == NULL) {
        return -EINVAL;
    }

    int32_t adc_mv1;
    int32_t adc_mv2;

    /*
     * Enable the XIAO battery divider.
     * P0.14 is ACTIVE_LOW, so OUTPUT_ACTIVE drives LOW.
     */
    int err = gpio_pin_configure_dt(
        &batt_enable, GPIO_OUTPUT_ACTIVE);

    if (err != 0) {
        LOG_ERR("Failed to enable battery divider: %d", err);
        return err;
    }

    k_sleep(BATT_DIVIDER_SETTLE_TIME);

    err = battery_adc_read_pin_mv(&adc_mv1);

    if (err == 0) {
        k_sleep(BATT_DIVIDER_NXWAIT_TIME);
        err = battery_adc_read_pin_mv(&adc_mv2);
    }

    /*
     * Always release P0.14 to Hi-Z, including on ADC errors.
     * Never drive P0.14 HIGH.
     */
    int disable_err = gpio_pin_configure_dt(
        &batt_enable, GPIO_INPUT);

    if (disable_err != 0) {
        LOG_ERR("Failed to release battery divider: %d",
                disable_err);

        if (err == 0) {
            err = disable_err;
        }
    }

    if (err != 0) {
        return err;
    }

    // int64_t scaled_mv =
    //     (int64_t)adc_mv2 *
    //     (BATT_DIVIDER_R_TOP_OHM + BATT_DIVIDER_R_BOTTOM_OHM) /
    //     BATT_DIVIDER_R_BOTTOM_OHM;
	/*
	* Empirical calibration for XIAO nRF52840 battery divider.
	* Validated at 3.3 V and 3.8 V supply.
	*/
	int64_t scaled_mv = (int64_t)adc_mv2 * 3;

    *battery_mv = (int32_t)scaled_mv;

    LOG_INF("Battery voltage: adc=%d mV battery=%d mV",
            adc_mv2, *battery_mv);

    return 0;
}

static void adc_task(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("Main adc task started");
	while (true) {
		k_sem_take(&adc_sem, K_FOREVER);
		LOG_INF("Going measure ADC input");
		int32_t bat_mv = 0;

		int err = battery_adc_read_mv(&bat_mv);
		if (err < 0) {
			LOG_ERR("Reading battery mv (err: %d)", err);
			set_battery_unavailable();
			continue;
		}
		set_battery_voltage_mv(bat_mv);
	}
}
static K_THREAD_DEFINE(adc_tid, ADC_TASK_STACK_SIZE, adc_task, NULL, NULL, NULL, ADC_TASK_PRIORITY, 0, K_TICKS_FOREVER);

void fire_adc(void)
{
	if (!adc_started) {
		k_mutex_lock(&adc_mutex, K_FOREVER);
		if (!adc_started) {
			k_thread_start(adc_tid);
			adc_started = true;
		}
		k_mutex_unlock(&adc_mutex);
	}
	k_sem_give(&adc_sem);
}

#endif