#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(xiao_hw, LOG_LEVEL_INF);

#include "zb_xiao.h"

#define XIAO_READ_BAT_EN_NODE DT_ALIAS(xiao_read_bat_enable)

#if DT_NODE_HAS_STATUS(XIAO_READ_BAT_EN_NODE, okay)
#define HAS_XIAO_READ_BAT_ENABLE 1
static const struct gpio_dt_spec xiao_read_bat_en = GPIO_DT_SPEC_GET(XIAO_READ_BAT_EN_NODE, enable_gpios);
#else
#define HAS_XIAO_READ_BAT_ENABLE 0
#endif

void xiao_battery_frontend_disable(void)
{
#if HAS_XIAO_READ_BAT_ENABLE
	int err;

	if (!gpio_is_ready_dt(&xiao_read_bat_en)) {
		LOG_ERR("XIAO battery enable GPIO not ready");
		return;
	}

	err = gpio_pin_configure_dt(&xiao_read_bat_en, GPIO_INPUT);
	if (err != 0) {
		LOG_ERR("Failed to set XIAO READ_BAT_ENABLE Hi-Z: %d", err);
		return;
	}	
#endif
}