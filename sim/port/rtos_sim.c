// esp_timer, queues, semaphores and task steps on the browser's one thread.
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <emscripten.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_app_desc.h"
#include "sim_rtos.h"
#include "midi.h"

// ---- clock

int64_t esp_timer_get_time(void)
{
  return (int64_t)(emscripten_get_now() * 1000.0);
}

// ---- esp_timer: a table polled by sim_rtos_poll()

struct sim_timer {
  esp_timer_cb_t cb;
  void* arg;
  int64_t due;      // 0 = stopped
  int64_t period;   // 0 = one-shot
  struct sim_timer* next;
};

static struct sim_timer* s_timers;

esp_err_t esp_timer_create(const esp_timer_create_args_t* args, esp_timer_handle_t* out)
{
  struct sim_timer* t = calloc(1, sizeof(*t));
  if (!t) return ESP_ERR_NO_MEM;
  t->cb = args->callback;
  t->arg = args->arg;
  t->next = s_timers;
  s_timers = t;
  *out = t;
  return ESP_OK;
}

esp_err_t esp_timer_start_once(esp_timer_handle_t t, uint64_t us)
{
  t->period = 0;
  t->due = esp_timer_get_time() + (int64_t)us;
  return ESP_OK;
}

esp_err_t esp_timer_start_periodic(esp_timer_handle_t t, uint64_t us)
{
  t->period = (int64_t)us;
  t->due = esp_timer_get_time() + (int64_t)us;
  return ESP_OK;
}

esp_err_t esp_timer_restart(esp_timer_handle_t t, uint64_t us)
{
  // Restarting keeps the timer's kind: a periodic one takes the new period
  if (!t->due) return ESP_ERR_INVALID_STATE;
  if (t->period) t->period = (int64_t)us;
  t->due = esp_timer_get_time() + (int64_t)us;
  return ESP_OK;
}

esp_err_t esp_timer_stop(esp_timer_handle_t t)
{
  if (!t->due) return ESP_ERR_INVALID_STATE;
  t->due = 0;
  return ESP_OK;
}

bool esp_timer_is_active(esp_timer_handle_t t) { return t->due != 0; }

esp_err_t esp_timer_delete(esp_timer_handle_t t)
{
  for (struct sim_timer** p = &s_timers; *p; p = &(*p)->next) {
    if (*p == t) { *p = t->next; free(t); return ESP_OK; }
  }
  return ESP_ERR_INVALID_ARG;
}

static void poll_timers(void)
{
  int64_t now = esp_timer_get_time();
  for (struct sim_timer* t = s_timers; t; t = t->next) {
    // A periodic timer that fell behind (a background tab) catches up by
    // skipping, as esp_timer's skip_unhandled_events would
    int guard = 0;
    while (t->due && t->due <= now && guard++ < 64) {
      if (t->period) {
        t->due += t->period;
        if (t->due <= now - 100000) t->due = now + t->period;
      } else {
        t->due = 0;
      }
      t->cb(t->arg);
    }
  }
}

// ---- task steps

#define MAX_STEPS 16
static struct { const char* name; sim_step_fn fn; void* arg; } s_steps[MAX_STEPS];
static int s_nsteps;

void sim_rtos_add_step(const char* name, sim_step_fn fn, void* arg)
{
  if (s_nsteps < MAX_STEPS) {
    s_steps[s_nsteps].name = name;
    s_steps[s_nsteps].fn = fn;
    s_steps[s_nsteps].arg = arg;
    s_nsteps++;
  }
}

static bool s_polling;

EMSCRIPTEN_KEEPALIVE void sim_rtos_poll(void)
{
  if (s_polling) return;   // a callback must not re-enter the loop
  s_polling = true;
  poll_timers();
  MIDI_Input_poll();   // picoruby-midi's input task, one pass (MIDI_INPUT_POLLED)
  for (int i = 0; i < s_nsteps; ) {
    if (s_steps[i].fn(s_steps[i].arg)) { i++; continue; }
    memmove(&s_steps[i], &s_steps[i + 1], sizeof(s_steps[0]) * (size_t)(s_nsteps - i - 1));
    s_nsteps--;
  }
  s_polling = false;
}

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char* name, uint32_t stack,
                                   void* arg, UBaseType_t prio, TaskHandle_t* out, BaseType_t core)
{
  (void)fn; (void)stack; (void)arg; (void)prio; (void)core;
  ESP_LOGW("SIM", "task '%s' cannot run in the simulator (no step function)", name);
  if (out) *out = NULL;
  return pdFAIL;
}

