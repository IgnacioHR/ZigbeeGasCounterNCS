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