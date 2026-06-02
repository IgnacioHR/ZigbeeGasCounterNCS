#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/reboot.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(btn_main, LOG_LEVEL_INF);

#include <zephyr/logging/log_ctrl.h>

#include "zb_features.h"
#include "zb_main_button.h"
#include "zb_main_loop.h"
#include "zb_deep_sleep.h"
#include "zb_report_event.h"
#include "zb_zigbee.h"
#include "zb_gpio_stable.h"

#ifdef FEATURE_DEEP_SLEEP
static atomic_t started_from_deep_sleep = ATOMIC_INIT(false);
#endif

/** Hardware from device tree */
#define MAIN_BUTTON_NODE DT_ALIAS(main_button)
static const struct gpio_dt_spec main_button = GPIO_DT_SPEC_GET(MAIN_BUTTON_NODE, gpios);

#ifdef CONFIG_DEBUG
#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led_main_button = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
#endif

/** Thread stacks and priorities */
#define BTN_THREAD_STACK_SIZE    1024
#define BTN_THREAD_PRIORITY         5
#define BTN_TASK_STACK_SIZE      2048
#define BTN_TASK_PRIORITY           5

// Hardware debounce time of the main button switch
#define BTN_DEBOUNCE_TIMEOUT 30 /* milliseconds */

// How long does the chip take from the moment it wakes up from an interrupt to the moment the button release time timer is computed */
// This value must be measured in place using the log facilities
#define FIXED_START_TIME_MS				150

#define CLICK_PRESS_TIME_MS       400    // MUST BE BIGGER THAN "FIXED_START_TIME_MS"
#define CLICK_HOLD_TIME_MS       3000
#define CLICK_RELEASE_TIME_MS     200

/** Main button gestures recognized */
typedef enum ButtonState_e {
    NONE = 0,
    PRESS,
    RELEASE,
    SINGLE_CLICK,
    DOUBLE_CLICK,
    UNKNOWN_CLICK,
    HOLD
} button_state_msg_t;

static atomic_t button_state = ATOMIC_INIT(NONE);

static K_SEM_DEFINE(btn_press_sem, 0, K_SEM_MAX_LIMIT);
static K_SEM_DEFINE(btn_release_sem, 0, K_SEM_MAX_LIMIT);

int main_button_read_early_level(void)
{
	int err;
	if (!device_is_ready(main_button.port)) {
		return -ENODEV;
	}
	err = gpio_pin_configure_dt(&main_button, GPIO_INPUT);
	if (err < 0) {
		return err;
	}
	return gpio_pin_get_dt(&main_button);
}

int main_button_read_stable_level(void)
{
	return gpio_read_stable_level(&main_button, 5, K_MSEC(10));
}

/**
 * @brief Safe way to set this property from other threads
 * 
 * @param value 
 */
void set_started_from_deep_sleep(bool value)
{
	atomic_set(&started_from_deep_sleep, false);
}

/**
 * @brief Callback from the ISR the main button has been pressed or released
 * 
 * @param dev N/A
 * @param cb N/A
 * @param pins N/A
 */
static void main_button_isr_cb(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	static int64_t last_main_btn_time_ms;

	const int64_t now_ms = k_uptime_get();
	const int64_t diff_ms = now_ms - last_main_btn_time_ms;

	if ((last_main_btn_time_ms > 0) && (diff_ms <= BTN_DEBOUNCE_TIMEOUT)) {
		return;
	}

	const int level = gpio_pin_get_dt(&main_button);
	if (level < 0)
		return;
	
	button_state_msg_t state = (button_state_msg_t)atomic_get(&button_state);

	if ((level == 1) && (state != PRESS)) {
#ifdef FEATURE_DEEP_SLEEP
		set_started_from_deep_sleep(false);
#endif
		k_sem_give(&btn_press_sem);
	}
	if ((level == 0) || (state == PRESS)) {
		k_sem_give(&btn_release_sem);
	}

	last_main_btn_time_ms = now_ms;

#ifdef CONFIG_DEBUG
	gpio_pin_set_dt(&led_main_button, level);
#endif
}

/**
 * @brief Hardware configuration of the main button input gpio pin and interrupt
 * 
 * @return int 
 */