// ---- semaphores (one thread: nothing to wait for)

struct sim_sem { int unused; };
SemaphoreHandle_t sim_sem_create(void) { return calloc(1, sizeof(struct sim_sem)); }

// ---- queues: a ring

struct sim_queue {
  UBaseType_t len, size, head, count;
  uint8_t* buf;
};

QueueHandle_t xQueueCreate(UBaseType_t len, UBaseType_t size)
{
  struct sim_queue* q = calloc(1, sizeof(*q));
  if (!q) return NULL;
  q->buf = malloc((size_t)len * size);
  if (!q->buf) { free(q); return NULL; }
  q->len = len;
  q->size = size;
  return q;
}

BaseType_t xQueueSend(QueueHandle_t q, const void* item, TickType_t wait)
{
  (void)wait;
  if (!q || q->count == q->len) return pdFALSE;
  memcpy(q->buf + (size_t)((q->head + q->count) % q->len) * q->size, item, q->size);
  q->count++;
  return pdTRUE;
}

BaseType_t xQueueReceive(QueueHandle_t q, void* item, TickType_t wait)
{
  (void)wait;
  if (!q || q->count == 0) return pdFALSE;
  memcpy(item, q->buf + (size_t)q->head * q->size, q->size);
  q->head = (q->head + 1) % q->len;
  q->count--;
  return pdTRUE;
}

BaseType_t xQueuePeek(QueueHandle_t q, void* item, TickType_t wait)
{
  (void)wait;
  if (!q || q->count == 0) return pdFALSE;
  memcpy(item, q->buf + (size_t)q->head * q->size, q->size);
  return pdTRUE;
}

BaseType_t xQueueReset(QueueHandle_t q)
{
  if (q) { q->head = 0; q->count = 0; }
  return pdPASS;
}

UBaseType_t uxQueueMessagesWaiting(QueueHandle_t q) { return q ? q->count : 0; }
void vQueueDelete(QueueHandle_t q) { if (q) { free(q->buf); free(q); } }

// ---- log

static vprintf_like_t s_vprintf = vprintf;

vprintf_like_t esp_log_set_vprintf(vprintf_like_t func)
{
  vprintf_like_t old = s_vprintf;
  s_vprintf = func ? func : vprintf;
  return old;
}

static esp_log_level_t s_level = ESP_LOG_INFO;
void esp_log_level_set(const char* tag, esp_log_level_t level) { if (!strcmp(tag, "*")) s_level = level; }

void esp_log_write(esp_log_level_t level, const char* tag, const char* format, ...)
{
  (void)tag;
  if (level > s_level) return;
  va_list ap;
  va_start(ap, format);
  s_vprintf(format, ap);
  va_end(ap);
}

const char* esp_err_to_name(esp_err_t code)
{
  switch (code) {
  case ESP_OK: return "ESP_OK";
  case ESP_ERR_NO_MEM: return "ESP_ERR_NO_MEM";
  case ESP_ERR_INVALID_ARG: return "ESP_ERR_INVALID_ARG";
  case ESP_ERR_INVALID_STATE: return "ESP_ERR_INVALID_STATE";
  case ESP_ERR_NOT_FOUND: return "ESP_ERR_NOT_FOUND";
  case ESP_ERR_NVS_NOT_FOUND: return "ESP_ERR_NVS_NOT_FOUND";
  default: return "ESP_FAIL";
  }
}

// ---- misc

uint32_t esp_random(void)
{
  return (uint32_t)(emscripten_random() * 4294967296.0);
}

#ifndef MIDORI_VERSION
#define MIDORI_VERSION "sim"
#endif
static const esp_app_desc_t s_desc = { MIDORI_VERSION, "midori-sim" };
const esp_app_desc_t* esp_app_get_description(void) { return &s_desc; }
