#pragma once

#ifndef ZB_GPIO_STABLE_H
#define ZB_GPIO_STABLE_H

#include <zephyr/kernel.h>

int gpio_read_stable_level(const struct gpio_dt_spec *spec, int samples, k_timeout_t delay);

#endif