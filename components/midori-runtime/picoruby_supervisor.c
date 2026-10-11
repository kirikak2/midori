/**
 * @file picoruby_supervisor.c
 * @brief PicoRuby task supervisor for dynamic script switching
 */

#include <string.h>
#include <stdarg.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "picoruby_supervisor.h"
#include "picoruby-esp32.h"
#include "console_input.h"
#include "midi.h"  /* for MIDI_set_stop_check / MIDI_set_cleanup_hook */

#include "picoruby.h"
#include "task.h"
#include "task_hal.h"
#include <mruby/class.h>
#include <mruby/string.h>
#include <mruby/array.h>
#include <mruby/presym.h>

// Defined in picoruby-esp32.c; some gems reach the running VM through it.
extern mrb_state *global_mrb;

/*
 * The VM the PicoRuby task is running, or NULL when there is none. The
 * runner task clears it after closing the VM itself; still set at
 * cleanup_vm() time means the task was deleted mid-run and its VM was
 * never closed.
 */
static mrb_state *volatile s_mrb = NULL;

static const char *TAG = "SUPERVISOR";

// Platform-specific includes
#if defined(CONFIG_USB_MIDI_UI_ENABLED)
#include "ui_common.h"
#endif

// Supervisor command types (internal)
typedef enum {
    CMD_LOAD_SCRIPT,
    CMD_STOP_SCRIPT,
    CMD_ENTER_UI_MODE,
} supervisor_cmd_type_t;

typedef struct {
    supervisor_cmd_type_t cmd;
    char script_path[128];
} supervisor_cmd_t;

// Event bits
#define EVT_TASK_STARTED     (1 << 0)
#define EVT_TASK_COMPLETED   (1 << 1)
#define EVT_TASK_ERROR       (1 << 2)
#define EVT_STOP_REQUESTED   (1 << 3)

// Supervisor state
static supervisor_state_t s_state = SUPERVISOR_STATE_IDLE;
static char s_current_script[128] = {0};
static char s_requested_script[128] = {0};  // Script to run (passed to main_task.rb)
static supervisor_script_result_t s_last_result = {0};
static volatile bool s_has_result = false;

// FreeRTOS handles
static TaskHandle_t s_supervisor_task = NULL;
static TaskHandle_t s_picoruby_task = NULL;
static QueueHandle_t s_cmd_queue = NULL;
static EventGroupHandle_t s_events = NULL;
static SemaphoreHandle_t s_state_mutex = NULL;

// Script request from Ruby side
static char s_ruby_requested_script[128] = {0};
static volatile bool s_ruby_script_requested = false;

// Forward declarations
static void supervisor_task(void *arg);
static void picoruby_runner_task(void *arg);
static bool run_vm_with_main_task(void);
static void register_script_manager_class(mrb_state *mrb);
static void register_console_io_class(mrb_state *mrb);

// External declarations from picoruby-esp32.c
extern void initialize_nvs(void);
extern void picoruby_esp32_midi_cleanup(void);

// Heap size configuration (must match picoruby-esp32.c)
#if defined(CONFIG_IDF_TARGET_ESP32P4) && defined(CONFIG_SPIRAM)
#define HEAP_SIZE (1024 * 1024 * 4)
#elif defined(CONFIG_IDF_TARGET_ESP32S3) && defined(CONFIG_SPIRAM)
#define HEAP_SIZE (1024 * 1024 * 2)
#elif defined(CONFIG_IDF_TARGET_ESP32P4)
#define HEAP_SIZE (1024 * 256)
#else
#define HEAP_SIZE (1024 * 180)
#endif

// C stack of the PicoRuby task. mruby's VM loop keeps large frames, and
// Kernel.load compiles the script (prism, recursive) on top of whatever Ruby
// frames are already live: 32KB, twice what the old mruby/c build used.
#define PICORUBY_TASK_STACK_SIZE (32 * 1024)

// Heap pool (declared in picoruby-esp32.c)
extern uint8_t heap_pool[];

// main_task bytecode (defined in mrb/main_task.c, included by picoruby-esp32.c)
extern const uint8_t main_task[];

void supervisor_init(void)
{
    ESP_LOGI(TAG, "Initializing supervisor...");

    // Create synchronization primitives
    s_cmd_queue = xQueueCreate(4, sizeof(supervisor_cmd_t));
    s_events = xEventGroupCreate();
    s_state_mutex = xSemaphoreCreateMutex();

    if (!s_cmd_queue || !s_events || !s_state_mutex) {
        ESP_LOGE(TAG, "Failed to create FreeRTOS primitives");
        return;
    }

    // Wire picoruby-midi's cooperative-stop hooks to the supervisor's
    // script-stop flag and to the all-notes-off cleanup helper, so the
    // gem itself stays free of picoruby-esp32 symbols.
    MIDI_set_stop_check(picoruby_esp32_stop_requested);
    MIDI_set_cleanup_hook(picoruby_esp32_midi_cleanup);

    // Initialize NVS (needed for some operations)
    initialize_nvs();

    // Create supervisor task on Core 1
    BaseType_t ret = xTaskCreatePinnedToCore(
        supervisor_task,
        "supervisor",
        4096,
        NULL,
        4,  // Higher priority than picoruby task
        &s_supervisor_task,
        1   // Core 1
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create supervisor task");
        return;
    }

    ESP_LOGI(TAG, "Supervisor initialized");
}

