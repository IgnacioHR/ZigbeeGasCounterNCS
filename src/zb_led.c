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
#include <zephyr/drivers/gpio.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(led, LOG_LEVEL_INF);

#include "zb_led.h"

#if HAS_XIAO_RGB_LED && IS_ENABLED(CONFIG_DEBUG)

#define XIAO_LED_RED_NODE   DT_ALIAS(xiao_led_red)
#define XIAO_LED_GREEN_NODE DT_ALIAS(xiao_led_green)
#define XIAO_LED_BLUE_NODE  DT_ALIAS(xiao_led_blue)

static const struct gpio_dt_spec led_red =
    GPIO_DT_SPEC_GET(XIAO_LED_RED_NODE, gpios);

static const struct gpio_dt_spec led_green =
    GPIO_DT_SPEC_GET(XIAO_LED_GREEN_NODE, gpios);

static const struct gpio_dt_spec led_blue =
    GPIO_DT_SPEC_GET(XIAO_LED_BLUE_NODE, gpios);

int xiao_leds_init(void)
{
    int err;

    if (!gpio_is_ready_dt(&led_red) ||
        !gpio_is_ready_dt(&led_green) ||
        !gpio_is_ready_dt(&led_blue)) {
        return -ENODEV;
    }

    err = gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_INACTIVE);
    if (err) {
        return err;
    }

    err = gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_INACTIVE);
    if (err) {
        return err;
    }

    err = gpio_pin_configure_dt(&led_blue, GPIO_OUTPUT_INACTIVE);
    if (err) {
        return err;
    }

    return 0;
}

void xiao_led_red_set(bool on)
{
    gpio_pin_set_dt(&led_red, on ? 1 : 0);
}

void xiao_led_green_set(bool on)
{
    gpio_pin_set_dt(&led_green, on ? 1 : 0);
}

void xiao_led_blue_set(bool on)
{
    gpio_pin_set_dt(&led_blue, on ? 1 : 0);
}

#endif