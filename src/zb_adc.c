#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(adc, LOG_LEVEL_INF);

#include "zb_features.h"
#include "zb_adc.h"
#include "zb_main_loop.h"
#include "zb_zigbee.h"

#define TIME_TO_EXPIRE_ADC_SECONDS			(12 * 60 * 60)

#ifdef FEATURE_MEASURE_BATTERY_LEVEL

#define ADC_TASK_STACK_SIZE   2048
#define ADC_TASK_PRIORITY        5

static K_SEM_DEFINE(adc_sem, 0, K_SEM_MAX_LIMIT);
static K_MUTEX_DEFINE(adc_mutex);

static bool adc_started = false;

static void adc_task(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("Main adc task started");
	while (true) {
		k_sem_take(&adc_sem, K_FOREVER);

		LOG_INF("Going measure ADC input");
	}
}
static K_THREAD_DEFINE(adc_tid, ADC_TASK_STACK_SIZE, adc_task, NULL, NULL, NULL, ADC_TASK_PRIORITY, 0, K_TICKS_FOREVER);

/**
 * @brief evaluates if it is time to masure battery parameters
 * 
 * @return true 
 * @return false 
 */
bool check_shall_measure_battery(void)
{
	// obtain last time adc value was sent from NVR
	uint32_t old_time = get_old_time();
	// obtain current time in ms since 2000-01-01T00:00:00UTC
	uint32_t current_time = get_current_time();
	// if treshold passed put action to measure battery values
	return old_time == 0 || current_time - old_time > TIME_TO_EXPIRE_ADC_SECONDS;
}


void fire_adc(void)
{
	k_mutex_lock(&adc_mutex, K_FOREVER);
	if (!adc_started) {
		k_thread_start(adc_tid);
	}
	k_mutex_unlock(&adc_mutex);
	k_sem_give(&adc_sem);
}

#endif