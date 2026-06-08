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

#ifndef ZB_WAKEUP_CAUSE_H
#define ZB_WAKEUP_CAUSE_H

typedef enum gm_wakeup_cause {
    GM_WAKEUP_GPIO_UNKNOWN = 0x00,
    GM_WAKEUP_GPIO_MAIN_BTN_PRESS,
    GM_WAKEUP_GPIO_REED_PULSE_ACTIVE,
    GM_WAKEUP_GPIO_REED_PULSE_INACTIVE,
    GM_WAKEUP_OTHER
} gm_wakeup_cause_t;

gm_wakeup_cause_t wakeup_cause_init(void);
int detect_cause_init(void);

#endif // ZB_WAKEUP_CAUSE_H