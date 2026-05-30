#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/logging/log.h>

#include "zb_boot_snapshot.h"
#include "zb_main_button.h"
#include "zb_reed.h"

LOG_MODULE_REGISTER(gm_boot_snapshot, LOG_LEVEL_INF);

static struct gm_boot_snapshot boot_snapshot = {
	.valid = false,
	.reset_cause_ret = -ENODATA,
	.supported_ret = -ENODATA,
	.clear_ret = -ENODATA,
	.main_button_level = -1,
	.reed_level = -1,
	.main_button_err = -ENODATA,
	.reed_err = -ENODATA,
};

const struct gm_boot_snapshot *gm_boot_snapshot_get(void)
{
	return &boot_snapshot;
}

static int gm_boot_snapshot_init(void)
{
	int ret;

	boot_snapshot.valid = false;

	ret = hwinfo_get_reset_cause(&boot_snapshot.reset_cause);
	boot_snapshot.reset_cause_ret = ret;

	ret = hwinfo_get_supported_reset_cause(
		&boot_snapshot.supported_reset_cause);
	boot_snapshot.supported_ret = ret;

	/*
	 * Importante:
	 * Leer antes de limpiar.
	 * Zephyr indica que algunas plataformas acumulan flags de reset hasta
	 * llamar a hwinfo_clear_reset_cause().
	 */
	boot_snapshot.clear_ret = hwinfo_clear_reset_cause();

	boot_snapshot.main_button_level = main_button_read_early_level();
	if (boot_snapshot.main_button_level < 0) {
		boot_snapshot.main_button_err = boot_snapshot.main_button_level;
	} else {
		boot_snapshot.main_button_err = 0;
	}

	boot_snapshot.reed_level = reed_read_early_level();
	if (boot_snapshot.reed_level < 0) {
		boot_snapshot.reed_err = boot_snapshot.reed_level;
	} else {
		boot_snapshot.reed_err = 0;
	}

	boot_snapshot.valid = true;

	return 0;
}

/*
 * APPLICATION porque a este nivel los drivers ya deberían estar listos.
 * Prioridad 0 para ejecutarlo al principio del nivel APPLICATION.
 */
SYS_INIT(gm_boot_snapshot_init, POST_KERNEL, CONFIG_GAS_DEVICE_INIT_PRIORITY);

bool gm_boot_snapshot_was_system_off_gpio(void)
{
	return boot_snapshot.valid &&
	       boot_snapshot.reset_cause_ret == 0 &&
	       (boot_snapshot.reset_cause & RESET_LOW_POWER_WAKE);
}

bool gm_boot_snapshot_was_watchdog(void)
{
	return boot_snapshot.valid &&
	       boot_snapshot.reset_cause_ret == 0 &&
	       (boot_snapshot.reset_cause & RESET_WATCHDOG);
}

bool gm_boot_snapshot_was_software(void)
{
	return boot_snapshot.valid &&
	       boot_snapshot.reset_cause_ret == 0 &&
	       (boot_snapshot.reset_cause & RESET_SOFTWARE);
}
