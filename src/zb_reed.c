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
#include <zephyr/sys/atomic.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(reed, LOG_LEVEL_INF);

// amount of time to ignore a digital input pin interrupt repetition
#define REED_DEBOUNCE_MS      150
#define REED_BOOT_STABLE_MS    50
#define REED_SAMPLE_MS         10

#include "zb_reed.h"
#include "zb_retained.h"
#include "zb_nvr.h"
#include "zb_zigbee_ctx.h"
#include "zb_gpio_stable.h"
#include "zb_deep_sleep.h"

#define GM_NVRAM_WAIT_TIMEOUT K_SECONDS(5)

#define REED_THREAD_STACK_SIZE	2048
#define REED_THREAD_PRIORITY			 1

static bool reed_started = false;
static K_MUTEX_DEFINE(reed_mutex);
static K_MUTEX_DEFINE(reed_fsm_mutex);
static atomic_t reed_irq_generation;

#define REED_INPUT_NODE DT_ALIAS(reed_input)
static const struct gpio_dt_spec reed_input = 
	GPIO_DT_SPEC_GET(REED_INPUT_NODE, gpios);

static struct gpio_callback reed_input_cb_data;
static bool reed_callback_registered;

#ifdef CONFIG_DEBUG
#define LED1_NODE DT_ALIAS(led1)
static const struct gpio_dt_spec led_reed_input = GPIO_DT_SPEC_GET(LED1_NODE, gpios);
#endif

/**
 * @brief Obtains the value of the reed pin
 * 
 * @return 0 if DOWN or INACTIVE
 * @return 1 if UP or ACTIVE
 * @return negative value if error
 */
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

/**
 * @brief Read the pin value until it is stable for 50 msec
 * 
 * @return 0 if DOWN or INACTIVE
 * @return 1 if UP or ACTIVE
 * @return negative value if error
 */
int reed_read_stable_level(void)
{
	return gpio_read_stable_level(&reed_input, 5, K_MSEC(10));
}

#define REED_STABLE_EXTRA_MS 200U

static int reed_sample_stable_level(uint32_t duration_ms)
{
    int level = gpio_pin_get_dt(&reed_input);
    if (level < 0) {
        return level;
    }

    uint32_t stable_ms = 0;
    uint32_t timeout_ms = duration_ms + REED_STABLE_EXTRA_MS;

    for (uint32_t elapsed_ms = 0;
         elapsed_ms < timeout_ms;
         elapsed_ms += REED_SAMPLE_MS) {

        k_msleep(REED_SAMPLE_MS);

        int current = gpio_pin_get_dt(&reed_input);
        if (current < 0) {
            return current;
        }

        if (current != level) {
            level = current;
            stable_ms = 0;
        } else {
            stable_ms += REED_SAMPLE_MS;

            if (stable_ms >= duration_ms) {
                return level;
            }
        }
    }

    return -EAGAIN;
}

static int reed_fsm_apply_level(int level)
{
    if (level != 0 && level != 1) {
        return -EINVAL;
    }

    k_mutex_lock(&reed_fsm_mutex, K_FOREVER);

    int expected = retained_get_next_reed_level();

    if (expected != 0 && expected != 1) {
        k_mutex_unlock(&reed_fsm_mutex);
        return -EINVAL;
    }

    if (level != expected) {
        k_mutex_unlock(&reed_fsm_mutex);
        return 0;
    }

    int err = retained_set_next_reed_level(1 - level);

    if (err != 0) {
        LOG_ERR("REED FSM: cannot update retained state: %d", err);
        k_mutex_unlock(&reed_fsm_mutex);
        return err;
    }

    if (level == 1) {
        LOG_DBG("REED FSM: ACTIVE -> WAIT_INACTIVE");
    } else {
        LOG_DBG("REED FSM: INACTIVE -> WAIT_ACTIVE, COUNT");
    }

    k_mutex_unlock(&reed_fsm_mutex);

    return level == 0 ? 1 : 0;
}

