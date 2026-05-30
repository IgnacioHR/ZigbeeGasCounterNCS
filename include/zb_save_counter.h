#pragma once

#ifndef ZB_SAVE_COUNTER_H
#define ZB_SAVE_COUNTER_H

void counter_schedule_save(void);
int save_counter_wait_loaded(k_timeout_t timeout);

#endif // ZB_SAVE_COUNTER