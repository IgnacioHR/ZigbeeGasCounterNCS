#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>

LOG_MODULE_REGISTER(deep_sleep, LOG_LEVEL_INF);

#include <zephyr/drivers/watchdog.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/init.h>

#include "zb_features.h"
#include "zb_deep_sleep.h"
#include "zb_retained.h"
#include "zb_zigbee.h"
#include "zb_main_button.h"
#include "zb_reed.h"
#include "zb_wakeup_cause.h"

#ifdef FEATURE_DEEP_SLEEP

#define TIME_TO_SLEEP_ZIGBEE_ON_MS					1000
#define TIME_TO_SLEEP_ZIGBEE_OFF_MS					50
#define TIME_TO_SLEEP_ZIGBEE_STARTING_MS		15 * 1000
#define TIME_TO_SLEEP_USER_WAKE_UP					60 * 1000
#define TIME_TO_SLEEP_BETWEEN_CYCLES		    10

#define WDT_NODE DT_ALIAS(watchdog0)
#if !DT_NODE_HAS_STATUS(WDT_NODE, okay)
#error "No watchdog0 alias found or watchdog device is disabled"
#endif

static const struct device *const wdt_dev = DEVICE_DT_GET(WDT_NODE);
static int wdt_channel_id = -1;

static void wdt_callback(const struct device *dev, int channel_id)
{
	ARG_UNUSED(dev);
	/*
	 * En muchos SoC, si el watchdog está configurado para reset,
	 * este callback puede no ejecutarse, o ejecutarse muy poco antes
	 * del reset. No dependas de él para guardar estado crítico.
	 */
	LOG_ERR("Watchdog timeout on channel %d", channel_id);
}

static int deep_sleep_watchdog_init(uint32_t timeout_ms)
{
	struct wdt_timeout_cfg cfg = {
		.window = {
			.min = 0,
			.max = timeout_ms,
		},
		.callback = wdt_callback,
		.flags = WDT_FLAG_RESET_SOC,
	};
	int err;
	if (!device_is_ready(wdt_dev)) {
		LOG_ERR("Watchdog device not ready");
		return -ENODEV;
	}
	wdt_channel_id = wdt_install_timeout(wdt_dev, &cfg);
	if (wdt_channel_id < 0) {
		LOG_ERR("wdt_install_timeout failed: %d", wdt_channel_id);
		return wdt_channel_id;
	}
	err = wdt_setup(wdt_dev, 0);
	if (err < 0) {
		LOG_ERR("wdt_setup failed: %d", err);
		return err;
	}
	LOG_DBG("Watchdog started: timeout=%u ms channel=%d", timeout_ms, wdt_channel_id);
	return 0;
}

/**
 * @brief evaluates the new time to enter sleep based on internal status
 * 
 * @return k_timeout_t 
 */
int64_t deep_sleep_eval_time_ms(bool main_button_pressed, bool other_startup)
{
	if (other_startup) {
		return TIME_TO_SLEEP_ZIGBEE_STARTING_MS;
	}
	if (main_button_pressed) {
		return TIME_TO_SLEEP_USER_WAKE_UP;
	}
	if (is_zigbee_started()) {
		return TIME_TO_SLEEP_ZIGBEE_ON_MS;
	}
	return TIME_TO_SLEEP_ZIGBEE_OFF_MS;
}

static atomic_t pof_initialized = ATOMIC_INIT(false);
static atomic_t pof_thread_started = ATOMIC_INIT(false);
static atomic_t pof_poweroff_in_progress = ATOMIC_INIT(false);

static struct k_event pof_events;
static struct k_spinlock pof_lock;

static bool armed;

#define POF_EVT_EVAL BIT(0)

static int64_t min_awake_until_ms;
static int64_t user_window_until_ms;
static int64_t zigbee_quiet_until_ms;
static int64_t zigbee_force_idle_after_ms;
static int64_t settings_sync_until_ms;