static int main_button_configure(void)
{
	int err;

	if (!device_is_ready(main_button.port)) {
		LOG_ERR("Main button GPIO device not ready");
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&main_button, GPIO_INPUT);
	if (err < 0) {
		LOG_ERR("GPIO can't configure main button (err: %d)", err);
		return err;
	}

	err = gpio_pin_interrupt_configure_dt(&main_button, GPIO_INT_EDGE_BOTH);
	if (err < 0) {
		LOG_ERR("GPIO can't configure main button interrupt (err: %d)", err);
		return err;
	}

	static struct gpio_callback main_button_cb_data;
	gpio_init_callback(&main_button_cb_data, main_button_isr_cb, BIT(main_button.pin));
	err = gpio_add_callback(main_button.port, &main_button_cb_data);
	if (err < 0) {
		LOG_ERR("GPIO can't add callback of main button interrupt (err: %d)", err);
	}

	LOG_DBG("Main button initialized");
	return 0;
}

void main_button_fire_from_start(void)
{
	k_sem_give(&btn_press_sem);
}

/**
 * @brief Configure GPIO to wake up the device.
 * 
 */
int main_button_gpio_wakeup(void)
{
	int current;

	if (!device_is_ready(main_button.port)) {
		LOG_ERR("Main button GPIO device not ready");
		return -ENODEV;
	}

	current = main_button_read_early_level();
	if (current < 0) {
		return current;
	}

	if (current == 1) {
		LOG_WRN("main_button_gpio_wakeup: button already active");
		return -EAGAIN;
	}

	return gpio_pin_interrupt_configure_dt(&main_button, GPIO_INT_LEVEL_ACTIVE);
}

#ifdef CONFIG_DEBUG
/**
 * @brief Hardware configuration of the led associated with the main button
 * 
 * @return int 
 */
static int main_button_led_configure(void)
{
	int err = gpio_pin_configure_dt(&led_main_button, GPIO_OUTPUT_ACTIVE);
	if (err < 0) {
		LOG_ERR("GPIO can't configure main button led (err: %d)", err);
		return err;
	}
	return 0;
}
#endif

/**
 * @brief Overall hardware initialization of the main button module. Called from the main function at startup
 * 
 * @return int 
 */
int main_button_gpio_init(void)
{
	int err;
	err = main_button_configure();
	if (err < 0)
		return err;
#ifdef CONFIG_DEBUG
	err = main_button_led_configure();
	if (err < 0)
		return err;
#endif
	return 0;
}

// Message queue for main button state changes
K_MSGQ_DEFINE(btn_state_msgq, sizeof(button_state_msg_t), 1, 1);

// Required inside btn_notify_state
static K_MUTEX_DEFINE(btn_notify_mutex);

/**
 * @brief changes the internal button state and fires message to the queue
 * 
 * @param state new state to assign
 * @return enum ButtonState_e same state assigned, for convenience, see usage
 */
static enum ButtonState_e btn_notify_state(enum ButtonState_e state)
{
	button_state_msg_t msg = (button_state_msg_t)state;
	k_mutex_lock(&btn_notify_mutex, K_FOREVER);
	atomic_set(&button_state, state);
	if (k_msgq_num_used_get(&btn_state_msgq) > 0)
		k_msgq_purge(&btn_state_msgq);
	int err = k_msgq_put(&btn_state_msgq, &msg, K_NO_WAIT);
	k_mutex_unlock(&btn_notify_mutex);
	if (err != 0) {
			LOG_ERR("Cannot notify button state: %d", err);
	}
	return state;
}

/**
 * @brief called when timer expires after main button press action detected, note, this timer
 * is cancelled if a release action is detected within CLICK_PRESS_TIME_MS milliseconds.
 * Allows to detect UNKNOWN_CLICK action detection.
 * 
 * @param work 
 */
static void timer_since_press_cb(struct k_work *work)
{
	ARG_UNUSED(work);
	enum ButtonState_e state =
    (enum ButtonState_e)atomic_get(&button_state);

	if (state == SINGLE_CLICK || state == DOUBLE_CLICK || state == UNKNOWN_CLICK) {
		btn_notify_state(UNKNOWN_CLICK);
	}
}
static K_WORK_DELAYABLE_DEFINE(work_since_press, timer_since_press_cb);

