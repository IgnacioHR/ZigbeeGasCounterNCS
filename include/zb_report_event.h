#pragma once

#ifndef ZB_REPORT_EVENT_H
#define ZB_REPORT_EVENT_H
#include <zb_config.h>
#include <zb_types.h>

#include "zb_features.h"

enum report_events {
	REPORT_CURRENT_SUMMATION_DELIVERED  = BIT(0),
#ifdef FEATURE_MEASURE_FLOW_RATE
	REPORT_INSTANTANEOUS_DEMAND         = BIT(1),
#endif
	REPORT_STATUS                       = BIT(2),
	REPORT_EXTENDED_STATUS              = BIT(3),
#ifdef FEATURE_MEASURE_BATTERY_LEVEL
	REPORT_BATTERY                      = BIT(4),
#endif
	REPORT_REQUEST_TIMING								= BIT(5),
	REPORT_NEW_TIME_ADQUIRED						= BIT(6),
};

void report_event_post(uint8_t task);
void report_event_start(void);
zb_uint64_t get_last_summation_sent();

#endif // ZB_REPORT_EVENT