static atomic_t blockers;
static atomic_t zb_tx_pending;
static atomic_t zb_rx_pending;


#define POWEROFF_THREAD_STACK_SIZE 4096
#define POWEROFF_THREAD_PRIORITY   (CONFIG_NUM_PREEMPT_PRIORITIES - 1)

K_THREAD_STACK_DEFINE(poweroff_stack, POWEROFF_THREAD_STACK_SIZE);

static struct k_thread poweroff_thread_data;
static k_tid_t poweroff_tid;


static int64_t now_ms(void)
{
	return k_uptime_get();
}

static int32_t delay_until_ms(int64_t deadline)
{
	int64_t now = now_ms();

	if (deadline <= now) {
		return 0;
	}

	int64_t delta = deadline - now;

	if (delta > INT32_MAX) {
		return INT32_MAX;
	}

	return (int32_t)delta;
}

static void poweroff_evaluate(bool *should_poweroff, int64_t *next_deadline, uint32_t *active_blockers)
{
	k_spinlock_key_t key;
	int64_t now;
	uint32_t b;
	*should_poweroff = false;
	*next_deadline = 0;
	
	now = now_ms();

	key = k_spin_lock(&pof_lock);

	if (!armed) {
		k_spin_unlock(&pof_lock, key);
		return;
	}

	if (zigbee_force_idle_after_ms != 0 &&
	    now >= zigbee_force_idle_after_ms) {
		LOG_INF("Force IDLE");
		atomic_set(&zb_tx_pending, 0);
		atomic_set(&zb_rx_pending, 0);
		atomic_and(&blockers, ~(POF_BLOCK_ZIGBEE_TX |
					POF_BLOCK_ZIGBEE_RX));
		zigbee_force_idle_after_ms = 0;
	}

	if (user_window_until_ms != 0 && now >= user_window_until_ms) {
		LOG_INF("User window expired");
		user_window_until_ms = 0;
		atomic_and(&blockers, ~POF_BLOCK_USER_WINDOW);
	}

	if (settings_sync_until_ms != 0 && now >= settings_sync_until_ms) {
		LOG_INF("Settings expired");
		settings_sync_until_ms = 0;
		atomic_and(&blockers, ~POF_BLOCK_SETTINGS);
	}

	b = atomic_get(&blockers);

	if (atomic_get(&zb_tx_pending) > 0) {
		LOG_INF("Pending TX");
		b |= POF_BLOCK_ZIGBEE_TX;
	}

	if (atomic_get(&zb_rx_pending) > 0) {
		LOG_INF("Pending RX");
		b |= POF_BLOCK_ZIGBEE_RX;
	}

	if (min_awake_until_ms != 0 && now < min_awake_until_ms) {
		// LOG_INF("Awake until %lld", min_awake_until_ms - now);
		*next_deadline = min_awake_until_ms;
		*active_blockers = b;
		k_spin_unlock(&pof_lock, key);
		return;
	}
	if (zigbee_quiet_until_ms != 0 && now < zigbee_quiet_until_ms) {
		LOG_INF("Quiet until %lld", zigbee_quiet_until_ms - now);
		*next_deadline = zigbee_quiet_until_ms;
		*active_blockers = b;
		k_spin_unlock(&pof_lock, key);
		return;
	}
	if (user_window_until_ms != 0 && now < user_window_until_ms) {
		// LOG_INF("User window until %lld", user_window_until_ms - now);
		*next_deadline = user_window_until_ms;
		*active_blockers = b;
		k_spin_unlock(&pof_lock, key);
		return;
	}
	if (settings_sync_until_ms != 0 && now < settings_sync_until_ms) {
		LOG_INF("Settings sync until %lld", settings_sync_until_ms - now);
		*next_deadline = settings_sync_until_ms;
		*active_blockers = b;
		k_spin_unlock(&pof_lock, key);
		return;
	}
	if (b != 0) {
		LOG_INF("Other blocks");
		if (zigbee_force_idle_after_ms != 0) {
			*next_deadline = zigbee_force_idle_after_ms;
			*active_blockers = b;
		}
		k_spin_unlock(&pof_lock, key);
		return;
	}
	LOG_INF("Should poweroff=true");
	*should_poweroff = true;
	k_spin_unlock(&pof_lock, key);
}

