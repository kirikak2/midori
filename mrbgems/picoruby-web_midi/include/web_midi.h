/*
 * picoruby-web_midi: the page's Web MIDI ports as picoruby-midi transports.
 *
 * PicoRuby.wasm only. The page owns the MIDIAccess; it tells the VM which
 * ports there are (web_midi_port), whether each is open
 * (web_midi_connected) and what arrives (web_midi_in). Sending calls
 * Module.webMidiOut(slot, cin, b1, b2, b3, timeUs) in the page.
 *
 * A port is one name: Web MIDI lists inputs and outputs apart, and the page
 * pairs those with the same name into one slot.
 */
#ifndef PICORUBY_WEB_MIDI_H_
#define PICORUBY_WEB_MIDI_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WEB_MIDI_MAX_PORTS 8

/* Page side (exported to JavaScript) */
void web_midi_port(int slot, const char *name, int has_input, int has_output);
void web_midi_connected(int slot, int connected);
void web_midi_in(int slot, int b0, int b1, int b2, int b3);

/* VM side */
int         WEB_MIDI_count(void);
const char *WEB_MIDI_name(int slot);
bool        WEB_MIDI_has_input(int slot);
bool        WEB_MIDI_has_output(int slot);
bool        WEB_MIDI_is_connected(int slot);
/* The port's bit in picoruby-midi's transport registry (registers it the
 * first time), or 0 when the registry is full */
uint16_t    WEB_MIDI_bit(int slot);

#ifdef __cplusplus
}
#endif

#endif
