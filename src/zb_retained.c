#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/retention/retention.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "zb_retained.h"
#include "zb_nvr.h"

LOG_MODULE_REGISTER(retained, LOG_LEVEL_INF);

#define GM_RET_MAGIC   0x474d5244u /* "GMRD" */
#define GM_RET_VERSION 2u

#define RETAINED_NODE DT_NODELABEL(gm_retention)

struct zb_retained_state {
	uint32_t 	version;
	uint16_t	next_reed_level;
	uint16_t	boot_to_system_off;
	uint64_t	last_summation_sent;
	uint32_t 	magic;
};

static const struct device *ret_dev = DEVICE_DT_GET(RETAINED_NODE);

static struct zb_retained_state state;
static struct k_spinlock state_lock;

static struct k_work_delayable flush_work;

static void retained_log_state(const char *tag)
{
	LOG_DBG("%s: magic=0x%08x version=%u next_reed_level=%u boot_to_system_off=%u "
		"last_sent=%llu",
		tag,
		state.magic,
		state.version,
		state.next_reed_level,
		state.boot_to_system_off,
		state.last_summation_sent
	);
}

static int retention_save_locked(void)
{
	return retention_write(ret_dev, 0, (const uint8_t *)&state, sizeof(state));
}

static void flush_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	k_spinlock_key_t key = k_spin_lock(&state_lock);

	LOG_DBG("flush_work_handler: saving next_reed_level=%u boot_to_system_off=%u",
		state.next_reed_level,
		state.boot_to_system_off);

	int err = retention_save_locked();

	k_spin_unlock(&state_lock, key);

	if (err) {
		LOG_ERR("retention_write failed: %d", err);
	}
}

static void retained_defaults(void)
{
	memset(&state, 0, sizeof(state));

	state.magic = GM_RET_MAGIC;
	state.version = GM_RET_VERSION;

	/*
	 * Estado inicial normal:
	 * esperamos que el reed pase a activo/HIGH.
	 */
	state.next_reed_level = 1;

	state.boot_to_system_off = 0;
	state.last_summation_sent = 0;
}


static int retained_flush_sync(void)
{
	k_spinlock_key_t key = k_spin_lock(&state_lock);
	int err = retention_save_locked();
	k_spin_unlock(&state_lock, key);

	return err;
}

/**
 * @brief Initializes the ram that persists information
 *        that survives a reboot process but does not
 *        survive a power recycle. This memory is not
 *        maintained in flash, but if the power consumption
 *        is high I might consider this decision later
 * 
 * @return int 
 */
int retained_init(void)
{
	int err;

	if (!device_is_ready(ret_dev)) {
		LOG_ERR("Retention device not ready");
		return -ENODEV;
	}

	k_work_init_delayable(&flush_work, flush_work_handler);

	err = retention_is_valid(ret_dev);
	LOG_DBG("retention_is_valid returned %d", err);

	if (err <= 0) {
		LOG_WRN("No valid retained reed state");
		retained_defaults();
		retained_log_state("retained_init defaults before flush");
		err = retained_flush_sync();
		LOG_INF("retained_init defaults flush err=%d", err);
		retained_log_state("retained_init final defaults");
		return err;
	}

	err = retention_read(ret_dev, 0, (uint8_t *)&state, sizeof(state));
	if (err) {
		LOG_WRN("retention_read failed: %d", err);
		retained_defaults();
		retained_log_state("retained_init read failed defaults before flush");
		err = retained_flush_sync();
		LOG_INF("retained_init read failed defaults flush err=%d", err);
		retained_log_state("retained_init final read failed defaults");
		return err;
	}

	retained_log_state("retained_init raw loaded");

	if (state.magic != GM_RET_MAGIC || state.version != GM_RET_VERSION) {
		LOG_WRN("Invalid retained reed state, resetting");
		retained_defaults();
		retained_log_state("retained_init invalid defaults before flush");
		err = retained_flush_sync();
		LOG_INF("retained_init invalid defaults flush err=%d", err);
		retained_log_state("retained_init final invalid defaults");
		return err;
	}

	retained_log_state("retained_init final loaded");
	return 0;
}

