#
# Copyright (c) 2024 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

cmake_minimum_required(VERSION 3.20.0)

if(DEFINED WEST_PYTHON)
  set(PYTHON_EXECUTABLE ${WEST_PYTHON})
else()
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(PYTHON_EXECUTABLE ${Python3_EXECUTABLE})
endif()

set(GAS_COUNTER_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(GAS_COUNTER_VERSION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/VERSION.json")
set(GAS_COUNTER_VERSION_CONF "${GAS_COUNTER_GENERATED_DIR}/gas_counter_version.conf")
set(GAS_COUNTER_VERSION_HEADER "${GAS_COUNTER_GENERATED_DIR}/zb_version_generated.h")
file(MAKE_DIRECTORY "${GAS_COUNTER_GENERATED_DIR}")

execute_process(
  COMMAND "${PYTHON_EXECUTABLE}"
          "${CMAKE_CURRENT_SOURCE_DIR}/cmake/generate_version_files.py"
          --input "${GAS_COUNTER_VERSION_FILE}"
          --out-conf "${GAS_COUNTER_VERSION_CONF}"
          --out-header "${GAS_COUNTER_VERSION_HEADER}"
  RESULT_VARIABLE gas_counter_version_gen_result
  OUTPUT_VARIABLE gas_counter_version_gen_stdout
  ERROR_VARIABLE gas_counter_version_gen_stderr
)

if(NOT "${gas_counter_version_gen_result}" STREQUAL "0")
  message(STATUS "Version generator stdout: ${gas_counter_version_gen_stdout}")
  message(STATUS "Version generator stderr: ${gas_counter_version_gen_stderr}")
  message(FATAL_ERROR "Failed to generate gas counter version files")
endif()

list(APPEND EXTRA_CONF_FILE "${GAS_COUNTER_VERSION_CONF}")
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(ZigbeeGasCounterNCS)

target_include_directories(app PRIVATE
  include
  "${GAS_COUNTER_GENERATED_DIR}"
)

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
  src/zb_nvr.c
  src/zb_adc.c
  src/zb_xiao.c
  src/zb_wakeup_cause.c
  src/zb_retained.c
  src/zb_radio.c
  src/zb_boot_snapshot.c
  src/zb_gpio_stable.c
  src/zb_instantaneous_demand.c
  src/zb_led.c
)

target_include_directories(app PRIVATE include)
# NORDIC SDK APP END