static bool reed_process_interrupt(void)
{
    atomic_val_t generation =
        atomic_get(&reed_irq_generation);

    int level = reed_sample_stable_level(REED_DEBOUNCE_MS);

    if (level < 0) {
        if (level != -EAGAIN) {
            LOG_ERR("REED: GPIO read error: %d", level);
        }
        return false;
    }

    if (generation != atomic_get(&reed_irq_generation)) {
        LOG_DBG("REED: edge detected during debounce");
        return false;
    }

    int result = reed_fsm_apply_level(level);

	if (result < 0) {
		LOG_ERR("REED FSM: transition failed: %d", result);
		return false;
	}

	return result == 1;
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

	atomic_inc(&reed_irq_generation);
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
#ifdef FEATURE_DEEP_SLEEP
		poweroff_mgr_block_set(POF_BLOCK_COUNTER);
#endif
		check_counter_increment();
#ifdef FEATURE_DEEP_SLEEP
		poweroff_mgr_block_clear(POF_BLOCK_COUNTER);
#endif
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

// /**
//  * @brief Check if the counter shall be incremented and do so in case it shall be
//  *        incremented
//  * 
//  */
// void check_counter_increment(void)
// {
// 	reed_thread_check_started();
// 	if (reed_process_interrupt()) {
// #ifdef CONFIG_DEBUG
// 		reed_led_on();
// #endif		
// 		nvr_ensure_loaded();
// 		zb_counter_increment();
// 	}
// }

void reed_count_pulse(void)
{
#ifdef CONFIG_DEBUG
    reed_led_on();
#endif

    nvr_ensure_loaded();
    zb_counter_increment();
}

void check_counter_increment(void)
{
    if (reed_process_interrupt()) {
        reed_count_pulse();
    }
}

/**
 * @brief Configure GPIO to wake up the device.
 * 
 */
int reed_gpio_wakeup(void)
{
	int err;
	// int level;
	// uint16_t next_level;
	// gpio_flags_t wake_flags;

	if (!device_is_ready(reed_input.port)) {
		LOG_ERR("Reed GPIO device not ready");
		return -ENODEV;
	}

	/*
	 * Stop normal runtime processing before converting this GPIO
	 * into a SYSTEMOFF wake-up source.
	 */
	err = gpio_pin_interrupt_configure_dt(&reed_input, GPIO_INT_DISABLE);
	if (err != 0) {
		LOG_ERR("Can't disable reed interrupt (err: %d)", err);
		return err;
	}

	if (reed_callback_registered) {
		err = gpio_remove_callback(reed_input.port, &reed_input_cb_data);
		if (err != 0) {
			LOG_ERR("Can't remove reed callback (err: %d)", err);
			return err;
		}
		reed_callback_registered = false;
	}

	err = gpio_pin_configure_dt(&reed_input, GPIO_INPUT);
	if (err < 0) {
		LOG_ERR("GPIO can't configure reed input (err: %d)", err);
		return err;
	}

	int expected = retained_get_next_reed_level();

	if (expected != 0 && expected != 1) {
		LOG_ERR("Invalid retained reed state: %d", expected);
		return -EINVAL;
	}

	err = gpio_pin_interrupt_configure_dt(
		&reed_input,
		expected == 1
			? GPIO_INT_LEVEL_ACTIVE
			: GPIO_INT_LEVEL_INACTIVE);

	if (err < 0) {
		LOG_ERR("Cannot configure reed wakeup: %d", err);
		return err;
	}

	LOG_DBG("REED FSM: System OFF armed for %s",
			expected == 1 ? "ACTIVE" : "INACTIVE");

	// level = reed_read_stable_level();
	// if (level < 0) {
	// 	return level;
	// }

	// if (level == 0) {
	// 	next_level = 1;
	// 	wake_flags = GPIO_INT_LEVEL_ACTIVE;
	// } else if (level == 1) {
	// 	next_level = 0;
	// 	wake_flags = GPIO_INT_LEVEL_INACTIVE;
	// } else {
	// 	return -EINVAL;
	// }

	/*
	 * Persist what the wake-up event will mean BEFORE arming the
	 * corresponding level detector.
	 */
	// err = retained_set_next_reed_level(next_level);
	// if (err != 0) {
	// 	return err;
	// }

	/*
	 * From here on there is no application callback registered.
	 * The GPIO is exclusively a SYSTEMOFF wake-up source.
	 */
	// err = gpio_pin_interrupt_configure_dt(&reed_input, wake_flags);
	// if (err != 0) {
	// 	return err;
	// }

	return 0;
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

	/*
	 * Register the callback BEFORE enabling the interrupt.
	 * This prevents an edge from occurring while the interrupt
	 * is enabled but no callback is registered yet.
	 */
	gpio_init_callback(&reed_input_cb_data, 
			reed_input_cb, 
			BIT(reed_input.pin));

	err = gpio_add_callback(reed_input.port, &reed_input_cb_data);
	if (err < 0) {
		LOG_ERR("GPIO can't add callback of reed input interrupt (err: %d)", err);
		return err;
	}
	reed_callback_registered = true;

	err = gpio_pin_interrupt_configure_dt(
		&reed_input, 
		GPIO_INT_DISABLE);
	if (err < 0) {
		LOG_ERR("GPIO can't configure reed input interrupt (err: %d)", err);
		gpio_remove_callback(reed_input.port, &reed_input_cb_data);
		reed_callback_registered = false;
		return err;
	}

	/*
	 * Read AFTER the callback and interrupt are installed.
	 *
	 * If a falling edge occurs from this point onwards, the callback
	 * can catch it. If it happened before enabling the interrupt,
	 * the current LOW level is visible here and the startup
	 * b_missed_reed logic can detect it when applicable.
	 */
	*level = gpio_pin_get_dt(&reed_input);
	if (*level < 0) {
		LOG_ERR("Can't read reed input (err: %d)", *level);
		return *level;
	}

	LOG_DBG("Reed input initialized");
	// reed_thread_check_started();

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

int reed_input_start(void)
{
    int err;

    reed_thread_check_started();

    err = gpio_pin_interrupt_configure_dt(
        &reed_input,
        GPIO_INT_EDGE_BOTH);

    if (err < 0) {
        LOG_ERR("Cannot enable reed interrupts: %d", err);
        return err;
    }

    /*
     * Reconcile a possible transition that happened
     * between boot recovery and interrupt activation.
     */
    k_sem_give(&reed_sem);

    return 0;
}

int reed_fsm_recover_boot(int wake_level)
{
    int expected = retained_get_next_reed_level();

    if (wake_level != expected) {
        LOG_ERR("REED FSM: unexpected wake level=%d expected=%d",
                wake_level, expected);
        return -EINVAL;
    }

	int current = reed_sample_stable_level(REED_BOOT_STABLE_MS);

	if (current == -EAGAIN) {
		LOG_WRN("REED FSM: unstable input during boot");

		if (wake_level == 1) {
			/*
			* LATCH confirms that ACTIVE was detected.
			* Record the closure, but do not count yet.
			*
			* System OFF will be armed for INACTIVE.
			* If the reed is already open, this will
			* cause another wakeup to confirm the opening.
			*/
			int result = reed_fsm_apply_level(1);
			return result < 0 ? result : 0;
		}

		/*
		* We woke up expecting INACTIVE, but cannot
		* confirm that the reed is open.
		*
		* Keep waiting for INACTIVE. Do not count.
		*/
		return 0;
	}

	if (current < 0) {
		LOG_ERR("REED FSM: GPIO read failed: %d", current);
		return current;
	}

    if (wake_level == 1) {
        int result = reed_fsm_apply_level(1);

        if (result < 0) {
            return result;
        }

        if (current == 0) {
            LOG_DBG("REED FSM: complete pulse recovered during boot");
            return reed_fsm_apply_level(0);
        }

        return 0;
    }

    if (current == 0) {
        return reed_fsm_apply_level(0);
    }

    LOG_WRN("REED FSM: INACTIVE wake not yet confirmed");
    return 0;
}