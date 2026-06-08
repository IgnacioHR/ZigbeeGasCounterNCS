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

#ifndef ZB_RADIO_H
#define ZB_RADIO_H

#include <zboss_api.h>
#include <zboss_api_addons.h>
#include <zb_zcl_metering.h>

zb_zcl_status_t radio_report_values(uint32_t events);
zb_zcl_status_t radio_send_values(uint32_t events);
void radio_report_ctx_init(void);
zb_zcl_status_t radio_request_values(uint32_t events);

#endif