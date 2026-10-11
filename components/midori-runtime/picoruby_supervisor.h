/**
 * @file picoruby_supervisor.h
 * @brief PicoRuby task supervisor for dynamic script switching
 *
 * This supervisor manages the lifecycle of PicoRuby tasks, allowing scripts
 * to be loaded and switched without restarting the ESP32.
 */

#ifndef PICORUBY_SUPERVISOR_H
#define PICORUBY_SUPERVISOR_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Script execution result
 */
typedef struct {
    bool success;                   ///< true if script completed successfully
    char error_message[256];        ///< Error message if script failed
    uint32_t execution_time_ms;     ///< Execution time in milliseconds
    char script_path[128];          ///< Path of the executed script
} supervisor_script_result_t;

/**
 * @brief Pseudo script path that starts an interactive Ruby session
 *
 * irb is run exactly like a script - own PicoRuby task, own VM, back to UI
 * mode when it ends - so it travels through the same "requested script"
 * slot. The leading colon keeps it apart from any real path on /sd.
 * See docs/IRB.md.
 */
#define SUPERVISOR_IRB_PATH ":irb"

/**
 * @brief Supervisor state
 */
typedef enum {
    SUPERVISOR_STATE_IDLE,          ///< No script running, waiting for request
    SUPERVISOR_STATE_RUNNING,       ///< Script is currently running
    SUPERVISOR_STATE_STOPPING,      ///< Script is being stopped
} supervisor_state_t;

/**
 * @brief Initialize and start the supervisor task
 *
 * Creates the supervisor task on Core 1 which manages PicoRuby task lifecycle.
 * The supervisor starts in UI mode (no script specified).
 */
void supervisor_init(void);

/**
 * @brief Request to load and run a script
 *
 * If a script is currently running, it will be stopped first.
 * The supervisor will reinitialize the VM and run the new script.
 *
 * @param script_path Full path to the script file (e.g., "/sd/app.rb")
 *                    Pass NULL to return to UI mode
 * @return true if request was accepted, false otherwise
 */
bool supervisor_request_script(const char *script_path);

/**
 * @brief Request an interactive Ruby (irb) session
 *
 * Shorthand for supervisor_request_script(SUPERVISOR_IRB_PATH): stops
 * whatever is running, resets the VM and starts main_task.rb in irb mode.
 * The session reads and writes the serial console (USB CDC in
 * midi_device mode) and ends on "quit", "exit" or Ctrl-D.
 *
 * @return true if the request was accepted
 */
bool supervisor_request_irb(void);

/**
 * @brief Request to stop the currently running script
 *
 * The script will be stopped and the supervisor will return to UI mode.
 *
 * @return true if request was accepted, false if no script running
 */
bool supervisor_stop_script(void);

/**
 * @brief Check if a script is currently running
 *
 * @return true if a script is running
 */
bool supervisor_is_script_running(void);

/**
 * @brief Get current supervisor state
 *
 * @return Current state
 */
supervisor_state_t supervisor_get_state(void);

/**
 * @brief Get the path of the currently running script
 *
 * @return Script path or NULL if no script is running
 */
const char* supervisor_get_current_script(void);

/**
 * @brief Get the result of the last script execution
 *
 * @param result Pointer to structure to receive result
 * @return true if result is available, false if no previous script
 */
bool supervisor_get_last_result(supervisor_script_result_t *result);

/**
 * @brief Log a message through the supervisor (platform-specific)
 *
 * On Freenove: logs to serial console
 * On M5Stack: logs to UI log screen
 *
 * @param format printf-style format string
 * @param ... Format arguments
 */
void supervisor_log(const char *format, ...);

/**
 * @brief Get the script path requested via supervisor (called from Ruby)
 *
 * This is called by main_task.rb to check if supervisor passed a script path.
 *
 * @return Script path if in script mode, NULL if in UI mode
 */
const char* supervisor_get_requested_script(void);

/**
 * @brief Clear the requested script (called from Ruby after processing)
 */
void supervisor_clear_requested_script(void);

/**
 * @brief Notify supervisor that Ruby script wants to load a new script
 *
 * Called from Ruby via ScriptManager.request_script()
 * This causes the current script to exit and supervisor to start the new one.
 *
 * @param script_path Script to load (NULL for UI mode)
 */
void supervisor_ruby_request_script(const char *script_path);

/**
 * @brief Check if Ruby requested a script change
 *
 * @return true if script change was requested from Ruby
 */
bool supervisor_ruby_script_requested(void);

/**
 * @brief Get the script path requested from Ruby
 *
 * @return Script path or NULL
 */
const char* supervisor_get_ruby_requested_script(void);

/**
 * @brief Clear the Ruby script request after handling
 */
void supervisor_clear_ruby_request(void);

#ifdef __cplusplus
}
#endif

#endif // PICORUBY_SUPERVISOR_H
