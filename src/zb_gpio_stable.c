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
#include <zephyr/drivers/gpio.h>

int gpio_read_stable_level(const struct gpio_dt_spec *spec, int stable_samples, k_timeout_t interval)
{
	int last;
	int stable_count = 1;
	if (!device_is_ready(spec->port)) {
		return -ENODEV;
	}
	last = gpio_pin_get_dt(spec);
	if (last < 0) {
		return last;
	}
	while (stable_count < stable_samples) {
		int cur;
		k_sleep(interval);
		cur = gpio_pin_get_dt(spec);
		if (cur < 0) {
			return cur;
		}
		if (cur == last) {
			stable_count++;
		} else {
			last = cur;
			stable_count = 1;
		}
	}
	return last;
}