int retained_set_next_reed_level(uint16_t level)
{
	k_spinlock_key_t key;
	int err;
	struct zb_retained_state verify;

	key = k_spin_lock(&state_lock);

	LOG_DBG("retained_set_next_reed_level: old=%u new=%u",
		state.next_reed_level, level);

	state.next_reed_level = level;

	err = retention_save_locked();
	if (err != 0) {
		LOG_ERR("retained_set_next_reed_level: write failed err=%d", err);
		k_spin_unlock(&state_lock, key);
		return err;
	}

	err = retention_read(ret_dev, 0, (uint8_t *)&verify, sizeof(verify));
	if (err != 0) {
		LOG_ERR("retained_set_next_reed_level: verify read failed err=%d", err);
		k_spin_unlock(&state_lock, key);
		return err;
	}

	LOG_DBG("retained_set_next_reed_level: verify next_reed_level=%u boot_to_system_off=%u",
		verify.next_reed_level,
		verify.boot_to_system_off);

	k_spin_unlock(&state_lock, key);

	return 0;
}

/**
 * @brief sys_poweroff() does not work if called ater zigbee_enable()
 *        so we implement sys_poweroff() in two steps. In step 1 we
 * 				set this retained variable to 1 and call sys_reboot(SYS_REBOOT_COLD)
 *        once the chip restarts, we call sys_poweroff() and reset this variable
 *        value to 0
 * 
 * @param value 
 */
int retained_set_boot_to_system_off(bool value)
{
	k_spinlock_key_t key;
	int err;
	struct zb_retained_state verify;

	key = k_spin_lock(&state_lock);

	LOG_DBG("retained_set_boot_to_system_off: old=%u new=%u next_reed_level=%u",
		state.boot_to_system_off,
		value ? 1 : 0,
		state.next_reed_level);

	state.boot_to_system_off = value ? 1 : 0;

	err = retention_save_locked();
	if (err != 0) {
		LOG_ERR("retained_set_boot_to_system_off: write failed err=%d", err);
		k_spin_unlock(&state_lock, key);
		return err;
	}

	err = retention_read(ret_dev, 0, (uint8_t *)&verify, sizeof(verify));
	if (err != 0) {
		LOG_ERR("retained_set_boot_to_system_off: verify read failed err=%d", err);
		k_spin_unlock(&state_lock, key);
		return err;
	}

	LOG_DBG("retained_set_boot_to_system_off: verify next_reed_level=%u boot_to_system_off=%u",
		verify.next_reed_level,
		verify.boot_to_system_off);

	k_spin_unlock(&state_lock, key);

	return 0;
}

/**
 * @brief True when the system shall configure to power off and wakeup from GPIO
 * 
 * @return true 
 * @return false 
 */
bool retained_shall_power_off(void)
{
	k_spinlock_key_t key;
	bool value;

	key = k_spin_lock(&state_lock);
	value = state.boot_to_system_off == 1;
	k_spin_unlock(&state_lock, key);

	return value;
}

uint16_t retained_get_next_reed_level(void)
{
	k_spinlock_key_t key;
	uint16_t value;

	key = k_spin_lock(&state_lock);
	value = state.next_reed_level;
	k_spin_unlock(&state_lock, key);

	return value;
}

// static void retained_schedule_flush(void)
// {
// 	k_work_reschedule(&flush_work, K_MSEC(100));
// }

void retained_set_last_summation_sent(uint64_t value)
{
	k_spinlock_key_t key;
	int err;

	key = k_spin_lock(&state_lock);

	state.last_summation_sent = value;
	err = retention_save_locked();

	k_spin_unlock(&state_lock, key);

	if (err != 0) {
		LOG_ERR("retained_set_last_summation_sent: write failed err=%d", err);
	}
}

uint64_t retained_get_last_summation_sent(void)
{
	k_spinlock_key_t key;
	uint64_t value;

	key = k_spin_lock(&state_lock);
	value = state.last_summation_sent;
	k_spin_unlock(&state_lock, key);

	return value;
}
