#pragma once

#ifndef ZB_RETAINED_H
#define ZB_RETAINED_H

#include <zephyr/kernel.h>

#define REED_DEBOUNCE_TIMEOUT_MS 150

int retained_init(void);
int retained_set_next_reed_level(uint16_t level);
uint16_t retained_get_next_reed_level(void);
int retained_set_boot_to_system_off(bool value);
bool retained_shall_power_off();
void retained_set_last_summation_sent(uint64_t value);
uint64_t retained_get_last_summation_sent(void);

#endif // ZB_RETAINED_H