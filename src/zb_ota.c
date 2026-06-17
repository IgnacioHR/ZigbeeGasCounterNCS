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
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ota, LOG_LEVEL_INF);

#include <zephyr/dfu/mcuboot.h>
#include <zboss_api.h>

#include "zb_features.h"
#include "zb_deep_sleep.h"

/**
 * @brief check if current image is not confirmed at mcuboot level
 * 
 * @return true if confirmed
 * @return false if not confirmed
 */
bool ota_is_new_image(void)
{
	if (!boot_is_img_confirmed()) {
#ifdef FEATURE_DEEP_SLEEP		
		poweroff_mgr_block_set(POF_BLOCK_OTA_CONFIRM);
#endif
		return true;
	}
	return false;
}

void confirm_new_ota_image(void)
{
	if (!boot_is_img_confirmed()) {
    LOG_WRN("MCUboot image is not confirmed, confirming now");

    int err = boot_write_img_confirmed();
    if (err != 0) {
      LOG_ERR("Failed to confirm MCUboot image: %d", err);
    } else {
    	LOG_INF("MCUboot image confirmed");
#ifdef FEATURE_DEEP_SLEEP
			poweroff_mgr_block_clear(POF_BLOCK_OTA_CONFIRM);
#endif
		}
	}
	return;
}