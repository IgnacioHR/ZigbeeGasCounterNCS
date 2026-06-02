#pragma once

#ifndef ZB_NVR_H
#define ZB_NVR_H

enum nvr_items {
	NVR_ITEM_COUNTER			= BIT(0),
	NVR_ITEM_BAT_TIME			= BIT(1),
};

void nvr_schedule_save(uint32_t nvr_items_mask);
int nvr_wait_loaded(k_timeout_t timeout);

#endif // ZB_NVR