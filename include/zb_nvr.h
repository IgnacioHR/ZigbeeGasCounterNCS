#pragma once

#ifndef ZB_NVR_H
#define ZB_NVR_H

void nvr_schedule_save(void);
int nvr_wait_loaded(k_timeout_t timeout);

#endif // ZB_NVR