static void poweroff_mgr_ensure_policy_initialized(void)
{
	if (atomic_get(&pof_initialized)) {
		return;
	}

	unsigned int key = irq_lock();

	if (!atomic_get(&pof_initialized)) {
		k_event_init(&pof_events);

		armed = false;

		atomic_set(&blockers, 0);
		atomic_set(&zb_tx_pending, 0);
		atomic_set(&zb_rx_pending, 0);

		min_awake_until_ms = 0;
		user_window_until_ms = 0;
		zigbee_quiet_until_ms = 0;
		zigbee_force_idle_after_ms = 0;
		settings_sync_until_ms = 0;

		atomic_set(&pof_initialized, 1);
	}

	irq_unlock(key);
}

static void poweroff(void)
{
	retained_set_boot_to_system_off(1);

	while (log_data_pending()) {
		log_flush();
	}

	int err = deep_sleep_watchdog_init(50);
	if (err != 0) {
		while (log_data_pending()) {
			log_flush();
		}
	}
}

static void poweroff_final_forever(void)
{
	poweroff();
	while (true) {
		k_sleep(K_FOREVER);
	}
}

static void poweroff_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	LOG_INF("Poweroff thread started");

	while (true) {
		k_event_wait(&pof_events,
			     POF_EVT_EVAL,
			     true,
			     K_FOREVER);

		LOG_DBG("Event received");

		while (true) {
			bool should_poweroff = false;
			int64_t next_deadline = 0;
			uint32_t active_blockers = 0;

			poweroff_evaluate(&should_poweroff, &next_deadline, &active_blockers);

			LOG_DBG("Poweroff evaluated should_poweroff=%s next_deadline=%lld", (should_poweroff ? "Yes" : "No"), next_deadline);

			if (should_poweroff) {
				if (atomic_cas(&pof_poweroff_in_progress, false, true)) {
					poweroff_final_forever();
				}
				k_sleep(K_FOREVER);
			}

			if (next_deadline == 0) {
				break;
			}

			int32_t delay_ms = delay_until_ms(next_deadline);

			if (delay_ms <= 0) {
				continue;
			}

			uint32_t events = k_event_wait(&pof_events,
						       POF_EVT_EVAL,
						       true,
						       K_MSEC(delay_ms));

			if (events & POF_EVT_EVAL) {
				continue;
			}
		}
	}
}

static void poweroff_mgr_ensure_thread_started(void)
{
	poweroff_mgr_ensure_policy_initialized();

	if (atomic_get(&pof_thread_started)) {
		return;
	}

	unsigned int key = irq_lock();
	if (!atomic_get(&pof_thread_started)) {
		poweroff_tid = k_thread_create(&poweroff_thread_data,
					       poweroff_stack,
					       K_THREAD_STACK_SIZEOF(poweroff_stack),
					       poweroff_thread,
					       NULL, NULL, NULL,
					       POWEROFF_THREAD_PRIORITY,
					       0,
					       K_NO_WAIT);
		k_thread_name_set(poweroff_tid, "poweroff_tid");
		atomic_set(&pof_thread_started, true);
	}
	irq_unlock(key);
}

static void poweroff_mgr_touch(void)
{
	bool should_poweroff = false;
	int64_t next_deadline = 0;
	uint32_t active_blockers = 0;

	poweroff_mgr_ensure_policy_initialized();

	if (atomic_get(&pof_poweroff_in_progress)) {
		return;
	}

	poweroff_evaluate(&should_poweroff, &next_deadline, &active_blockers);

	if (should_poweroff) {
		if (atomic_cas(&pof_poweroff_in_progress, 0, 1)) {
			poweroff_final_forever();
		}

		return;
	}

	if (next_deadline != 0) {
		poweroff_mgr_ensure_thread_started();
		k_event_post(&pof_events, POF_EVT_EVAL);
	}
}

