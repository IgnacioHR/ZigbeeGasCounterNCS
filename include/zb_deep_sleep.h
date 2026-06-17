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

#pragma once

#ifndef ZB_DEEP_SLEEP_H
#define ZB_DEEP_SLEEP_H

#include "zb_wakeup_cause.h"
#include "zb_features.h"
#ifdef FEATURE_DEEP_SLEEP

enum pof_blocker {
	POF_BLOCK_ZIGBEE_TX     = BIT(0),
	POF_BLOCK_ZIGBEE_RX     = BIT(1),
	POF_BLOCK_ZBOSS_BUSY    = BIT(2),
	POF_BLOCK_SETTINGS      = BIT(3),
	POF_BLOCK_USER_WINDOW   = BIT(4),
	POF_BLOCK_ADC           = BIT(5),
	POF_BLOCK_SAVE_NVS			= BIT(6),
	POF_BLOCK_APP_NOT_READY	= BIT(7),
	POF_BLOCK_OTA						= BIT(8),
	POF_BLOCK_COUNTER				= BIT(9),
	POF_BLOCK_OTA_CONFIRM		= BIT(10),
	POF_BLOCK_LEAVE					= BIT(11),
};

int64_t deep_sleep_eval_time_ms(bool main_button_pressed, bool other_startup);

void poweroff_mgr_enable_for_wakeup(gm_wakeup_cause_t reason);

void poweroff_mgr_block_set(uint32_t mask);
void poweroff_mgr_block_clear(uint32_t mask);

void poweroff_mgr_user_window_open(int32_t timeout_ms);
void poweroff_mgr_user_window_extend(int32_t timeout_ms);
void poweroff_mgr_user_window_close(void);

void poweroff_mgr_zigbee_tx_begin(void);
void poweroff_mgr_zigbee_tx_done(void);
void poweroff_mgr_zigbee_rx_begin(void);
void poweroff_mgr_zigbee_rx_done(void);

bool poweroff_mgr_try_poweroff_now(void);

#endif // FEATURE_DEEP_SLEEP
#endif // ZB_DEEP_SLEEP_H