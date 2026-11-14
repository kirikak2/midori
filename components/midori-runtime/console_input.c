/*
 * Midori serial console input
 *
 * Owns everything between "bytes arrive on the console link" and "the Ruby
 * side is handed a complete command":
 *
 *   console on CDC (USB-MIDI-Device mode, except on the CrowPanel)
 *     TinyUSB task --console_cdc_rx_cb()--> RX stream buffer --+
 *                                                              |
 *   console on UART / USB-Serial-JTAG (everything else)        |
 *     console task polls getchar() -----------------------------+
 *                                                              v
 *                                    console task: echo + line editing
 *                                                              |
 *                                              command queue <-+
 *                                                              |
 *                       ScriptManager#check_console (Ruby VM) <-+
 *
 * Why the CDC hook is mandatory in CDC mode:
 *   picoruby-usb_midi_device registers its own .callback_rx with
 *   esp_tinyusb, and that handler drains the CDC FIFO with
 *   tinyusb_cdcacm_read() on every RX event. With no application callback
 *   installed the bytes are read out and dropped on the floor, so a
 *   getchar() on the CDC VFS finds tud_cdc_n_available() == 0 and most
 *   keystrokes never reach the console at all.
 *
 * Why a dedicated task (same shape as picoruby-midi's midi_input task):
 *   - The CDC hook runs in the TinyUSB task and must return immediately,
 *     so it only copies bytes into a stream buffer.
 *   - Line editing, echo and command parsing then run at priority 4,
 *     decoupled from the Ruby VM task (priority 3), which sleeps 100 ms
 *     between polls. Input is buffered and processed while the VM sleeps,
 *     while it is busy, and while a script is running.
 *
 * Core affinity: the task echoes with printf(), which in CDC mode ends up
 * in tud_cdc_n_write_flush(). Like the gem's MIDI TX task it therefore
 * stays on the TinyUSB core (Core 1) so it never runs in true parallel
 * with tud_task.
 *
 * PicoModem mode:
 *   A bare STX (0x02) from the host switches the task out of line editing
 *   and into a byte pump feeding PicoRuby's stdin ring buffer, so that
 *   PicoModem.session can run the binary transfer protocol on the Ruby
 *   side (only PicoRuby's VFS can reach /sd). See docs/PICOMODEM.md.
 *
 * irb mode:
 *   The "irb" command asks the supervisor for an interactive Ruby task.
 *   That task brings its own line editor (Editor::Line, reading STDIN), so
 *   the console task hands the link over for the duration of the session -
 *   same byte pump as PicoModem in CDC mode. See docs/IRB.md.
 *
 * attach mode:
 *   The "attach" command joins the Ruby VM that is already running (a
 *   script, or UI mode) instead of starting a new one. That VM is busy
 *   with its own loop, so the line editing stays here: every finished
 *   line goes into the attach queue, and the Ruby side evaluates it the
 *   next time the script passes MIDI.run_filters (UI.process,
 *   MIDI.sleep_ms, the clock loops) and asks for the next prompt.
 *   "eval <id> <code>" / "eval64" are the same thing for machines (the
 *   web simulator), with framed answers. See docs/ATTACH.md.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/stream_buffer.h"
#include "esp_log.h"
#include "esp_system.h"
#include "sdkconfig.h"

#include "console_input.h"
#include "picoruby_supervisor.h"
#include "picoruby-esp32.h"

#if CONFIG_USB_MIDI_CONSOLE_ON_CDC
#include "usb_midi_device.h"
#include "tinyusb_cdc_acm.h"
#elif CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#include "driver/usb_serial_jtag_vfs.h"
#else
#include "driver/uart_vfs.h"
#endif

/*
 * picoruby-machine/ports/esp32/machine.c: pushes one byte into the ring
 * buffer that STDIN.read_nonblock drains. Declared here rather than via
 * hal.h, which drags in the Ruby VM headers.
 */
extern bool picorb_hal_stdin_push(uint8_t ch);
/* Same file: pops one byte, or HAL_GETCHAR_NODATA (-1) when empty. */
extern int picorb_hal_getchar(void);
/* picoruby-io-console/ports/esp32/io-console.c */
extern void io_raw_bang(bool nonblock);

static const char *TAG = "CONSOLE";

#define CONSOLE_TASK_STACK_SIZE  4096
#define CONSOLE_TASK_PRIORITY    4    /* above the Ruby VM task (3),
                                       * below the TinyUSB task (5) */
#define CONSOLE_TASK_CORE        1    /* same core as tud_task */

#define CONSOLE_RX_STREAM_SIZE   1024 /* raw bytes buffered from the link */
#define CONSOLE_LINE_MAX         256  /* longest editable line */
#define CONSOLE_CMD_MAX          128  /* matches the supervisor's path buffer */
#define CONSOLE_CMD_QUEUE_DEPTH  4    /* commands waiting for the Ruby side */
#define CONSOLE_POLL_INTERVAL_MS 10   /* stdin polling fallback */

#define CONSOLE_STX              0x02 /* host asks for a PicoModem session */
#define CONSOLE_ACK              0x06 /* our answer, before going binary */
#define CONSOLE_CDC_TX_CHUNK     64   /* bytes per write_queue/flush pair */
#define CONSOLE_ATTACH_DEPTH     4    /* lines waiting for the Ruby side */
#define CONSOLE_ATTACH_WAIT_MS   3000 /* "the script is not polling" notice */
#define CONSOLE_TICK_MS          200  /* CDC mode: wake up for the notice */
#define CONSOLE_CDC_TX_TIMEOUT   pdMS_TO_TICKS(200)

/*
 * What the console task does with an incoming byte. LINE is the normal
 * command line; the other two hand the link to the Ruby side and differ
 * only in who ends the session.
 */
typedef enum {
    CONSOLE_MODE_LINE = 0,  /* echo + line editing + command parsing */
    CONSOLE_MODE_MODEM,     /* PicoModem frames -> PicoRuby stdin */
    CONSOLE_MODE_IRB,       /* irb keystrokes -> PicoRuby stdin */
    CONSOLE_MODE_ATTACH,    /* line editing here, lines -> the running VM */
} console_mode_t;