/**
 * @brief called when the timer expires after main button press action detected, note, this timer
 * is cancelled if a release action is detected. Allows to detect HOLD o long press actions.
 * 
 * @param work 
 */
static void timer_since_press_detect_hold_cb(struct k_work *work)
{
	ARG_UNUSED(work);
	enum ButtonState_e state =
    (enum ButtonState_e)atomic_get(&button_state);
	if (state == PRESS)
		btn_notify_state(HOLD);
}
static K_WORK_DELAYABLE_DEFINE(work_detect_hold, timer_since_press_detect_hold_cb);

/**
 * @brief called when the timer expires after main button release action detected, note, this timer
 * is cancelled if a press action is detected within the period of time defined at CLICK_RELEASE_TIME_MS. 
 * Allows to announce proper gesture detected
 * 
 * @param work 
 */
static void timer_since_release_cb(struct k_work *work)
{
	ARG_UNUSED(work);
	enum ButtonState_e state =
    (enum ButtonState_e)atomic_get(&button_state);
	bool single_or_double_or_unknown_click = state == SINGLE_CLICK || state == DOUBLE_CLICK || state == UNKNOWN_CLICK;
	if (single_or_double_or_unknown_click) {
		btn_notify_state(state);
	} else if (state != NONE) {
		btn_notify_state(HOLD);
	}
}
static K_WORK_DELAYABLE_DEFINE(work_since_release, timer_since_release_cb);

/**
 * @brief Called from the ISR when the button is pressed via a semaphore
 * 
 * @param p1 
 * @param p2 
 * @param p3 
 */
static void btn_press_thread(void *p1, void *p2, void *p3)
{
	int err;
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_DBG("Main button press thread started");
	while (true) {
		k_sem_take(&btn_press_sem, K_FOREVER);

		enum ButtonState_e state =
			(enum ButtonState_e)atomic_get(&button_state);

		if (state == NONE) {
			state = btn_notify_state(PRESS);
		}
		
		// int32_t current_time_since_press = timer_since_press_period_ms;
		// int32_t current_time_since_hold = timer_detect_hold_ms;
		int32_t click_press_time_adjusted_ms = CLICK_PRESS_TIME_MS;			
		int32_t hold_time_adjusted_ms = CLICK_HOLD_TIME_MS;
		#ifdef FEATURE_DEEP_SLEEP
		bool started_from_deep_sleep_value = atomic_get(&started_from_deep_sleep);
		if (started_from_deep_sleep_value)
		{
				int32_t startTime_ms = FIXED_START_TIME_MS; // empirical evaluation needed
				// substract FIXED_START_TIME_MS ms to timers
				click_press_time_adjusted_ms = click_press_time_adjusted_ms - startTime_ms; // measured time for the device to start
				hold_time_adjusted_ms -= hold_time_adjusted_ms - startTime_ms;
		}
		#endif
		err = k_work_reschedule(&work_since_press, K_MSEC(click_press_time_adjusted_ms));
		if (err < 0)
			LOG_ERR("Can't set press timer time to %dms", click_press_time_adjusted_ms);
		if (state == PRESS) {
			err = k_work_reschedule(&work_detect_hold, K_MSEC(hold_time_adjusted_ms));
			if (err < 0)
				LOG_ERR("Can't start hold timer");
		} else {
			err = k_work_cancel_delayable(&work_since_release);
			if (err < 0)
				LOG_ERR("Can't stop press timer: %d", err);
		}
	}
}
static K_THREAD_DEFINE(btn_press_tid, BTN_THREAD_STACK_SIZE, btn_press_thread, NULL, NULL, NULL, BTN_THREAD_PRIORITY, 0, K_TICKS_FOREVER);

/**
 * @brief Called from the ISR when the button is released via a semaphore
 * 
 * @param p1 
 * @param p2 
 * @param p3 
 */
