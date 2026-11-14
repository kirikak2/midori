// FreeRTOS stand-ins: the simulator runs on one thread (the browser's), so
// critical sections and mutexes do nothing, and a queue is a ring that the
// host loop drains (sim/port/rtos_sim.c).
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_timer.h"   // ESP-IDF's FreeRTOS headers bring it in too
typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t TickType_t;
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdFAIL 0
#define portMAX_DELAY 0xffffffffu
#define portTICK_PERIOD_MS (1000 / CONFIG_FREERTOS_HZ)
#define pdMS_TO_TICKS(ms) ((TickType_t)((uint64_t)(ms) * CONFIG_FREERTOS_HZ / 1000))
#define tskIDLE_PRIORITY 0
#define tskNO_AFFINITY 0x7fffffff
typedef struct { int unused; } portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED { 0 }
#define portMUX_INITIALIZE(m) ((void)(m))
#define portENTER_CRITICAL(m) ((void)(m))
#define portEXIT_CRITICAL(m) ((void)(m))
#define portENTER_CRITICAL_SAFE(m) ((void)(m))
#define portEXIT_CRITICAL_SAFE(m) ((void)(m))
#define portENTER_CRITICAL_ISR(m) ((void)(m))
#define portEXIT_CRITICAL_ISR(m) ((void)(m))
#define configASSERT(x) ((void)0)
typedef struct { int unused[4]; } StaticSemaphore_t;
typedef struct { int unused[4]; } StaticQueue_t;
typedef struct { int unused[4]; } StaticTask_t;
typedef void* StackType_t_unused;
typedef uint8_t StackType_t;