static StreamBufferHandle_t s_rx_stream  = NULL;
static QueueHandle_t        s_cmd_queue  = NULL;
static TaskHandle_t         s_task       = NULL;
static bool                 s_cdc_mode   = false;
/* Written by the TinyUSB task, cleared by the console task: a lost update
 * only costs a warning line. */
static volatile uint32_t    s_rx_dropped = 0;
/* MODEM is entered by the console task itself (on STX) and left by the
 * Ruby side; IRB is entered and left by the Ruby side. */
static volatile console_mode_t s_mode     = CONSOLE_MODE_LINE;
static volatile uint32_t    s_raw_lost   = 0;

/* attach mode (see the section below). The queue carries both the human
 * session's lines and the machines' one-shot evals. */
static QueueHandle_t        s_attach_queue   = NULL;
static portMUX_TYPE         s_attach_mux     = portMUX_INITIALIZER_UNLOCKED;
static volatile bool        s_attach_running = false; /* a line is being evaluated */
static volatile bool        s_attach_cancel  = false; /* Ctrl-C / detach for Ruby */
static volatile bool        s_attach_more    = false; /* Ruby waits for more lines */
static volatile TickType_t  s_attach_queued_at = 0;
static volatile bool        s_attach_noticed = false;

/* Line editor state. Kept on the console task's stack rather than in .bss
 * (see docs/MEMORY_ALLOCATION.md on static buffers and DRAM layout). */
typedef struct {
    char    buf[CONSOLE_LINE_MAX];
    size_t  len;
    bool    last_was_cr;
    uint8_t esc;   /* 0: normal, 1: after ESC, 2: inside a CSI sequence */
} console_line_t;

/*--------------------------------------------------------------------+
 * Output helpers
 *--------------------------------------------------------------------*/

static void console_prompt(void)
{
    printf("> ");
    fflush(stdout);
}

static void console_help(void)
{
    printf("Commands:\n");
    printf("  load /sd/app.rb  - Load and run a script\n");
    printf("  irb              - Start an interactive Ruby session\n");
    printf("  attach           - Run Ruby inside the running script (or UI mode)\n");
    printf("  stop             - Stop the running script (back to UI mode)\n");
    printf("  ls [dir]         - List a directory (/, /sd, /plugins ...; UI mode)\n");
    printf("  rm <path>        - Delete a file (UI mode)\n");
    printf("  mv <from> <to>   - Move a file, across volumes too (UI mode)\n");
    printf("  rescan           - Read the SD card again (the Scripts screen's list)\n");
    printf("  info             - Board, mode and running script, in one line\n");
    printf("  heap             - Show free heap memory\n");
    printf("  restart          - Restart ESP32\n");
    fflush(stdout);
}

/*--------------------------------------------------------------------+
 * PicoModem mode
 *
 * In CDC mode the frames bypass stdio entirely and go to TinyUSB. The
 * other USB port modes have no such shortcut, so there the stdio VFS is
 * asked to stop rewriting line endings for the duration of a transfer.
 *
 * Caveat outside CDC mode: picoruby-machine runs its own stdin_reader
 * task that polls the very same link (usb_serial_jtag_ll directly, or
 * fgetc on the UART VFS), so two readers compete for each byte. Line
 * editing tolerates that; a binary protocol may see reordered bytes.
 *--------------------------------------------------------------------*/

#if !CONFIG_USB_MIDI_CONSOLE_ON_CDC
/*
 * The console VFS rewrites LF as CRLF on the way out and CR as LF on the
 * way in (CONFIG_LIBC_STD{OUT,IN}_LINE_ENDING_*), either of which would
 * corrupt a frame. Both directions are switched off around a transfer.
 */
#if defined(CONFIG_LIBC_STDOUT_LINE_ENDING_CRLF) || defined(CONFIG_NEWLIB_STDOUT_LINE_ENDING_CRLF)
#define CONSOLE_TX_DEFAULT_ENDINGS ESP_LINE_ENDINGS_CRLF
#elif defined(CONFIG_LIBC_STDOUT_LINE_ENDING_CR) || defined(CONFIG_NEWLIB_STDOUT_LINE_ENDING_CR)
#define CONSOLE_TX_DEFAULT_ENDINGS ESP_LINE_ENDINGS_CR
#else
#define CONSOLE_TX_DEFAULT_ENDINGS ESP_LINE_ENDINGS_LF
#endif

#if defined(CONFIG_LIBC_STDIN_LINE_ENDING_CRLF) || defined(CONFIG_NEWLIB_STDIN_LINE_ENDING_CRLF)
#define CONSOLE_RX_DEFAULT_ENDINGS ESP_LINE_ENDINGS_CRLF
#elif defined(CONFIG_LIBC_STDIN_LINE_ENDING_CR) || defined(CONFIG_NEWLIB_STDIN_LINE_ENDING_CR)
#define CONSOLE_RX_DEFAULT_ENDINGS ESP_LINE_ENDINGS_CR
#else
#define CONSOLE_RX_DEFAULT_ENDINGS ESP_LINE_ENDINGS_LF
#endif

static void console_set_tx_endings(esp_line_endings_t mode)
{
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    usb_serial_jtag_vfs_set_tx_line_endings(mode);
#else
    uart_vfs_dev_port_set_tx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, mode);
#endif
}

static void console_set_rx_endings(esp_line_endings_t mode)
{
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    usb_serial_jtag_vfs_set_rx_line_endings(mode);
#else
    uart_vfs_dev_port_set_rx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, mode);
#endif
}
#endif /* !CONFIG_USB_MIDI_CONSOLE_ON_CDC */

int console_input_write_raw(const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0) {
        return 0;
    }

