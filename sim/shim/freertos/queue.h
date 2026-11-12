#pragma once
#include "FreeRTOS.h"
typedef struct sim_queue* QueueHandle_t;
#ifdef __cplusplus
extern "C" {
#endif
QueueHandle_t xQueueCreate(UBaseType_t len, UBaseType_t item_size);
// Never waits: on one thread nothing could fill or empty the queue meanwhile
BaseType_t xQueueSend(QueueHandle_t q, const void* item, TickType_t wait);
BaseType_t xQueueReceive(QueueHandle_t q, void* item, TickType_t wait);
BaseType_t xQueuePeek(QueueHandle_t q, void* item, TickType_t wait);
BaseType_t xQueueReset(QueueHandle_t q);
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t q);
void vQueueDelete(QueueHandle_t q);
#define xQueueSendToBack xQueueSend
#define xQueueSendFromISR(q, i, w) xQueueSend(q, i, 0)
#ifdef __cplusplus
}
#endif
