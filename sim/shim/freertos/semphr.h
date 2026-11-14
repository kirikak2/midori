#pragma once
#include "FreeRTOS.h"
typedef struct sim_sem* SemaphoreHandle_t;
#ifdef __cplusplus
extern "C" {
#endif
SemaphoreHandle_t sim_sem_create(void);
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) { return sim_sem_create(); }
static inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void) { return sim_sem_create(); }
static inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t* b) { (void)b; return sim_sem_create(); }
static inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutexStatic(StaticSemaphore_t* b) { (void)b; return sim_sem_create(); }
static inline SemaphoreHandle_t xSemaphoreCreateBinary(void) { return sim_sem_create(); }
static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t t) { (void)s; (void)t; return pdTRUE; }
static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t s) { (void)s; return pdTRUE; }
static inline BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t s, TickType_t t) { (void)s; (void)t; return pdTRUE; }
static inline BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t s) { (void)s; return pdTRUE; }
static inline void vSemaphoreDelete(SemaphoreHandle_t s) { (void)s; }
#ifdef __cplusplus
}
#endif