bool supervisor_request_script(const char *script_path)
{
    if (!s_cmd_queue) {
        return false;
    }

    supervisor_cmd_t cmd = {0};
    if (script_path) {
        cmd.cmd = CMD_LOAD_SCRIPT;
        strncpy(cmd.script_path, script_path, sizeof(cmd.script_path) - 1);
    } else {
        cmd.cmd = CMD_ENTER_UI_MODE;
    }

    return xQueueSend(s_cmd_queue, &cmd, pdMS_TO_TICKS(100)) == pdPASS;
}

bool supervisor_request_irb(void)
{
    return supervisor_request_script(SUPERVISOR_IRB_PATH);
}

bool supervisor_stop_script(void)
{
    if (!s_cmd_queue || !supervisor_is_script_running()) {
        return false;
    }

    supervisor_cmd_t cmd = {
        .cmd = CMD_STOP_SCRIPT
    };

    return xQueueSend(s_cmd_queue, &cmd, pdMS_TO_TICKS(100)) == pdPASS;
}

/*
 * A PicoRuby task always exists (UI mode runs one too), so "running" means
 * the task was started with a script path rather than for the UI loop.
 */
bool supervisor_is_script_running(void)
{
    return s_state == SUPERVISOR_STATE_RUNNING && s_current_script[0] != '\0';
}

supervisor_state_t supervisor_get_state(void)
{
    return s_state;
}

const char* supervisor_get_current_script(void)
{
    return s_current_script[0] ? s_current_script : NULL;
}

bool supervisor_get_last_result(supervisor_script_result_t *result)
{
    if (!s_has_result || !result) {
        return false;
    }
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    memcpy(result, &s_last_result, sizeof(supervisor_script_result_t));
    xSemaphoreGive(s_state_mutex);
    return true;
}

