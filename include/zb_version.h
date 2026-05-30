#pragma once

/*
 * Zigbee Gas Meter - An open-source Zigbee gas meter project.
 * Copyright (c) 2025 Ignacio Hernández-Ros.
 *
 * This work is licensed under the Creative Commons Attribution-NonCommercial-ShareAlike 4.0
 * International License. To view a copy of this license, visit
 * https://creativecommons.org/licenses/by-nc-sa/4.0/
 *
 * You may use, modify, and share this work for personal and non-commercial purposes, as long
 * as you credit the original author(s) and share any derivatives under the same license.
 */

#define OTA_UPGRADE_HW_VERSION          0x0101
#define OTA_UPGRADE_IMAGE_TYPE          0x1011
#define HARDWARE_VERSION                0x01
#define APP_BUILD                       1                // 8 bit value
#define APP_VERSION                     1                // 8 bit value
#define STACK_VERSION                   1                // 8 bit value
#define STACK_BUILD                     1                // 8 bit value
#define PRODUCT_LABEL                   "\x15""MI CaSA - GAS COUNTER"
#define SW_BUILD_ID                     "\x04""V1.0"

#define OTA_FILE_VERSION (((APP_VERSION) << 24 ) | ((APP_BUILD) << 16) | ((STACK_VERSION) << 8) | (STACK_BUILD))