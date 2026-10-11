/*
 * Midori serial console input
 *
 * Accepts console bytes from the serial link (USB CDC, UART or
 * USB-Serial/JTAG depending on the USB port mode), does the line editing
 * and echo in its own task, and hands completed commands to the Ruby side.
 *
 * See console_input.c for the data flow and the reason it is a separate
 * task rather than a poll from the Ruby VM task.
 */

#ifndef MIDORI_CONSOLE_INPUT_H
#define MIDORI_CONSOLE_INPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Create the console task and, in USB-MIDI-Device mode, register the CDC
 * receive hook with picoruby-usb_midi_device. Call it after
 * USB_MIDI_DEVICE_start(); calling it twice is a no-op.
 */
void console_input_start(void);

/*
 * Pop one script path requested from the console ("load /sd/app.rb").
 * Non-blocking. Returns true and fills `out` when a command was queued,
 * false when the queue is empty.
 */
bool console_input_pop_command(char *out, size_t out_size);

/*--------------------------------------------------------------------+
 * PicoModem (binary file transfer) mode
 *
 * The host — the PicoRuby web terminal — starts a transfer by sending a
 * bare STX (0x02), the same handshake R2P2's shell uses. The console task
 * answers with ACK (0x06), stops echoing and line editing, and from then
 * on forwards every byte to PicoRuby's stdin ring buffer so that
 * PicoModem.session can read frames through STDIN.read_nonblock.
 *
 * The session itself has to run on the Ruby side: only PicoRuby's VFS can
 * reach /sd (see docs/PICOMODEM.md).
 *--------------------------------------------------------------------*/

/*
 * True once the host asked for a PicoModem session and the Ruby side has
 * not called console_input_modem_exit() yet.
 */
bool console_input_modem_pending(void);

/*
 * Write bytes to the console link untouched — no LF -> CRLF translation,
 * no NUL termination. Frames must go through this rather than $stdout,
 * whose VFS would corrupt them. Returns the number of bytes written.
 */
int console_input_write_raw(const uint8_t *data, size_t len);

/*
 * End a PicoModem session: resume echo and line editing, restore logging
 * and print a fresh prompt.
 */
void console_input_modem_exit(void);

/*--------------------------------------------------------------------+
 * irb (interactive Ruby) mode
 *
 * While irb runs, the line editing lives on the Ruby side (Editor::Line
 * inside picoruby-shell), so the console task must get out of the way:
 * it stops echoing and, in CDC mode, forwards every byte to PicoRuby's
 * stdin ring buffer instead. In the other USB port modes it stops reading
 * the link altogether and leaves it to picoruby-machine's stdin_reader
 * task, which feeds the same ring buffer.
 *
 * The irb session itself is started by the supervisor as its own PicoRuby
 * task (see supervisor_request_irb) - see docs/IRB.md.
 *--------------------------------------------------------------------*/

/* Hand the link to the Ruby side. Called from Ruby once irb is ready. */
void console_input_irb_enter(void);

/*
 * Take the link back: resume echo and line editing and print a fresh
 * prompt. Safe to call when no irb session is active; the supervisor
 * calls it whenever it starts a non-irb task, so a crashed or force-
 * stopped session cannot leave the console mute.
 */
void console_input_irb_exit(void);

#ifdef __cplusplus
}
#endif

#endif /* MIDORI_CONSOLE_INPUT_H */
