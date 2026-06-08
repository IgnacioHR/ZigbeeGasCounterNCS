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

#ifndef ZB_MAIN_BUTTON_H
#define ZB_MAIN_BUTTON_H

#include "zb_features.h"

int main_button_gpio_init(int *level);
void main_button_start(void);
int main_button_get_level(void);
void set_started_from_deep_sleep(bool value);
int main_button_gpio_wakeup(void);
void main_button_fire_press(void);
void main_button_fire_release(void);
int main_button_read_early_level(void);
int main_button_read_stable_level(void);

#endif