#if CONFIG_USB_MIDI_CONSOLE_ON_CDC
    /* Straight to TinyUSB: the stdio VFS would translate LF to CRLF and
     * flushes one byte at a time. */
    size_t off = 0;
    int stalled = 0;
    while (off < len) {
        size_t chunk = len - off;
        if (chunk > CONSOLE_CDC_TX_CHUNK) chunk = CONSOLE_CDC_TX_CHUNK;

        size_t queued = tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0,
                                                   data + off, chunk);
        if (queued == 0) {
            /* Ring buffer full — let the host drain it, then retry. */
            if (tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0,
                                           CONSOLE_CDC_TX_TIMEOUT) != ESP_OK &&
                ++stalled > 5) {
                break;                  /* host is not reading; give up */
            }
            continue;
        }
        stalled = 0;
        off += queued;
        tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, CONSOLE_CDC_TX_TIMEOUT);
    }
    return (int)off;
#else
    console_set_tx_endings(ESP_LINE_ENDINGS_LF);
    size_t written = fwrite(data, 1, len, stdout);
    fflush(stdout);
    console_set_tx_endings(CONSOLE_TX_DEFAULT_ENDINGS);
    return (int)written;
#endif
}

/*
 * Throw away whatever is sitting in PicoRuby's stdin ring buffer. Nobody
 * drains it while the Ruby side is in the UI loop, so bytes that reached
 * it outside a session (the tail of a PicoModem exchange that ended
 * early, leftovers from irb) stay there indefinitely. PicoModem.session
 * reads the oldest byte first, gives up when it is not an STX, and leaves
 * the real frame behind as fresh garbage for the next attempt - once one
 * stray byte is in, every later transfer times out.
 */
static void console_stdin_discard(void)
{
    int guard = 4096;               /* well above PICORB_STDIN_BUFFER_SIZE */
    while (guard-- > 0 && picorb_hal_getchar() >= 0) {
    }
}

/*
 * Runs in the console task, on the STX byte itself. Everything the Ruby
 * side needs must be in place before the ACK goes out, because the host
 * starts sending frames as soon as it sees it.
 */
static void console_modem_enter(void)
{
    s_mode = CONSOLE_MODE_MODEM;

    /* Log lines share the link with the frames; anything printed between
     * here and console_input_modem_exit() would land inside the stream. */
    esp_log_level_set("*", ESP_LOG_NONE);

    /* PicoModem calls STDIN.raw! itself, but frames may already be
     * arriving while the Ruby task is still asleep in its 100 ms poll —
     * and in cooked mode picorb_hal_stdin_push() eats 0x03 and 0x1A as
     * signals, which are perfectly ordinary payload bytes. */
    io_raw_bang(true);

    /* Before the ACK: from here on the buffer holds this session's frames
     * and nothing else. */
    console_stdin_discard();

#if !CONFIG_USB_MIDI_CONSOLE_ON_CDC
    console_set_rx_endings(ESP_LINE_ENDINGS_LF);
#endif

    const uint8_t ack = CONSOLE_ACK;
    console_input_write_raw(&ack, 1);
}

bool console_input_modem_pending(void)
{
    return s_mode == CONSOLE_MODE_MODEM;
}

void console_input_modem_exit(void)
{
    if (s_mode != CONSOLE_MODE_MODEM) {
        return;
    }

#if !CONFIG_USB_MIDI_CONSOLE_ON_CDC
    console_set_rx_endings(CONSOLE_RX_DEFAULT_ENDINGS);
#endif

    s_mode = CONSOLE_MODE_LINE;
    esp_log_level_set("*", (esp_log_level_t)CONFIG_LOG_DEFAULT_LEVEL);

    uint32_t lost = s_raw_lost;
    if (lost) {
        s_raw_lost = 0;
        ESP_LOGW(TAG, "PicoModem: %u bytes dropped (stdin buffer full)",
                 (unsigned)lost);
    }

    console_prompt();
}

/*--------------------------------------------------------------------+
 * irb mode
 *
 * Unlike PicoModem this is plain text, so line endings and logging are
 * left alone: the session is meant to be read by a human, and ESP_LOG
 * lines from the MIDI drivers are part of what one wants to see while
 * poking at devices from the prompt.
 *
 * Terminal mode is left cooked on purpose. In cooked mode
 * picorb_hal_stdin_push() turns 0x03/0x1A into signals, which is how
 * Ctrl-C aborts a running expression and Editor::Line clears its buffer;
 * IO#read_nonblock flips to raw for the duration of each read by itself.
 *--------------------------------------------------------------------*/

void console_input_irb_enter(void)
{
    if (s_mode == CONSOLE_MODE_IRB) {
        return;
    }
    ESP_LOGI(TAG, "irb: console handed over to the Ruby side");
    /* Called from the irb task, the buffer's only reader: stale bytes
     * would otherwise show up as the first keystrokes of the session. */
    console_stdin_discard();
    s_mode = CONSOLE_MODE_IRB;
}

void console_input_irb_exit(void)
{
    if (s_mode != CONSOLE_MODE_IRB) {
        return;
    }

    s_mode = CONSOLE_MODE_LINE;

    uint32_t lost = s_raw_lost;
    if (lost) {
        s_raw_lost = 0;
        ESP_LOGW(TAG, "irb: %u bytes dropped (stdin buffer full)",
                 (unsigned)lost);
    }

    ESP_LOGI(TAG, "irb: console taken back");
    console_prompt();
}

/*--------------------------------------------------------------------+
 * attach mode
 *
 * Queue items start with a kind byte:
 *   'L' a line typed in the attach session (Ruby answers with
 *       console_input_attach_reply(), which prints the next prompt)
 *   'E' "eval <id> <code>" from a machine: framed answer, no prompt
 *   'B' "eval64": the same with the code in Base64 (any length and bytes)
 *
 * Who prints the prompt: Ruby, through console_input_attach_reply(),
 * after it has evaluated a line - only it knows whether the expression is
 * complete. This task prints it only where Ruby will not: on entering the
 * session, and after a Ctrl-C that found nothing being evaluated.
 *--------------------------------------------------------------------*/

