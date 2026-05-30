#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(adc, LOG_LEVEL_INF);

#include "zb_adc.h"

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

void fire_adc(void)
{
	k_mutex_lock(&adc_mutex, K_FOREVER);
	if (!adc_started) {
		k_thread_start(adc_tid);
	}
	k_mutex_unlock(&adc_mutex);
	k_sem_give(&adc_sem);
}