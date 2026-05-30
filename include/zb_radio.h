#pragma once

#ifndef ZB_RADIO_H
#define ZB_RADIO_H

#include <zboss_api.h>
#include <zboss_api_addons.h>
#include <zb_zcl_metering.h>

zb_zcl_status_t radio_report_values(uint32_t events);
zb_zcl_status_t radio_send_values(uint32_t events);
void radio_report_ctx_init(void);
zb_zcl_status_t radio_request_values(uint32_t events);

#endif