static void console_attach_prompt(bool more)
{
    printf(more ? "attach* " : "attach> ");
    fflush(stdout);
}

/* Supervisor state that has a Ruby VM to attach to: a script, or UI mode
 * (also the supervisor's task: RUNNING without a script path). Not irb,
 * which owns the link already, and not a VM on its way out. */
static bool console_attach_possible(void)
{
    if (supervisor_get_state() != SUPERVISOR_STATE_RUNNING) {
        return false;
    }
    const char *script = supervisor_get_current_script();
    return !(script && strcmp(script, SUPERVISOR_IRB_PATH) == 0);
}

/* The queue holds heap strings (kind byte + text): an eval64 can be far
 * longer than a console line. Whoever takes one out frees it. Freeing is
 * not allowed inside the critical section, so the drains below take the
 * pointers out under the lock and free them after it. */
static char *console_attach_item(char kind, const char *text, size_t len)
{
    char *item = (char *)malloc(len + 2);
    if (item == NULL) {
        return NULL;
    }
    item[0] = kind;
    memcpy(item + 1, text, len);
    item[len + 1] = '\0';
    return item;
}

static bool console_attach_send_n(char kind, const char *text, size_t len)
{
    char *item = console_attach_item(kind, text ? text : "", text ? len : 0);
    if (item == NULL) {
        return false;
    }
    if (xQueueSend(s_attach_queue, &item, 0) != pdTRUE) {
        free(item);
        return false;
    }
    s_attach_queued_at = xTaskGetTickCount();
    s_attach_noticed = false;
    return true;
}

static bool console_attach_send(char kind, const char *text)
{
    return console_attach_send_n(kind, text, text ? strlen(text) : 0);
}

/* Inside s_attach_mux: everything waiting, into `out` (CONSOLE_ATTACH_DEPTH) */
static int console_attach_take_all(char **out)
{
    int n = 0;
    while (n < CONSOLE_ATTACH_DEPTH && xQueueReceive(s_attach_queue, &out[n], 0) == pdTRUE) {
        n++;
    }
    return n;
}

static void console_attach_free_all(char **items, int n)
{
    for (int i = 0; i < n; i++) {
        free(items[i]);
    }
}

static void console_attach_enter(void)
{
    if (s_attach_queue == NULL) {
        printf("attach: not available\n");
        return;
    }
    if (!console_attach_possible()) {
        printf("attach: nothing to attach to (irb is running, or a script is stopping)\n");
        return;
    }
    const char *script = supervisor_get_current_script();
    char *old[CONSOLE_ATTACH_DEPTH];
    taskENTER_CRITICAL(&s_attach_mux);
    int n = console_attach_take_all(old);
    s_attach_more = false;
    s_attach_cancel = true;          /* Ruby: drop what an earlier session left */
    taskEXIT_CRITICAL(&s_attach_mux);
    console_attach_free_all(old, n);
    printf("Attached to %s\n", (script && script[0]) ? script : "UI mode");
    printf("Lines run when the script calls UI.process / MIDI.sleep_ms.\n");
    printf("exit / Ctrl-D to detach, Ctrl-C to cancel\n");
    s_mode = CONSOLE_MODE_ATTACH;
    console_attach_prompt(false);
}

/* Back to the command line. `why` is printed when the session did not end
 * by the user's choice. */
static void console_attach_leave(const char *why)
{
    if (s_mode != CONSOLE_MODE_ATTACH) {
        return;
    }
    s_mode = CONSOLE_MODE_LINE;
    char *old[CONSOLE_ATTACH_DEPTH];
    taskENTER_CRITICAL(&s_attach_mux);
    /* Lines nobody evaluated yet go; an eval from a machine is not ours to
     * drop, but nothing sends one during a human session. */
    int n = console_attach_take_all(old);
    s_attach_cancel = true;          /* stop a running line, forget the rest */
    s_attach_more = false;
    taskEXIT_CRITICAL(&s_attach_mux);
    console_attach_free_all(old, n);
    if (why) {
        printf("\n[attach] %s: detached\n", why);
    } else {
        printf("Detached\n");
    }
    console_prompt();
}

static void console_attach_line(const char *line)
{
    /* Leaving needs no answer from Ruby, so it works even when the script
     * never polls. Only at the first line of an expression: inside one,
     * "exit" might be part of the code. */
    if (!s_attach_more && (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0)) {
        console_attach_leave(NULL);
        return;
    }
    if (!s_attach_more && line[0] == '\0') {
        console_attach_prompt(false);
        return;
    }
    if (!console_attach_send('L', line)) {
        printf("attach: busy (%d lines waiting), line dropped\n", CONSOLE_ATTACH_DEPTH);
        console_attach_prompt(s_attach_more);
    }
}

/* Ctrl-C: drop the lines still waiting, stop the one being evaluated
 * (Ruby prints the prompt after it) or else forget the half-typed
 * expression and print the prompt here. */
static void console_attach_interrupt(void)
{
    printf("^C\n");
    bool running;
    char *old[CONSOLE_ATTACH_DEPTH];
    taskENTER_CRITICAL(&s_attach_mux);
    int n = console_attach_take_all(old);
    running = s_attach_running;
    s_attach_cancel = true;
    s_attach_more = false;
    taskEXIT_CRITICAL(&s_attach_mux);
    console_attach_free_all(old, n);
    if (!running) {
        console_attach_prompt(false);
    } else {
        fflush(stdout);
    }
}

/* Called from the console task's loop: tell the user when the script has
 * not picked a line up for a while (it never calls UI.process or
 * MIDI.sleep_ms, or it is stuck), once per line. */
static void console_attach_tick(void)
{
    if (s_mode != CONSOLE_MODE_ATTACH || s_attach_noticed) {
        return;
    }
    if (uxQueueMessagesWaiting(s_attach_queue) == 0) {
        return;
    }
    if (xTaskGetTickCount() - s_attach_queued_at < pdMS_TO_TICKS(CONSOLE_ATTACH_WAIT_MS)) {
        return;
    }
    s_attach_noticed = true;
    printf("\n(waiting: the script has not called UI.process / MIDI.sleep_ms yet;"
           " Ctrl-C to cancel)\n");
    fflush(stdout);
}

