#pragma once
#include "FreeRTOS.h"
typedef struct sim_task* TaskHandle_t;
typedef void (*TaskFunction_t)(void*);
#ifdef __cplusplus
extern "C" {
#endif
// Tasks cannot run on one thread; creating one fails. Code that has a task
// gives the simulator a step function instead (sim/README.md).
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char* name, uint32_t stack,
                                   void* arg, UBaseType_t prio, TaskHandle_t* out, BaseType_t core);
#define xTaskCreate(fn, name, stack, arg, prio, out) xTaskCreatePinnedToCore(fn, name, stack, arg, prio, out, tskNO_AFFINITY)
static inline void vTaskDelay(TickType_t t) { (void)t; }
static inline TickType_t xTaskGetTickCount(void) { extern int64_t esp_timer_get_time(void); return (TickType_t)(esp_timer_get_time() / 1000 / portTICK_PERIOD_MS); }
static inline TaskHandle_t xTaskGetCurrentTaskHandle(void) { return (TaskHandle_t)0; }
static inline void vTaskDelete(TaskHandle_t t) { (void)t; }
#ifdef __cplusplus
}
#endif
