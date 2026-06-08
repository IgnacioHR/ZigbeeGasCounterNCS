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
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(zb_light_sleep, LOG_LEVEL_INF);

#include "zb_features.h"

#ifdef FEATURE_LIGHT_SLEEP

// TODO

	ESP_ERROR_CHECK((periodic_checks_timer = xTimerCreate("periodic_checks", pdMS_TO_TICKS(MUST_SYNC_MINIMUM_TIME * 1000 - 100), pdTRUE, "p_c", periodic_checks_callback)) == NULL ? ESP_FAIL : ESP_OK);
	if (periodic_checks_timer != NULL) {
			xTimerStart(periodic_checks_timer, 0);
	}
#endif