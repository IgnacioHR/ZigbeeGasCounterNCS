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

#ifndef ZB_NVR_H
#define ZB_NVR_H

enum nvr_items {
	NVR_ITEM_COUNTER			= BIT(0),
	NVR_ITEM_BAT_TIME			= BIT(1),
};

void nvr_schedule_save(uint32_t nvr_items_mask);
int nvr_wait_loaded(k_timeout_t timeout);

#endif // ZB_NVR