void poweroff_mgr_block_set(uint32_t mask)
{
	LOG_DBG("poweroff_mgr_block_set called with mask=%u", mask);
	k_spinlock_key_t key;
	poweroff_mgr_ensure_policy_initialized();

	key = k_spin_lock(&pof_lock);
	atomic_or(&blockers, mask);
	k_spin_unlock(&pof_lock, key);

	poweroff_mgr_touch();
}

void poweroff_mgr_block_clear(uint32_t mask)
{
	LOG_DBG("poweroff_mgr_block_clear called with mask=%u", mask);
	k_spinlock_key_t key;
	poweroff_mgr_ensure_policy_initialized();

	key = k_spin_lock(&pof_lock);
	atomic_and(&blockers, ~mask);
	k_spin_unlock(&pof_lock, key);

	poweroff_mgr_touch();
}

/**
 * @brief Opens a period of time where the device will not
 * 				enter power off.
 * 				If a window was already set, this function will
 *        just set the new time.
 * 
 * @param timeout_ms 
 */
void poweroff_mgr_user_window_open(int32_t timeout_ms)
{
	int64_t deadline;

	LOG_INF("user_window_open timeoutms=%d", timeout_ms);
	k_spinlock_key_t key;
	poweroff_mgr_ensure_policy_initialized();

	deadline = now_ms() + timeout_ms;

	key = k_spin_lock(&pof_lock);
	user_window_until_ms = deadline;
	atomic_or(&blockers, POF_BLOCK_USER_WINDOW);
	k_spin_unlock(&pof_lock, key);

	poweroff_mgr_touch();
}

/**
 * @brief Extends a period of time where the device will not
 *        enter power off.
 *        If a window was already set, this function will 
 * 				check the remaining time in the window and will make sure
 * 				the new window ends as far in the future as possible
 * 
 * @param timeout_ms 
 */
void poweroff_mgr_user_window_extend(int32_t timeout_ms)
{
	LOG_INF("user_window_extend %d", timeout_ms);

	int64_t remaining;
	k_spinlock_key_t key;
	poweroff_mgr_ensure_policy_initialized();

	key = k_spin_lock(&pof_lock);
	remaining = user_window_until_ms - now_ms();
	if (remaining < 0)
		remaining = 0;
	if (remaining < timeout_ms)
		user_window_until_ms = now_ms() + timeout_ms;
	atomic_or(&blockers, POF_BLOCK_USER_WINDOW);
	k_spin_unlock(&pof_lock, key);

	poweroff_mgr_touch();
}

/**
 * @brief Clear any current period time where the device will
 *        not enter power off.
 * 
 */
void poweroff_mgr_user_window_close(void)
{
	LOG_INF("user_window_extend closed");

	k_spinlock_key_t key;
	poweroff_mgr_ensure_policy_initialized();

	key = k_spin_lock(&pof_lock);
	user_window_until_ms = 0;
	atomic_and(&blockers, ~POF_BLOCK_USER_WINDOW);
	k_spin_unlock(&pof_lock, key);

	poweroff_mgr_touch();
}

void poweroff_mgr_zigbee_tx_begin(void)
{
	LOG_DBG("poweroff_mgr_zigbee_tx_begin called");
	k_spinlock_key_t key;
	poweroff_mgr_ensure_policy_initialized();

	key = k_spin_lock(&pof_lock);
	atomic_or(&blockers, POF_BLOCK_ZIGBEE_TX);
	k_spin_unlock(&pof_lock, key);

	poweroff_mgr_touch();
}