/* The id of "eval <id> ..." / "eval64 <id> ...": up to the first space.
 * Returns what follows it (NULL when nothing does). */
static const char *console_eval_id(const char *args, char *id, size_t id_size)
{
    while (*args == ' ') args++;
    const char *sp = strchr(args, ' ');
    size_t idlen = sp ? (size_t)(sp - args) : strlen(args);
    if (idlen >= id_size) idlen = id_size - 1;
    memcpy(id, args, idlen);
    id[idlen] = '\0';
    if (sp == NULL) return NULL;
    while (*sp == ' ') sp++;
    return sp;
}

static bool console_eval_ready(const char *id)
{
    if (s_attach_queue == NULL || !console_attach_possible()) {
        printf("#>eval %s err busy: no Ruby VM to run it (irb, or stopping)\n", id);
        fflush(stdout);
        return false;
    }
    if (s_mode == CONSOLE_MODE_ATTACH) {
        printf("#>eval %s err busy: an attach session is open on the console\n", id);
        fflush(stdout);
        return false;
    }
    return true;
}

/* "eval <id> <code>": one expression, answered in frames
 * ("#<eval <id>", what it prints, "#>eval <id> ok|err ..."). The code is
 * one console line: printable ASCII only. */
static void console_eval_command(const char *args)
{
    char id[32];
    const char *code = console_eval_id(args, id, sizeof(id));
    if (id[0] == '\0' || code == NULL) {
        printf("#>eval %s err usage: eval <id> <code>\n", id);
        fflush(stdout);
        return;
    }
    if (!console_eval_ready(id)) return;
    char buf[CONSOLE_LINE_MAX + 40];
    int n = snprintf(buf, sizeof(buf), "%s %s", id, code);
    if (!console_attach_send_n('E', buf, (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1)) {
        printf("#>eval %s err busy: queue full\n", id);
        fflush(stdout);
    }
}

/*
 * "eval64 <id> <+|.> <base64>": code of any length and any bytes (several
 * lines, UTF-8), as Base64 in pieces that fit a console line. "+" adds a
 * piece and is answered "#>eval64 <id> more"; "." adds the last one and
 * queues the whole for Ruby, which decodes it and answers as "eval" does.
 * One eval64 at a time (a new id drops an unfinished one). The pieces are
 * collected here, in the console task, so they take no room in the queue.
 */
#define CONSOLE_EVAL64_MAX   (24 * 1024)   /* Base64 characters */
static char   *s_e64_buf = NULL;
static size_t  s_e64_len = 0;
static char    s_e64_id[32];

static void console_eval64_drop(void)
{
    free(s_e64_buf);
    s_e64_buf = NULL;
    s_e64_len = 0;
    s_e64_id[0] = '\0';
}

static void console_eval64_command(const char *args)
{
    char id[32];
    const char *rest = console_eval_id(args, id, sizeof(id));
    if (id[0] == '\0' || rest == NULL || (rest[0] != '+' && rest[0] != '.')) {
        printf("#>eval %s err usage: eval64 <id> <+|.> <base64>\n", id);
        fflush(stdout);
        return;
    }
    bool last = rest[0] == '.';
    const char *b64 = rest + 1;
    while (*b64 == ' ') b64++;
    size_t n = strlen(b64);

    if (strcmp(id, s_e64_id) != 0) {
        console_eval64_drop();
        snprintf(s_e64_id, sizeof(s_e64_id), "%s", id);
    }
    if (s_e64_len + n > CONSOLE_EVAL64_MAX) {
        console_eval64_drop();
        printf("#>eval %s err code too long (max %d Base64 characters)\n", id, CONSOLE_EVAL64_MAX);
        fflush(stdout);
        return;
    }
    char *grown = (char *)realloc(s_e64_buf, s_e64_len + n + 1);
    if (grown == NULL) {
        console_eval64_drop();
        printf("#>eval %s err out of memory\n", id);
        fflush(stdout);
        return;
    }
    s_e64_buf = grown;
    memcpy(s_e64_buf + s_e64_len, b64, n);
    s_e64_len += n;
    s_e64_buf[s_e64_len] = '\0';

    if (!last) {
        printf("#>eval64 %s more\n", id);
        fflush(stdout);
        return;
    }
    if (console_eval_ready(id)) {
        /* "<id> <base64>" */
        size_t idlen = strlen(id);
        char *item = (char *)malloc(idlen + 1 + s_e64_len + 2);
        bool sent = false;
        if (item != NULL) {
            item[0] = 'B';
            memcpy(item + 1, id, idlen);
            item[1 + idlen] = ' ';
            memcpy(item + 2 + idlen, s_e64_buf, s_e64_len + 1);
            if (xQueueSend(s_attach_queue, &item, 0) == pdTRUE) {
                sent = true;
            } else {
                free(item);
            }
        }
        if (!sent) {
            printf("#>eval %s err busy: queue full\n", id);
            fflush(stdout);
        }
    }
    console_eval64_drop();
}

char *console_input_attach_pop(void)
{
    if (s_attach_queue == NULL) {
        return NULL;
    }
    char *item = NULL;
    taskENTER_CRITICAL(&s_attach_mux);
    /* Inside the lock: a Ctrl-C either drops this line or sees it running */
    if (xQueueReceive(s_attach_queue, &item, 0) == pdTRUE) {
        if (item[0] == 'L') {
            s_attach_running = true;
        }
    } else {
        item = NULL;
    }
    taskEXIT_CRITICAL(&s_attach_mux);
    return item;
}

void console_input_attach_reply(bool more)
{
    taskENTER_CRITICAL(&s_attach_mux);
    s_attach_running = false;
    s_attach_more = more;
    taskEXIT_CRITICAL(&s_attach_mux);
    if (s_mode == CONSOLE_MODE_ATTACH) {
        console_attach_prompt(more);
    }
}

bool console_input_attach_take_cancel(void)
{
    taskENTER_CRITICAL(&s_attach_mux);
    bool c = s_attach_cancel;
    s_attach_cancel = false;
    taskEXIT_CRITICAL(&s_attach_mux);
    return c;
}

bool console_input_attached(void)
{
    return s_mode == CONSOLE_MODE_ATTACH;
}

void console_input_attach_exit(const char *why)
{
    taskENTER_CRITICAL(&s_attach_mux);
    s_attach_running = false;
    taskEXIT_CRITICAL(&s_attach_mux);
    console_attach_leave(why);
    /* A machine's evals die with the VM too */
    if (s_attach_queue) {
        char *old[CONSOLE_ATTACH_DEPTH];
        taskENTER_CRITICAL(&s_attach_mux);
        int n = console_attach_take_all(old);
        taskEXIT_CRITICAL(&s_attach_mux);
        console_attach_free_all(old, n);
    }
}

/*--------------------------------------------------------------------+
 * Command handling
 *--------------------------------------------------------------------*/

static void console_queue_script(const char *path)
{
    char cmd[CONSOLE_CMD_MAX];
    snprintf(cmd, sizeof(cmd), "%s", path);

    if (xQueueSend(s_cmd_queue, cmd, 0) != pdTRUE) {
        printf("Command queue full, dropped: %s\n", cmd);
        fflush(stdout);
        ESP_LOGW(TAG, "command queue full, dropped: %s", cmd);
    }
}

/* The board as the web simulator names it (sim/web/device.js BOARDS) */
#if CONFIG_USB_MIDI_BOARD_M5STACK_TAB5
#define CONSOLE_BOARD_ID "tab5"
#elif CONFIG_USB_MIDI_BOARD_M5STACK_CORES3
#define CONSOLE_BOARD_ID "cores3"
#elif CONFIG_USB_MIDI_BOARD_ELECROW_CROWPANEL
#define CONSOLE_BOARD_ID "crowpanel"
#else
#define CONSOLE_BOARD_ID "freenove"
#endif

/* A command the Ruby side answers (it needs the PicoRuby VFS). They share
 * the queue with "load"'s paths, so they start with a byte no path does.
 * Only UI mode reads the queue: while a script or irb runs they are
 * refused here rather than left waiting. Answers are framed for machines
 * (the web simulator) and name their target: "#<name arg", lines,
 * "#>name arg ok|err ..." */
#define CONSOLE_RUBY_CMD '\x01'

static void console_queue_ruby(const char *name, const char *arg)
{
    /* UI mode is the supervisor's task too (RUNNING, no script path) */
    if (supervisor_is_script_running() || supervisor_get_state() == SUPERVISOR_STATE_STOPPING) {
        printf("#>%s %s err busy: a script is running (stop it first)\n", name, arg);
        fflush(stdout);
        return;
    }
    char cmd[CONSOLE_CMD_MAX];
    int n = snprintf(cmd, sizeof(cmd), "%c%s %s", CONSOLE_RUBY_CMD, name, arg);
    if (n < 0 || n >= (int)sizeof(cmd)) {
        printf("#>%s %s err path too long (max %d)\n", name, arg, CONSOLE_CMD_MAX - 8);
        fflush(stdout);
        return;
    }
    console_queue_script(cmd);
}

/* Runs in the console task. Anything that needs the PicoRuby VFS (i.e.
 * actually loading a script) is queued for the Ruby side instead. */
static void console_execute(const char *line)
{
    if (line[0] == '\0') {
        return;                     /* bare Enter */
    }

    /* One line about the device for the web simulator: which board, and
     * whether UI mode (which answers ls / rm and file transfers) is up.
     * proto: 2 = ls / rm answers name their target ("#>ls /sd ok 3"),
     * 3 = mv, 4 = eval (and attach), 5 = eval64 / evalstop */
    if (strcmp(line, "info") == 0) {
        const char *script = supervisor_get_current_script();
        const char *mode = supervisor_get_state() == SUPERVISOR_STATE_STOPPING ? "stopping"
                         : !supervisor_is_script_running() ? "ui"
                         : strcmp(script, SUPERVISOR_IRB_PATH) == 0 ? "irb" : "script";
        printf("#info proto=5 board=%s mode=%s script=%s\n", CONSOLE_BOARD_ID, mode,
               (script && strcmp(mode, "script") == 0) ? script : "");
        fflush(stdout);
        return;
    }

    if (strcmp(line, "ls") == 0 || strncmp(line, "ls ", 3) == 0) {
        const char *dir = line[2] ? line + 3 : "/";
        while (*dir == ' ') dir++;
        console_queue_ruby("ls", *dir ? dir : "/");
        return;
    }

    if (strncmp(line, "rm ", 3) == 0) {
        const char *path = line + 3;
        while (*path == ' ') path++;
        if (*path == '\0') {
            printf("#>rm  err usage: rm <path>\n");
            fflush(stdout);
            return;
        }
        console_queue_ruby("rm", path);
        return;
    }

    if (strncmp(line, "mv ", 3) == 0) {
        const char *args = line + 3;
        while (*args == ' ') args++;
        if (strchr(args, ' ') == NULL) {
            printf("#>mv %s err usage: mv <from> <to>\n", args);
            fflush(stdout);
            return;
        }
        console_queue_ruby("mv", args);
        return;
    }

    /* The Scripts screen's reload: UI mode reads the card again and
     * hands the new list to C */
    if (strcmp(line, "rescan") == 0) {
        picoruby_esp32_request_sd_refresh();
        printf("#>rescan ok\n");
        fflush(stdout);
        return;
    }

    if (strncmp(line, "load ", 5) == 0) {
        const char *path = line + 5;
        while (*path == ' ') path++;
        if (*path == '\0') {
            printf("Usage: load /sd/app.rb\n");
            fflush(stdout);
            return;
        }
        if (strlen(path) >= CONSOLE_CMD_MAX) {
            printf("Path too long (max %d)\n", CONSOLE_CMD_MAX - 1);
            fflush(stdout);
            return;
        }
        ESP_LOGI(TAG, "Console: load %s", path);
        console_queue_script(path);
        return;
    }

    if (strcmp(line, "attach") == 0) {
        console_attach_enter();
        fflush(stdout);
        return;
    }

    if (strncmp(line, "eval ", 5) == 0) {
        console_eval_command(line + 5);
        return;
    }

    if (strncmp(line, "eval64 ", 7) == 0) {
        console_eval64_command(line + 7);
        return;
    }

    /* A machine's Ctrl-C: drop the evals still queued and stop the one
     * running (it answers "#>eval <id> err stopped") */
    if (strcmp(line, "evalstop") == 0) {
        if (s_attach_queue != NULL && s_mode != CONSOLE_MODE_ATTACH) {
            char *old[CONSOLE_ATTACH_DEPTH];
            taskENTER_CRITICAL(&s_attach_mux);
            int n = console_attach_take_all(old);
            s_attach_cancel = true;
            taskEXIT_CRITICAL(&s_attach_mux);
            console_attach_free_all(old, n);
        }
        console_eval64_drop();
        printf("#>evalstop ok\n");
        fflush(stdout);
        return;
    }

    /* Like "stop", handled in C: the supervisor starts irb as a PicoRuby
     * task of its own, so it works whether the Ruby side is sitting in the
     * UI loop or busy running a script. The console stays in line mode
     * until the session itself calls console_input_irb_enter(). */
    if (strcmp(line, "irb") == 0) {
        if (supervisor_request_irb()) {
            printf("Starting irb...\n");
        } else {
            printf("Failed to start irb\n");
        }
        fflush(stdout);
        return;
    }

    /* Unlike "load", this one is handled entirely in C: while a script is
     * running the Ruby side never polls the command queue, so a queued
     * command would sit there until the script ended by itself. */
    if (strcmp(line, "stop") == 0) {
        if (supervisor_stop_script()) {
            printf("Stopping script...\n");
        } else {
            printf("No script running\n");
        }
        fflush(stdout);
        return;
    }

    if (strcmp(line, "heap") == 0) {
        printf("Free heap: %u bytes\n", (unsigned)esp_get_free_heap_size());
        fflush(stdout);
        return;
    }

    if (strcmp(line, "restart") == 0) {
        printf("Restarting ESP32...\n");
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_restart();
        return;
    }

    if (strcmp(line, "help") == 0 || strcmp(line, "?") == 0) {
        console_help();
        return;
    }

    printf("Unknown command: %s\n", line);
    console_help();
}

/*--------------------------------------------------------------------+
 * Line editor
 *--------------------------------------------------------------------*/

static void console_feed(console_line_t *st, uint8_t c)
{
    /* The PicoRuby web terminal opens a transfer with a bare STX, the same
     * handshake R2P2's shell uses. Whatever was half-typed is dropped. */
    if (c == CONSOLE_STX && s_mode == CONSOLE_MODE_LINE) {
        st->len = 0;
        st->esc = 0;
        st->last_was_cr = false;
        console_modem_enter();
        return;
    }

    /* Swallow ANSI escape sequences (arrow keys, function keys) so they
     * do not end up as "[A" in the command line. */
    if (st->esc == 1) {
        st->esc = (c == '[' || c == 'O') ? 2 : 0;
        return;
    }
    if (st->esc == 2) {
        if (c >= 0x40 && c <= 0x7E) st->esc = 0;   /* final byte of CSI */
        return;
    }
    if (c == 0x1B) {
        st->esc = 1;
        return;
    }

    bool was_cr = st->last_was_cr;
    st->last_was_cr = (c == '\r');

    if (c == '\n' && was_cr) {
        return;                     /* second half of a CRLF */
    }

    if (c == '\r' || c == '\n') {
        printf("\n");
        fflush(stdout);
        st->buf[st->len] = '\0';
        st->len = 0;
        if (s_mode == CONSOLE_MODE_ATTACH) {
            console_attach_line(st->buf);
            return;
        }
        console_execute(st->buf);
        /* "attach" printed its own prompt */
        if (s_mode == CONSOLE_MODE_LINE) {
            console_prompt();
        }
        return;
    }

    if (c == 0x08 || c == 0x7F) {   /* BS / DEL */
        if (st->len > 0) {
            st->len--;
            printf("\b \b");
            fflush(stdout);
        }
        return;
    }

    if (s_mode == CONSOLE_MODE_ATTACH) {
        if (c == 0x03) {            /* Ctrl-C: cancel (see the function) */
            st->len = 0;
            console_attach_interrupt();
            return;
        }
        if (c == 0x04 && st->len == 0) {   /* Ctrl-D on an empty line */
            printf("\n");
            console_attach_leave(NULL);
            return;
        }
    }

    if (c == 0x03 || c == 0x15) {   /* Ctrl-C / Ctrl-U: discard the line */
        st->len = 0;
        printf("\n");
        console_prompt();
        return;
    }

    if (c >= 0x20 && c < 0x7F) {    /* printable */
        if (st->len < CONSOLE_LINE_MAX - 1) {
            st->buf[st->len++] = (char)c;
            putchar((int)c);
            fflush(stdout);
        }
        /* line full: ignore silently, Enter still submits what was typed */
    }
}

/*
 * Outside the normal command line the bytes belong to the Ruby side - to
 * the PicoModem protocol or to irb's line editor - so they go into
 * PicoRuby's stdin ring buffer, where STDIN.read_nonblock picks them up.
 */
static void console_dispatch(console_line_t *st, uint8_t c)
{
    if (s_mode == CONSOLE_MODE_MODEM || s_mode == CONSOLE_MODE_IRB) {
        if (!picorb_hal_stdin_push(c)) {
            s_raw_lost++;
        }
        return;
    }
    console_feed(st, c);
}

/*--------------------------------------------------------------------+
 * CDC receive hook (TinyUSB task context)
 *--------------------------------------------------------------------*/

#if CONFIG_USB_MIDI_CONSOLE_ON_CDC
static void console_cdc_rx_cb(const uint8_t *data, size_t len, void *arg)
{
    (void)arg;
    if (s_rx_stream == NULL || len == 0) return;

    /* Copy and return: this runs on the task that services USB, so it must
     * never block. Overflow is counted and reported by the console task. */
    size_t sent = xStreamBufferSend(s_rx_stream, data, len, 0);
    if (sent < len) {
        s_rx_dropped += (uint32_t)(len - sent);
    }
}
#endif

/*--------------------------------------------------------------------+
 * Console task
 *--------------------------------------------------------------------*/

static void console_input_task(void *arg)
{
    (void)arg;
    console_line_t st = { .len = 0, .last_was_cr = false, .esc = 0 };
    uint8_t buf[64];

    ESP_LOGI(TAG, "Console input task running (%s)",
             s_cdc_mode ? "CDC callback" : "stdin polling");

    for (;;) {
        if (s_cdc_mode) {
            size_t n = xStreamBufferReceive(s_rx_stream, buf, sizeof(buf),
                                            pdMS_TO_TICKS(CONSOLE_TICK_MS));
            for (size_t i = 0; i < n; i++) {
                console_dispatch(&st, buf[i]);
            }
        } else if (s_mode == CONSOLE_MODE_IRB) {
            /* picoruby-machine runs its own stdin_reader task polling this
             * very link, and it fills the ring buffer irb reads from. Two
             * pollers would split the keystrokes between them, so stand
             * back for the duration of the session. (PicoModem keeps
             * pumping: it answers the STX handshake from here and cannot
             * assume the Ruby task is awake yet.) */
            vTaskDelay(pdMS_TO_TICKS(CONSOLE_POLL_INTERVAL_MS));
        } else {
            /* UART / USB-Serial-JTAG: no RX callback available, so drain
             * stdin here. Being a task of its own, a short interval costs
             * nothing and keeps typing responsive. */
            int c;
            int guard = 0;
            while (guard++ < (int)sizeof(buf) * 8) {
                c = getchar();
                if (c == EOF || c < 0) break;
                console_dispatch(&st, (uint8_t)c);
            }
            vTaskDelay(pdMS_TO_TICKS(CONSOLE_POLL_INTERVAL_MS));
        }

        console_attach_tick();

        uint32_t dropped = s_rx_dropped;
        if (dropped) {
            s_rx_dropped = 0;
            ESP_LOGW(TAG, "console RX overflow: %u bytes dropped",
                     (unsigned)dropped);
        }
    }
}

/*--------------------------------------------------------------------+
 * Public API
 *--------------------------------------------------------------------*/

void console_input_start(void)
{
    if (s_task != NULL) {
        return;
    }

    s_cmd_queue = xQueueCreate(CONSOLE_CMD_QUEUE_DEPTH, CONSOLE_CMD_MAX);
    if (s_cmd_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create command queue");
        return;
    }
    /* Optional: without it "attach" / "eval" say they are unavailable */
    s_attach_queue = xQueueCreate(CONSOLE_ATTACH_DEPTH, sizeof(char *));
    if (s_attach_queue == NULL) {
        ESP_LOGW(TAG, "Failed to create attach queue");
    }

#if CONFIG_USB_MIDI_CONSOLE_ON_CDC
    s_rx_stream = xStreamBufferCreate(CONSOLE_RX_STREAM_SIZE, 1);
    if (s_rx_stream == NULL) {
        ESP_LOGE(TAG, "Failed to create RX stream buffer");
        vQueueDelete(s_cmd_queue);
        s_cmd_queue = NULL;
        return;
    }
    /* Must be registered: the gem's CDC handler drains the FIFO either
     * way, so without this hook every keystroke is discarded. */
    if (USB_MIDI_DEVICE_set_cdc_rx_callback(console_cdc_rx_cb, NULL) == 0) {
        s_cdc_mode = true;
        /* tinyusb_console_init() pointed stdin at the CDC as well, and
         * picoruby-machine's stdin_reader task polls it with fgetc() from
         * whichever core it lands on. Whenever it beats the gem's RX
         * handler to a packet, those bytes skip the console task and go
         * straight into PicoRuby's stdin buffer: lost keystrokes in line
         * mode, stray bytes ahead of the next PicoModem frame. The hook
         * above sees every byte, so nobody else needs stdin. */
        if (freopen("/dev/null", "r", stdin) == NULL) {
            ESP_LOGW(TAG, "Failed to detach stdin from the CDC");
        }
    } else {
        ESP_LOGW(TAG, "CDC RX hook unavailable, falling back to stdin polling");
        vStreamBufferDelete(s_rx_stream);
        s_rx_stream = NULL;
    }
#endif

    BaseType_t ret = xTaskCreatePinnedToCore(
        console_input_task,
        "console_in",
        CONSOLE_TASK_STACK_SIZE,
        NULL,
        CONSOLE_TASK_PRIORITY,
        &s_task,
        CONSOLE_TASK_CORE);

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create console task");
        s_task = NULL;
        return;
    }

    ESP_LOGI(TAG, "Console input started (core %d, prio %d, source: %s)",
             CONSOLE_TASK_CORE, CONSOLE_TASK_PRIORITY,
             s_cdc_mode ? "USB CDC callback" : "stdin");
}

bool console_input_pop_command(char *out, size_t out_size)
{
    if (s_cmd_queue == NULL || out == NULL || out_size == 0) {
        return false;
    }

    char cmd[CONSOLE_CMD_MAX];
    if (xQueueReceive(s_cmd_queue, cmd, 0) != pdTRUE) {
        return false;
    }

    cmd[CONSOLE_CMD_MAX - 1] = '\0';
    snprintf(out, out_size, "%s", cmd);
    return true;
}
