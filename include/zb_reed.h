#pragma once

#ifndef ZB_REED_H
#define ZB_REED_H

int reed_gpio_init(int *level);
int reed_get_level(void);
int reed_gpio_wakeup(void);
void check_counter_increment(void);
int reed_read_early_level(void);
int reed_read_stable_level(void);

#ifdef CONFIG_DEBUG
void reed_led_on();
void reed_led_off();
#endif

#endif // Z_REED