void poweroff_mgr_zigbee_tx_done(void)
{
	LOG_DBG("poweroff_mgr_zigbee_tx_done called");
	k_spinlock_key_t key;
	poweroff_mgr_ensure_policy_initialized();

	key = k_spin_lock(&pof_lock);
	atomic_and(&blockers, ~POF_BLOCK_ZIGBEE_TX);
	k_spin_unlock(&pof_lock, key);

	poweroff_mgr_touch();
}

void poweroff_mgr_zigbee_rx_begin(void)
{
	LOG_DBG("poweroff_mgr_zigbee_rx_begin called");
	k_spinlock_key_t key;
	poweroff_mgr_ensure_policy_initialized();

	key = k_spin_lock(&pof_lock);
	atomic_or(&blockers, POF_BLOCK_ZIGBEE_RX);
	k_spin_unlock(&pof_lock, key);

	poweroff_mgr_touch();
}

void poweroff_mgr_zigbee_rx_done(void)
{
	LOG_DBG("poweroff_mgr_zigbee_rx_done called");
	k_spinlock_key_t key;
	poweroff_mgr_ensure_policy_initialized();

	key = k_spin_lock(&pof_lock);
	atomic_and(&blockers, ~POF_BLOCK_ZIGBEE_RX);
	k_spin_unlock(&pof_lock, key);

	poweroff_mgr_touch();
}

void poweroff_mgr_enable_for_wakeup(gm_wakeup_cause_t reason)
{
	k_spinlock_key_t key;
	int64_t now;

	poweroff_mgr_ensure_policy_initialized();
	now = now_ms();
	key = k_spin_lock(&pof_lock);

	switch (reason) {
	case GM_WAKEUP_GPIO_REED_PULSE_ACTIVE:
	case GM_WAKEUP_GPIO_REED_PULSE_INACTIVE:
		min_awake_until_ms = now;
		break;
	case GM_WAKEUP_GPIO_MAIN_BTN_PRESS:
		min_awake_until_ms = now + TIME_TO_SLEEP_ZIGBEE_STARTING_MS;
		user_window_until_ms = now + TIME_TO_SLEEP_ZIGBEE_STARTING_MS;
		atomic_or(&blockers, POF_BLOCK_USER_WINDOW);
		break;
	case GM_WAKEUP_OTHER:
	default:
		min_awake_until_ms = now;
		break;
	}
	armed = true;
	k_spin_unlock(&pof_lock, key);
	poweroff_mgr_touch();
}

static int poweroff_mgr_sys_init(void)
{
	k_event_init(&pof_events);

	k_spinlock_key_t key = k_spin_lock(&pof_lock);

	armed = false;

	atomic_set(&blockers, POF_BLOCK_APP_NOT_READY);
	atomic_set(&zb_tx_pending, 0);
	atomic_set(&zb_rx_pending, 0);

	min_awake_until_ms = 0;
	user_window_until_ms = 0;
	zigbee_quiet_until_ms = 0;
	zigbee_force_idle_after_ms = 0;
	settings_sync_until_ms = 0;

	k_spin_unlock(&pof_lock, key);

	atomic_set(&pof_initialized, true);

	return 0;
}
SYS_INIT(poweroff_mgr_sys_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

bool poweroff_mgr_try_poweroff_now(void)
{
	bool should_poweroff = false;
	int64_t next_deadline = 0;
	uint32_t active_blockers = 0;
	
	poweroff_mgr_ensure_policy_initialized();
	poweroff_evaluate(&should_poweroff, &next_deadline, &active_blockers);

	if (should_poweroff) {
		LOG_INF("Immediate poweroff conditions satisfied");
		if (atomic_cas(&pof_poweroff_in_progress, false, true)) {
			poweroff_final_forever();	
		}
	}

	if (next_deadline != 0) {
		poweroff_mgr_ensure_thread_started();
		k_event_post(&pof_events, POF_EVT_EVAL);
	}
	
	return false;
}
#endif