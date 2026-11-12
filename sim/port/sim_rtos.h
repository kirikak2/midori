// The simulator's stand-ins for tasks and timers (sim/port/rtos_sim.c).
//
// The browser gives one thread. What runs as a FreeRTOS task on the device
// registers a step function here instead, and esp_timer callbacks wait in a
// table; sim_rtos_poll() runs whatever is due. The page calls it from its
// fast loop (MessageChannel) and every frame.
#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

// A step runs one pass of a task's loop without blocking. It returns false
// when the task would have ended (it is then removed).
typedef bool (*sim_step_fn)(void* arg);
void sim_rtos_add_step(const char* name, sim_step_fn fn, void* arg);
void sim_rtos_poll(void);

#ifdef __cplusplus
}
#endif
