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
 */

#include <stdio.h>
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
    printf("  stop             - Stop the running script (back to UI mode)\n");
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

/* Runs in the console task. Anything that needs the PicoRuby VFS (i.e.
 * actually loading a script) is queued for the Ruby side instead. */
static void console_execute(const char *line)
{
    if (line[0] == '\0') {
        return;                     /* bare Enter */
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
    if (c == CONSOLE_STX) {
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
        console_execute(st->buf);
        st->len = 0;
        console_prompt();
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
    if (s_mode != CONSOLE_MODE_LINE) {
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
                                            portMAX_DELAY);
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
