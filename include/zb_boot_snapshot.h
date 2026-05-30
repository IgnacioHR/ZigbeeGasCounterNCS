#pragma once

#include <stdint.h>
#include <stdbool.h>

struct gm_boot_snapshot {
	bool valid;
	int reset_cause_ret;
	uint32_t reset_cause;
	int supported_ret;
	uint32_t supported_reset_cause;
	int clear_ret;
	int main_button_level;
	int reed_level;
	int main_button_err;
	int reed_err;
};

const struct gm_boot_snapshot *gm_boot_snapshot_get(void);
bool gm_boot_snapshot_was_system_off_gpio(void);
bool gm_boot_snapshot_was_watchdog(void);
bool gm_boot_snapshot_was_software(void);