void supervisor_log(const char *format, ...)
{
    char buf[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

    // Always log to serial
    ESP_LOGI(TAG, "%s", buf);

#if defined(CONFIG_USB_MIDI_UI_ENABLED)
    // Also log to M5Stack UI
    extern void ui_add_log(const char *msg);
    ui_add_log(buf);
#endif
}

const char* supervisor_get_requested_script(void)
{
    return s_requested_script[0] ? s_requested_script : NULL;
}

void supervisor_clear_requested_script(void)
{
    s_requested_script[0] = '\0';
}

void supervisor_ruby_request_script(const char *script_path)
{
    if (script_path) {
        strncpy(s_ruby_requested_script, script_path, sizeof(s_ruby_requested_script) - 1);
        s_ruby_requested_script[sizeof(s_ruby_requested_script) - 1] = '\0';
    } else {
        s_ruby_requested_script[0] = '\0';
    }
    s_ruby_script_requested = true;
    ESP_LOGI(TAG, "Ruby requested script: %s", script_path ? script_path : "(UI mode)");
}

bool supervisor_ruby_script_requested(void)
{
    return s_ruby_script_requested;
}

const char* supervisor_get_ruby_requested_script(void)
{
    return s_ruby_requested_script[0] ? s_ruby_requested_script : NULL;
}

void supervisor_clear_ruby_request(void)
{
    s_ruby_script_requested = false;
    s_ruby_requested_script[0] = '\0';
}

// ============================================================================
// Internal Functions
// ============================================================================

/* An irb session occupies the "current script" slot like any script. */
static bool is_irb_path(const char *path)
{
    return path != NULL && strcmp(path, SUPERVISOR_IRB_PATH) == 0;
}

/**
 * @brief Build error message from an exception object
 * @param mrb     VM the exception belongs to (mrb->exc must be clear)
 * @param exc     Exception object
 * @param buf     Output buffer
 * @param bufsize Buffer size
 */
static void build_error_message(mrb_state *mrb, mrb_value exc, char *buf, size_t bufsize)
{
    // "message (ExceptionClass)", with the location when mruby recorded it
    mrb_value str = mrb_inspect(mrb, exc);
    int offset = snprintf(buf, bufsize, "%s", RSTRING_CSTR(mrb, str));

    // Add call stack (max 3 levels for UI display)
    mrb_value bt = mrb_nil_value();
    if (mrb_respond_to(mrb, exc, MRB_SYM(backtrace))) {
        bt = mrb_funcall_id(mrb, exc, MRB_SYM(backtrace), 0);
    }
    if (mrb_array_p(bt)) {
        for (mrb_int i = 0; i < 3 && i < RARRAY_LEN(bt); i++) {
            int remaining = (int)bufsize - offset;
            if (remaining <= 0) break;
            mrb_value line = mrb_ary_ref(mrb, bt, i);
            if (!mrb_string_p(line)) break;
            offset += snprintf(buf + offset, remaining, "\n  %s", RSTRING_CSTR(mrb, line));
        }
    }
}

/*
 * Delete the finished PicoRuby task.
 *
 * picoruby_runner_task() signals the supervisor and then suspends itself,
 * expecting to be deleted here; without this the stack of every task
 * we start would leak, which adds up quickly now that scripts can be
 * started and stopped from the UI.
 */
static void reap_picoruby_task(void)
{
    if (s_picoruby_task == NULL) {
        return;
    }

    TaskHandle_t task = s_picoruby_task;
    s_picoruby_task = NULL;
    vTaskDelete(task);
}

/*
 * Drop any pending stop/script-change request.
 *
 * Must be done before starting the next task: g_stop_requested stays set
 * after a stop, and a fresh script polling ScriptManager#stop_requested?
 * (as MIDI.bpm_loop does) would otherwise exit on its very first
 * iteration.
 */
static void clear_script_request_flags(void)
{
    extern void picoruby_esp32_clear_stop_flag(void);
    extern char g_requested_script[];

    picoruby_esp32_clear_stop_flag();
    g_requested_script[0] = '\0';
}

/*
 * Reset the UI state a script leaves behind. Pads keep their labels and
 * colours in C, and pad presses queue up for a VM that no longer exists.
 */
static void reset_ui_state(void)
{
#if defined(CONFIG_USB_MIDI_UI_ENABLED)
    extern void ui_pad_clear_all(void);
    extern void ui_knob_clear_all(void);
    extern void ui_event_init(void);
    extern void ui_xypad_reset(void);
    ui_pad_clear_all();
    // Drops every XYPad slot, including ones latched by Hold. This does not
    // itself send MIDI (nothing in C does, see docs/XYPAD.md) -- any latched
    // note is silenced by the existing All Sound Off / All Notes Off sweep
    // this same stop path already sends across every channel.
    ui_xypad_reset();
    // Knobs hold no notes, so there is nothing to silence -- but the previous
    // script's labels and banks must not be left on screen.
    ui_knob_clear_all();
    ui_event_init();
#endif
}

static void stop_picoruby_task(void)
{
    if (s_picoruby_task == NULL) {
        return;
    }

    ESP_LOGI(TAG, "Stopping PicoRuby task...");
    s_state = SUPERVISOR_STATE_STOPPING;

    // Request stop via picoruby-esp32 mechanism
    extern void picoruby_esp32_request_stop(void);
    picoruby_esp32_request_stop();

    // Wait for task to complete (with timeout)
    EventBits_t bits = xEventGroupWaitBits(
        s_events,
        EVT_TASK_COMPLETED | EVT_TASK_ERROR,
        pdTRUE,
        pdFALSE,
        pdMS_TO_TICKS(5000)
    );

    if (!(bits & (EVT_TASK_COMPLETED | EVT_TASK_ERROR))) {
        // Timeout - the script never polled stop_requested?, kill it
        ESP_LOGW(TAG, "Task stop timeout, forcing deletion");
    }

    reap_picoruby_task();
    ESP_LOGI(TAG, "PicoRuby task stopped");
}

static void cleanup_vm(void)
{
    ESP_LOGI(TAG, "Cleaning up VM...");

    if (s_mrb != NULL) {
        // The PicoRuby task was deleted mid-run (it ignored the stop request),
        // so its VM was never closed. It cannot be closed now either: the
        // task may have been preempted halfway through updating an object
        // or the heap. Abandon it instead -- the next
        // mrb_open_with_custom_alloc() re-initialises the whole heap pool.
        // Only state kept
        // outside the pool needs dropping here: the tick timer's VM pointer,
        // and (below) FatFs' volume table.
        ESP_LOGW(TAG, "Abandoning a VM that was not closed");
        mrb_hal_task_final(s_mrb);
        s_mrb = NULL;
        global_mrb = NULL;
    }

    // FatFs[] lives outside the VM heap but points at FATFS objects inside
    // it. A closed VM has unmounted them already (FAT objects unmount when
    // freed); an abandoned one has not, and the next f_mount() would write
    // through the stale pointers (see docs/BUILTIN_METHOD_UAF.md).
    { extern void ff_clear_volumes(void); ff_clear_volumes(); }

    ESP_LOGI(TAG, "VM cleanup complete");
}

static void start_picoruby_task(const char *script_path)
{
    ESP_LOGI(TAG, "Starting PicoRuby task, script: %s",
             script_path ? script_path : "(UI mode)");

    // Anything but irb needs the console back in line-editing mode. Doing
    // it here (rather than trusting the session's own ensure block) also
    // covers an irb task that crashed or was force-stopped.
    if (!is_irb_path(script_path)) {
        console_input_irb_exit();
    }

    // Set the requested script for main_task.rb to read
    if (script_path) {
        strncpy(s_requested_script, script_path, sizeof(s_requested_script) - 1);
        s_requested_script[sizeof(s_requested_script) - 1] = '\0';
        strncpy(s_current_script, script_path, sizeof(s_current_script) - 1);
        s_current_script[sizeof(s_current_script) - 1] = '\0';
    } else {
        s_requested_script[0] = '\0';
        s_current_script[0] = '\0';
    }

    // Clear Ruby request flag
    supervisor_clear_ruby_request();

    // Create the picoruby runner task
    BaseType_t ret = xTaskCreatePinnedToCore(
        picoruby_runner_task,
        "picoruby",
        PICORUBY_TASK_STACK_SIZE,
        NULL,
        3,      // Lower priority than supervisor
        &s_picoruby_task,
        1       // Core 1
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create PicoRuby task");
        s_state = SUPERVISOR_STATE_IDLE;
        return;
    }

    s_state = SUPERVISOR_STATE_RUNNING;
    xEventGroupSetBits(s_events, EVT_TASK_STARTED);
}

static void supervisor_task(void *arg)
{
    (void)arg;
    supervisor_cmd_t cmd;

    ESP_LOGI(TAG, "Supervisor task started");

    // Start in UI mode initially
    start_picoruby_task(NULL);

    while (1) {
        // Check for commands
        if (xQueueReceive(s_cmd_queue, &cmd, pdMS_TO_TICKS(100)) == pdPASS) {
            switch (cmd.cmd) {
                case CMD_LOAD_SCRIPT:
                    ESP_LOGI(TAG, "Command: Load script %s", cmd.script_path);
                    // Stop current task if running
                    if (s_picoruby_task != NULL) {
                        stop_picoruby_task();
                    }
                    // MIDI cleanup before VM reset
                    picoruby_esp32_midi_cleanup();
                    vTaskDelay(pdMS_TO_TICKS(50));  // Allow MIDI messages to be sent
                    // Cleanup and reinitialize VM
                    cleanup_vm();
                    reset_ui_state();
                    clear_script_request_flags();
                    // Start new task
                    start_picoruby_task(cmd.script_path);
                    break;

                case CMD_STOP_SCRIPT:
                    ESP_LOGI(TAG, "Command: Stop script");
                    // s_current_script is empty in UI mode, where the running
                    // task is the UI loop itself and there is nothing to stop.
                    if (s_picoruby_task != NULL && s_current_script[0] != '\0') {
                        supervisor_log("[Script] Stopped: %s", s_current_script);
                        stop_picoruby_task();
                        picoruby_esp32_midi_cleanup();
                        vTaskDelay(pdMS_TO_TICKS(50));  // Allow MIDI messages to be sent
                        cleanup_vm();
                        reset_ui_state();
                        clear_script_request_flags();
                        // Return to UI mode
                        start_picoruby_task(NULL);
                    } else {
                        ESP_LOGI(TAG, "No script running, nothing to stop");
                    }
                    break;

                case CMD_ENTER_UI_MODE:
                    ESP_LOGI(TAG, "Command: Enter UI mode");
                    if (s_picoruby_task != NULL) {
                        stop_picoruby_task();
                    }
                    picoruby_esp32_midi_cleanup();
                    vTaskDelay(pdMS_TO_TICKS(50));
                    cleanup_vm();
                    reset_ui_state();
                    clear_script_request_flags();
                    start_picoruby_task(NULL);
                    break;
            }
        }

        // irb has no cooperative stop check: its editor loop sits in
        // STDIN.read_nonblock and never looks at g_stop_requested. A script
        // tapped on the UI while irb is running would otherwise stay in the
        // request flags with nothing to act on them, so turn it into a load
        // command here - that path stops the task (forcibly, after the
        // timeout) before starting the script.
        if (is_irb_path(s_current_script) && s_picoruby_task != NULL) {
            extern volatile bool g_script_change_requested;
            extern char g_requested_script[];
            if (g_script_change_requested && g_requested_script[0] != '\0') {
                char next_script[128];
                strncpy(next_script, g_requested_script, sizeof(next_script) - 1);
                next_script[sizeof(next_script) - 1] = '\0';
                g_script_change_requested = false;
                g_requested_script[0] = '\0';
                ESP_LOGI(TAG, "irb: UI requested script %s", next_script);
                supervisor_request_script(next_script);
            }
        }

        // Check if Ruby requested a script change
        if (s_ruby_script_requested) {
            const char *requested = supervisor_get_ruby_requested_script();
            ESP_LOGI(TAG, "Ruby requested script change: %s",
                     requested ? requested : "(UI mode)");

            // Wait for current task to complete naturally
            // (Ruby should exit after calling request_script)
            EventBits_t bits = xEventGroupWaitBits(
                s_events,
                EVT_TASK_COMPLETED | EVT_TASK_ERROR,
                pdTRUE,
                pdFALSE,
                pdMS_TO_TICKS(2000)
            );

            if (bits & (EVT_TASK_COMPLETED | EVT_TASK_ERROR)) {
                // Task ended, process the request
                char next_script[128] = {0};
                if (requested) {
                    strncpy(next_script, requested, sizeof(next_script) - 1);
                }
                supervisor_clear_ruby_request();

                // MIDI cleanup
                picoruby_esp32_midi_cleanup();
                vTaskDelay(pdMS_TO_TICKS(50));

                // Delete the finished task (it suspended itself)
                reap_picoruby_task();

                // Cleanup VM
                cleanup_vm();
                reset_ui_state();
                clear_script_request_flags();

                // Start new task
                start_picoruby_task(next_script[0] ? next_script : NULL);
            } else {
                // Timeout waiting for task
                ESP_LOGW(TAG, "Timeout waiting for task to exit, forcing restart");
                supervisor_clear_ruby_request();
                stop_picoruby_task();
                picoruby_esp32_midi_cleanup();
                cleanup_vm();
                reset_ui_state();
                clear_script_request_flags();
                start_picoruby_task(NULL);
            }
        }

        // Check if task completed without a script request (script ended normally)
        EventBits_t bits = xEventGroupWaitBits(
            s_events,
            EVT_TASK_COMPLETED | EVT_TASK_ERROR,
            pdTRUE,
            pdFALSE,
            0  // Don't wait
        );

        if ((bits & (EVT_TASK_COMPLETED | EVT_TASK_ERROR)) && !s_ruby_script_requested) {
            reap_picoruby_task();

            // Log the result
            if (bits & EVT_TASK_ERROR) {
                supervisor_log("[Script] Error: %s", s_last_result.error_message);
            } else {
                supervisor_log("[Script] Completed in %lu ms",
                              (unsigned long)s_last_result.execution_time_ms);
            }

            // MIDI cleanup
            picoruby_esp32_midi_cleanup();
            vTaskDelay(pdMS_TO_TICKS(50));

            // Check if there's a pending UI script change request (e.g., user selected another script)
            extern volatile bool g_script_change_requested;
            extern char g_requested_script[];
            if (g_script_change_requested && g_requested_script[0] != '\0') {
                // UI requested a script change - run that script instead of returning to UI mode
                char next_script[128];
                strncpy(next_script, g_requested_script, sizeof(next_script) - 1);
                next_script[sizeof(next_script) - 1] = '\0';

                // Clear the request flags
                g_script_change_requested = false;
                extern volatile bool g_stop_requested;
                g_stop_requested = false;
                g_requested_script[0] = '\0';

                ESP_LOGI(TAG, "UI requested script change: %s", next_script);

                // Cleanup VM and start the requested script
                cleanup_vm();
                reset_ui_state();
                start_picoruby_task(next_script);
            } else {
                // No pending request, return to UI mode
                ESP_LOGI(TAG, "Script ended, returning to UI mode");
                cleanup_vm();
                reset_ui_state();
                clear_script_request_flags();
                start_picoruby_task(NULL);
            }
        }
    }
}

static void picoruby_runner_task(void *arg)
{
    (void)arg;
    uint32_t start_time = xTaskGetTickCount();

    ESP_LOGI(TAG, "PicoRuby runner task started");

    // Run the VM with main_task.rb (returns success/failure)
    bool success = run_vm_with_main_task();

    // Calculate execution time
    uint32_t end_time = xTaskGetTickCount();
    uint32_t exec_time = (end_time - start_time) * portTICK_PERIOD_MS;

    // Store result (error_message is already set in run_vm_with_main_task if error)
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    s_last_result.execution_time_ms = exec_time;
    strncpy(s_last_result.script_path, s_current_script, sizeof(s_last_result.script_path) - 1);
    if (success) {
        s_last_result.success = true;
        s_last_result.error_message[0] = '\0';
    }
    // else: success=false and error_message already set by run_vm_with_main_task()
    s_has_result = true;
    xSemaphoreGive(s_state_mutex);

    ESP_LOGI(TAG, "PicoRuby runner task completed (exec time: %lu ms, success: %d)",
             (unsigned long)exec_time, success);

    // Notify supervisor with appropriate event
    if (success) {
        xEventGroupSetBits(s_events, EVT_TASK_COMPLETED);
    } else {
        xEventGroupSetBits(s_events, EVT_TASK_ERROR);
    }

    // Suspend self - supervisor will delete
    vTaskSuspend(NULL);
}

// ScriptManager methods for supervisor integration

extern void picoruby_esp32_clear_script_list(void);
extern bool picoruby_esp32_add_script(const char *filename);
extern void picoruby_esp32_set_script_list_ready(bool ready);
extern bool picoruby_esp32_stop_requested(void);

static mrb_value c_sm_clear(mrb_state *mrb, mrb_value self)
{
    picoruby_esp32_clear_script_list();
    return mrb_nil_value();
}

static mrb_value c_sm_add(mrb_state *mrb, mrb_value self)
{
    mrb_value name;
    mrb_get_args(mrb, "o", &name);
    if (!mrb_string_p(name)) {
        return mrb_false_value();
    }
    return mrb_bool_value(picoruby_esp32_add_script(RSTRING_CSTR(mrb, name)));
}

static mrb_value c_sm_set_ready(mrb_state *mrb, mrb_value self)
{
    mrb_value ready = mrb_true_value();
    mrb_get_args(mrb, "|o", &ready);
    picoruby_esp32_set_script_list_ready(!mrb_false_p(ready));
    return mrb_nil_value();
}

static mrb_value c_sm_get_requested(mrb_state *mrb, mrb_value self)
{
    extern volatile bool g_script_change_requested;
    extern char g_requested_script[];
    if (g_script_change_requested && g_requested_script[0] != '\0') {
        ESP_LOGI(TAG, "get_requested returning: %s", g_requested_script);
        return mrb_str_new_cstr(mrb, g_requested_script);
    }
    return mrb_nil_value();
}

static mrb_value c_sm_clear_request(mrb_state *mrb, mrb_value self)
{
    extern volatile bool g_script_change_requested;
    extern volatile bool g_stop_requested;
    extern char g_requested_script[];
    ESP_LOGI(TAG, "clear_request called - clearing all stop flags");
    g_script_change_requested = false;
    g_stop_requested = false;
    g_requested_script[0] = '\0';
    return mrb_nil_value();
}

static mrb_value c_sm_stop_requested(mrb_state *mrb, mrb_value self)
{
    return mrb_bool_value(picoruby_esp32_stop_requested());
}

/*
 * Pick up a script path typed on the serial console.
 *
 * Reading the link, echoing and line editing all happen in the console
 * task (console_input.c) so nothing is lost between these polls; this
 * method only dequeues a completed "load <path>" command. The actual load
 * stays on the Ruby side because only PicoRuby's VFS can reach /sd.
 */
static mrb_value c_sm_check_console(mrb_state *mrb, mrb_value self)
{
    char script_path[128];
    if (console_input_pop_command(script_path, sizeof(script_path))) {
        return mrb_str_new_cstr(mrb, script_path);
    }
    return mrb_nil_value();
}

/*
 * PicoModem (file transfer with the PicoRuby web terminal).
 *
 * The console task answers the host's STX handshake on its own and then
 * pumps raw bytes into PicoRuby's stdin, but the transfer itself has to
 * run here: PicoModem reads and writes files through File/VFS, and only
 * PicoRuby's VFS knows about /sd. See docs/PICOMODEM.md.
 */
static mrb_value c_sm_modem_requested(mrb_state *mrb, mrb_value self)
{
    return mrb_bool_value(console_input_modem_pending());
}

static mrb_value c_sm_modem_end(mrb_state *mrb, mrb_value self)
{
    console_input_modem_exit();
    return mrb_nil_value();
}

/*
 * ConsoleIO#write — binary-safe counterpart of $stdout.write.
 *
 * $stdout goes through picorb_hal_write() and the stdio VFS, which
 * rewrites LF as CRLF and would corrupt every frame carrying a 0x0A. This
 * writes the string's bytes, length-delimited, straight to the link.
 */
static mrb_value c_console_io_write(mrb_state *mrb, mrb_value self)
{
    mrb_value str;
    mrb_get_args(mrb, "o", &str);
    if (!mrb_string_p(str)) {
        mrb_raise(mrb, E_TYPE_ERROR, "ConsoleIO#write expects a String");
    }
    int written = console_input_write_raw((const uint8_t *)RSTRING_PTR(str),
                                          (size_t)RSTRING_LEN(str));
    return mrb_fixnum_value(written);
}

static void register_console_io_class(mrb_state *mrb)
{
    struct RClass *cls = mrb_define_class(mrb, "ConsoleIO", mrb->object_class);
    mrb_define_method(mrb, cls, "write", c_console_io_write, MRB_ARGS_REQ(1));
}

static mrb_value c_sm_cleanup_midi(mrb_state *mrb, mrb_value self)
{
    ESP_LOGI(TAG, "Ruby requested MIDI cleanup");
    picoruby_esp32_midi_cleanup();
    return mrb_nil_value();
}

static mrb_value c_sm_free_heap(mrb_state *mrb, mrb_value self)
{
    return mrb_fixnum_value((mrb_int)esp_get_free_heap_size());
}

/*
 * irb mode.
 *
 * The supervisor starts an interactive session the same way it starts a
 * script, passing SUPERVISOR_IRB_PATH through the requested-script slot;
 * main_task.rb asks these methods which of the three modes (irb, script,
 * UI) it was started in.
 *
 * irb_begin/irb_end frame the session: between them the console task
 * stops echoing and line-editing and feeds raw bytes to PicoRuby's stdin,
 * which is where Editor::Line reads its keystrokes. See docs/IRB.md.
 */
static mrb_value c_sm_irb_requested(mrb_state *mrb, mrb_value self)
{
    return mrb_bool_value(is_irb_path(supervisor_get_requested_script()));
}

static mrb_value c_sm_irb_begin(mrb_state *mrb, mrb_value self)
{
    console_input_irb_enter();
    return mrb_nil_value();
}

static mrb_value c_sm_irb_end(mrb_state *mrb, mrb_value self)
{
    console_input_irb_exit();
    return mrb_nil_value();
}

static mrb_value c_sm_get_autorun_script(mrb_state *mrb, mrb_value self)
{
    const char *script = supervisor_get_requested_script();
    // irb is not a file: main_task.rb reaches it through irb_requested?
    if (is_irb_path(script)) {
        return mrb_nil_value();
    }
    if (script && script[0] != '\0') {
        ESP_LOGI(TAG, "get_autorun_script returning: %s", script);
        return mrb_str_new_cstr(mrb, script);
    }
    return mrb_nil_value();
}

static mrb_value c_sm_request_script(mrb_state *mrb, mrb_value self)
{
    mrb_value path = mrb_nil_value();
    mrb_get_args(mrb, "|o", &path);
    supervisor_ruby_request_script(mrb_string_p(path) ? RSTRING_CSTR(mrb, path) : NULL);
    return mrb_nil_value();
}

static mrb_value c_sm_sd_refresh_requested(mrb_state *mrb, mrb_value self)
{
    extern volatile bool g_sd_refresh_requested;
    return mrb_bool_value(g_sd_refresh_requested);
}

static mrb_value c_sm_clear_sd_refresh(mrb_state *mrb, mrb_value self)
{
    extern volatile bool g_sd_refresh_requested;
    g_sd_refresh_requested = false;
    ESP_LOGI(TAG, "SD refresh request cleared");
    return mrb_nil_value();
}

static mrb_value c_sm_add_log(mrb_state *mrb, mrb_value self)
{
    mrb_value str;
    mrb_get_args(mrb, "o", &str);
    if (!mrb_string_p(str)) {
        return mrb_nil_value();
    }
    const char *text = RSTRING_CSTR(mrb, str);
#if defined(CONFIG_USB_MIDI_UI_ENABLED)
    extern void ui_add_log(const char *msg);
    ui_add_log(text);
#else
    ESP_LOGI(TAG, "[Log] %s", text);
#endif
    return mrb_nil_value();
}

static void register_script_manager_class(mrb_state *mrb)
{
    struct RClass *cls = mrb_define_class(mrb, "ScriptManager", mrb->object_class);
    mrb_define_method(mrb, cls, "clear", c_sm_clear, MRB_ARGS_NONE());
    mrb_define_method(mrb, cls, "add", c_sm_add, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, cls, "set_ready", c_sm_set_ready, MRB_ARGS_OPT(1));
    mrb_define_method(mrb, cls, "get_requested", c_sm_get_requested, MRB_ARGS_NONE());
    mrb_define_method(mrb, cls, "clear_request", c_sm_clear_request, MRB_ARGS_NONE());
    mrb_define_method(mrb, cls, "stop_requested?", c_sm_stop_requested, MRB_ARGS_NONE());
    mrb_define_method(mrb, cls, "check_console", c_sm_check_console, MRB_ARGS_NONE());
    mrb_define_method(mrb, cls, "modem_requested?", c_sm_modem_requested, MRB_ARGS_NONE());
    mrb_define_method(mrb, cls, "modem_end", c_sm_modem_end, MRB_ARGS_NONE());
    mrb_define_method(mrb, cls, "cleanup_midi", c_sm_cleanup_midi, MRB_ARGS_NONE());
    mrb_define_method(mrb, cls, "free_heap", c_sm_free_heap, MRB_ARGS_NONE());
    mrb_define_method(mrb, cls, "sd_refresh_requested?", c_sm_sd_refresh_requested, MRB_ARGS_NONE());
    mrb_define_method(mrb, cls, "clear_sd_refresh", c_sm_clear_sd_refresh, MRB_ARGS_NONE());
    mrb_define_method(mrb, cls, "add_log", c_sm_add_log, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, cls, "get_autorun_script", c_sm_get_autorun_script, MRB_ARGS_NONE());
    mrb_define_method(mrb, cls, "request_script", c_sm_request_script, MRB_ARGS_OPT(1));
    mrb_define_method(mrb, cls, "irb_requested?", c_sm_irb_requested, MRB_ARGS_NONE());
    mrb_define_method(mrb, cls, "irb_begin", c_sm_irb_begin, MRB_ARGS_NONE());
    mrb_define_method(mrb, cls, "irb_end", c_sm_irb_end, MRB_ARGS_NONE());
    ESP_LOGI(TAG, "ScriptManager class registered");
}

static bool run_vm_with_main_task(void)
{
    ESP_LOGI(TAG, "Initializing VM...");

    // A fresh VM per run: mrb_open_with_custom_alloc() re-initialises the
    // heap pool, so nothing of the previous script survives.
    mrb_state *mrb = mrb_open_with_custom_alloc(heap_pool, HEAP_SIZE);
    if (mrb == NULL) {
        xSemaphoreTake(s_state_mutex, portMAX_DELAY);
        snprintf(s_last_result.error_message, sizeof(s_last_result.error_message),
                 "Failed to open mruby VM");
        s_last_result.success = false;
        xSemaphoreGive(s_state_mutex);
        ESP_LOGE(TAG, "Failed to open mruby VM");
        return false;
    }
    s_mrb = mrb;
    global_mrb = mrb;

    // mrb_open() runs every gem's initializer and mrblib. If one of them
    // raises, mrb_open() stops there and still returns the VM, with the
    // exception in mrb->exc and every later gem uninitialised -- running
    // main_task.rb on that only produces confusing errors further down
    // (a missing MIDI.start!, say). Report the real cause and give up.
    if (mrb->exc) {
        mrb_value exc = mrb_obj_value(mrb->exc);
        mrb->exc = NULL;
        char msg[200];
        build_error_message(mrb, exc, msg, sizeof(msg));
        xSemaphoreTake(s_state_mutex, portMAX_DELAY);
        snprintf(s_last_result.error_message, sizeof(s_last_result.error_message),
                 "Gem initialization failed: %s", msg);
        s_last_result.success = false;
        xSemaphoreGive(s_state_mutex);
        ESP_LOGE(TAG, "Gem initialization failed: %s", msg);
        mrb_close(mrb);
        global_mrb = NULL;
        s_mrb = NULL;
        return false;
    }

    // Gems are initialised by mrb_open(), so require has nothing to set up.
    register_script_manager_class(mrb);
    register_console_io_class(mrb);

    bool success = true;
    mrb_value exc = mrb_nil_value();

    mrc_ccontext *cc = mrc_ccontext_new(mrb);
    mrc_irep *irep = mrb_read_irep(mrb, main_task);
    mrb_value name = mrb_str_new_lit(mrb, "main_task");
    mrb_value task = mrb_nil_value();
    if (irep != NULL) {
        task = mrc_create_task(cc, irep, name, mrb_nil_value(), mrb_obj_value(mrb->top_self));
    }
    if (mrb_nil_p(task)) {
        ESP_LOGE(TAG, "Failed to create main_task");
        success = false;
    } else {
        // Run the VM (this blocks until all tasks complete)
        mrb_task_run(mrb);

        // An exception that escaped main_task.rb is left in mrb->exc, or
        // kept as the task's result value.
        if (mrb->exc) {
            exc = mrb_obj_value(mrb->exc);
        } else {
            mrb_value result = mrb_task_value(mrb, task);
            if (mrb_obj_is_kind_of(mrb, result, E_EXCEPTION)) {
                exc = result;
            }
        }
    }
    mrc_ccontext_free(cc);

    if (!mrb_nil_p(exc)) {
        success = false;
        mrb->exc = NULL;  // inspect/backtrace below run Ruby code

        xSemaphoreTake(s_state_mutex, portMAX_DELAY);
        build_error_message(mrb, exc, s_last_result.error_message,
                            sizeof(s_last_result.error_message));
        s_last_result.success = false;
        xSemaphoreGive(s_state_mutex);

        ESP_LOGE(TAG, "Script error: %s", s_last_result.error_message);
    } else if (!success) {
        xSemaphoreTake(s_state_mutex, portMAX_DELAY);
        snprintf(s_last_result.error_message, sizeof(s_last_result.error_message),
                 "Failed to create main_task");
        s_last_result.success = false;
        xSemaphoreGive(s_state_mutex);
    }

    // Close the VM here, on its own task. Gem finalizers run and objects are
    // freed (FAT volumes unmount themselves); the tick timer lets go of it in
    // mrb_hal_task_final().
    mrb_close(mrb);
    global_mrb = NULL;
    s_mrb = NULL;

    ESP_LOGI(TAG, "main_task.rb completed (success=%d)", success);
    return success;
}