static void btn_release_thread(void *p1, void *p2, void *p3)
{
	int err;
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("Main button release task started");
	while (true)
	{
		k_sem_take(&btn_release_sem, K_FOREVER);

		enum ButtonState_e state =
			(enum ButtonState_e)atomic_get(&button_state);

		if (state == PRESS) {
			state = btn_notify_state(RELEASE);
		}
		if (state == HOLD) {
			state = btn_notify_state(NONE);
			continue;
		}
		if (k_work_delayable_is_pending(&work_since_press)) {
			err = k_work_cancel_delayable(&work_since_press);
			if (err < 0)
				LOG_ERR("Can't cancel work_since_press timer");
			if (state == SINGLE_CLICK) {
				atomic_set(&button_state, DOUBLE_CLICK);
			} else if (state == DOUBLE_CLICK) {
				atomic_set(&button_state, UNKNOWN_CLICK);
			} else if (state != UNKNOWN_CLICK && state != DOUBLE_CLICK) {
				atomic_set(&button_state, SINGLE_CLICK);
			}
		}
		if (k_work_delayable_is_pending(&work_detect_hold)) {
			err = k_work_cancel_delayable(&work_detect_hold);
			if (err < 0)
				LOG_ERR("Can't cancel work_detect_hold timer");
		}
		err = k_work_reschedule(&work_since_release, K_MSEC(CLICK_RELEASE_TIME_MS));
		if (err < 0)
			LOG_ERR("Can't start 0.4s timer after release");
	}
}
static K_THREAD_DEFINE(btn_release_tid, BTN_THREAD_STACK_SIZE, btn_release_thread, NULL, NULL, NULL, BTN_THREAD_PRIORITY, 0, K_TICKS_FOREVER);

/**
 * @brief process messages received int he button message queue. Allows to link gestures to specific actions
 * 
 * @param p1 
 * @param p2 
 * @param p3 
 */
static void btn_task(void *p1, void *p2, void *p3)
{
	int err;
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);
	LOG_INF("Main button action task started");
	while (true) {
		button_state_msg_t msg;
		err = k_msgq_get(&btn_state_msgq, &msg, K_FOREVER);
		if (err == -ENOMSG) {
			continue;
		}
		if (err < 0) {
			LOG_ERR("btn_task k_msgq_get (err: %d)", err);
			continue;
		}
		enum ButtonState_e state = (enum ButtonState_e)msg;
		switch (state) {
		case PRESS:
			// LOG_INF("Button press");
#ifdef CONFIG_DEBUG
			gpio_pin_set_dt(&led_main_button, 1);
#endif
			main_loop_post(SHALL_ENABLE_ZIGBEE);
#ifdef FEATURE_DEEP_SLEEP
			poweroff_mgr_user_window_extend(deep_sleep_eval_time_ms(true, false));
#endif
			break;
		case RELEASE:
			// LOG_INF("Button release");
#ifdef CONFIG_DEBUG
			gpio_pin_set_dt(&led_main_button, 0);
#endif
			break;
		case SINGLE_CLICK:
			LOG_INF("Single click detected");
			report_event_post(
				REPORT_CURRENT_SUMMATION_DELIVERED |
				REPORT_STATUS |
				REPORT_EXTENDED_STATUS
			);
#ifdef FEATURE_MEASURE_BATTERY_LEVEL
			main_loop_post(SHALL_MEASURE_BATTERY);
#endif
			reset_device_status();
			reset_device_extended_status();
			btn_notify_state(NONE);
			break;
		case DOUBLE_CLICK:
			LOG_INF("Double click detected");
			btn_notify_state(NONE);
			/*
				* Equivalente a esp_restart().
				*/
			while (log_data_pending()) {
				log_flush();
			}
			sys_reboot(SYS_REBOOT_COLD);
			break;
		case UNKNOWN_CLICK:
			LOG_INF("Unknown click detected");
			btn_notify_state(NONE);
			break;
		case HOLD:
			LOG_INF("Hold detected");
			// leave_action();
			break;
		case NONE:
			// LOG_INF("Button state reset");
			// led_off();
			break;
		default:
			LOG_INF("Unknown button state: %d", state);
			break;
		}
	}
}
static K_THREAD_DEFINE(btn_task_tid, BTN_TASK_STACK_SIZE, btn_task, NULL, NULL, NULL, BTN_TASK_PRIORITY, 0, K_TICKS_FOREVER);

void main_button_start(void)
{
	k_thread_start(btn_press_tid);
	k_thread_start(btn_release_tid);
	k_thread_start(btn_task_tid);
}

/**
 * @brief Get the logical level of the main button input
 * 
 * @return int 
 */
int get_main_button_level(void)
{
	return gpio_pin_get_dt(&main_button);
}