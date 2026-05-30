#
# Copyright (c) 2024 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(ZigbeeGasCounterNCS)

# NORDIC SDK APP START
target_sources(app PRIVATE
  src/zb_main.c
  src/zb_main_button.c
  src/zb_reed.c
  src/zb_zigbee.c
  src/zb_deep_sleep.c
  src/zb_light_sleep.c
  src/zb_main_loop.c
  src/zb_report_event.c
  src/zb_save_counter.c
  src/zb_adc.c
  src/zb_wakeup_cause.c
  src/zb_retained.c
  src/zb_radio.c
  src/zb_boot_snapshot.c
  src/zb_gpio_stable.c
)

target_include_directories(app PRIVATE include)
# NORDIC SDK APP END
