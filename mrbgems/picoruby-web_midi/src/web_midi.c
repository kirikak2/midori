/*
 * picoruby-web_midi: Web MIDI ports as picoruby-midi transports.
 * See include/web_midi.h. PicoRuby.wasm (Emscripten) only.
 */
#include <string.h>
#include <stdlib.h>

#include <mruby.h>
#include <mruby/presym.h>
#include <mruby/string.h>

#include "../include/web_midi.h"
#include "../../picoruby-midi/include/midi_transport.h"
#include "../../picoruby-midi/include/midi.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

EM_JS(void, web_midi_js_out, (int slot, int cin, int b1, int b2, int b3, double t), {
  if (Module.webMidiOut) Module.webMidiOut(slot, cin, b1, b2, b3, t);
});
EM_JS(double, web_midi_js_now_us, (void), {
  return performance.now() * 1000;
});
#define KEEP EMSCRIPTEN_KEEPALIVE
#else
static void web_midi_js_out(int s, int c, int b1, int b2, int b3, double t)
{ (void)s; (void)c; (void)b1; (void)b2; (void)b3; (void)t; }
static double web_midi_js_now_us(void) { return 0; }
#define KEEP
#endif

#define RING 1024   /* bytes per port, a power of two */

typedef struct {
  char name[64];
  bool used, has_input, has_output, connected;
  uint16_t bit;
  midi_transport_t transport;
  uint8_t rx[RING];
  uint32_t head, tail;
} port_t;

static port_t s_ports[WEB_MIDI_MAX_PORTS];
static int s_count;

static port_t *port_at(int slot)
{
  return (slot >= 0 && slot < WEB_MIDI_MAX_PORTS && s_ports[slot].used) ? &s_ports[slot] : NULL;
}

/* ---- transport ops (ctx = port_t*) */

static int op_send(void *ctx, uint8_t cable, uint8_t cin, uint8_t b1, uint8_t b2, uint8_t b3)
{
  (void)cable;
  port_t *p = ctx;
  if (!p->connected || !p->has_output) return -1;
  web_midi_js_out((int)(p - s_ports), cin & 0x0F, b1, b2, b3, web_midi_js_now_us());
  return 0;
}

static int op_available(void *ctx)
{
  port_t *p = ctx;
  return (int)(p->head - p->tail);
}

static int op_read(void *ctx, uint8_t *buf, size_t maxlen)
{
  port_t *p = ctx;
  int n = 0;
  while ((size_t)n + 4 <= maxlen && p->head - p->tail >= 4) {
    for (int i = 0; i < 4; i++) buf[n + i] = p->rx[(p->tail + i) & (RING - 1)];
    p->tail += 4;
    n += 4;
  }
  return n;
}

static bool op_connected(void *ctx)
{
  return ((port_t *)ctx)->connected;
}

static const midi_transport_ops_t s_ops = {
  op_send, op_read, op_available, op_connected, MIDI_TRANSPORT_ID_USB
};

/* ---- page side */

KEEP void web_midi_port(int slot, const char *name, int has_input, int has_output)
{
  if (slot < 0 || slot >= WEB_MIDI_MAX_PORTS) return;
  port_t *p = &s_ports[slot];
  if (!p->used) {
    p->used = true;
    p->transport.ops = &s_ops;
    p->transport.ctx = p;
    if (slot + 1 > s_count) s_count = slot + 1;
  }
  strncpy(p->name, name ? name : "", sizeof(p->name) - 1);
  p->has_input = has_input != 0;
  p->has_output = has_output != 0;
}

KEEP void web_midi_connected(int slot, int connected)
{
  port_t *p = port_at(slot);
  if (!p) return;
  p->connected = connected != 0;
  if (!p->connected) p->head = p->tail = 0;
}

KEEP void web_midi_in(int slot, int b0, int b1, int b2, int b3)
{
  port_t *p = port_at(slot);
  if (!p || !p->connected || p->head - p->tail + 4 > RING) return;
  uint8_t pkt[4] = { (uint8_t)b0, (uint8_t)b1, (uint8_t)b2, (uint8_t)b3 };
  for (int i = 0; i < 4; i++) p->rx[(p->head + i) & (RING - 1)] = pkt[i];
  p->head += 4;
  MIDI_Input_kick();
}

/* ---- VM side */

int WEB_MIDI_count(void) { return s_count; }
const char *WEB_MIDI_name(int slot) { port_t *p = port_at(slot); return p ? p->name : NULL; }
bool WEB_MIDI_has_input(int slot) { port_t *p = port_at(slot); return p && p->has_input; }
bool WEB_MIDI_has_output(int slot) { port_t *p = port_at(slot); return p && p->has_output; }
bool WEB_MIDI_is_connected(int slot) { port_t *p = port_at(slot); return p && p->connected; }

uint16_t WEB_MIDI_bit(int slot)
{
  port_t *p = port_at(slot);
  if (!p) return 0;
  if (p->bit == 0) {
    p->bit = MIDI_transport_register(&p->transport);
    /* Its input is read by picoruby-midi's input task, along the routes */
    if (p->bit != 0 && p->has_input) MIDI_Input_add_packet_source(p->bit);
  }
  return p->bit;
}

/* ---- Ruby: WebMIDI._count etc. (mrblib/web_midi.rb wraps them) */

static mrb_value m_count(mrb_state *mrb, mrb_value self)
{
  (void)mrb; (void)self;
  return mrb_fixnum_value(WEB_MIDI_count());
}

static mrb_value m_name(mrb_state *mrb, mrb_value self)
{
  mrb_int slot;
  mrb_get_args(mrb, "i", &slot);
  const char *n = WEB_MIDI_name((int)slot);
  return n ? mrb_str_new_cstr(mrb, n) : mrb_nil_value();
}

static mrb_value m_has_input(mrb_state *mrb, mrb_value self)
{
  mrb_int slot;
  mrb_get_args(mrb, "i", &slot);
  return mrb_bool_value(WEB_MIDI_has_input((int)slot));
}

static mrb_value m_has_output(mrb_state *mrb, mrb_value self)
{
  mrb_int slot;
  mrb_get_args(mrb, "i", &slot);
  return mrb_bool_value(WEB_MIDI_has_output((int)slot));
}

static mrb_value m_connected(mrb_state *mrb, mrb_value self)
{
  mrb_int slot;
  mrb_get_args(mrb, "i", &slot);
  return mrb_bool_value(WEB_MIDI_is_connected((int)slot));
}

/* WebMIDI._key(slot): a short stable key for the port's name ("w" + FNV-1a
 * 32 in hex): the MIDI Devices list keeps keys of a few characters only, and
 * names such as "IAC Driver Bus 1" / "... 2" share their start */
static mrb_value m_key(mrb_state *mrb, mrb_value self)
{
  mrb_int slot;
  mrb_get_args(mrb, "i", &slot);
  const char *n = WEB_MIDI_name((int)slot);
  if (!n) return mrb_nil_value();
  uint32_t h = 2166136261u;
  for (const unsigned char *c = (const unsigned char *)n; *c; c++) { h ^= *c; h *= 16777619u; }
  char buf[12];
  static const char hex[] = "0123456789abcdef";
  buf[0] = 'w';
  for (int i = 0; i < 8; i++) buf[1 + i] = hex[(h >> (28 - 4 * i)) & 0xF];
  buf[9] = 0;
  return mrb_str_new_cstr(mrb, buf);
}

static mrb_value m_bit(mrb_state *mrb, mrb_value self)
{
  mrb_int slot;
  mrb_get_args(mrb, "i", &slot);
  return mrb_fixnum_value(WEB_MIDI_bit((int)slot));
}

static mrb_value m_send_packet(mrb_state *mrb, mrb_value self)
{
  mrb_int slot, cable, cin, b1, b2, b3;
  mrb_get_args(mrb, "iiiiii", &slot, &cable, &cin, &b1, &b2, &b3);
  port_t *p = port_at((int)slot);
  if (!p) return mrb_fixnum_value(-1);
  return mrb_fixnum_value(op_send(p, (uint8_t)cable, (uint8_t)cin, (uint8_t)b1, (uint8_t)b2, (uint8_t)b3));
}

static mrb_value m_bytes_available(mrb_state *mrb, mrb_value self)
{
  mrb_int slot;
  mrb_get_args(mrb, "i", &slot);
  port_t *p = port_at((int)slot);
  return mrb_fixnum_value(p ? op_available(p) : 0);
}

static mrb_value m_read_available(mrb_state *mrb, mrb_value self)
{
  mrb_int slot;
  mrb_get_args(mrb, "i", &slot);
  port_t *p = port_at((int)slot);
  if (!p || op_available(p) == 0) return mrb_nil_value();
  uint8_t buf[RING];
  int n = op_read(p, buf, sizeof(buf));
  return n > 0 ? mrb_str_new(mrb, (const char *)buf, n) : mrb_nil_value();
}

void mrb_picoruby_web_midi_gem_init(mrb_state *mrb)
{
  struct RClass *c = mrb_define_module_id(mrb, MRB_SYM(WebMIDI));
  mrb_define_module_function_id(mrb, c, MRB_SYM(_count), m_count, MRB_ARGS_NONE());
  mrb_define_module_function_id(mrb, c, MRB_SYM(_name), m_name, MRB_ARGS_REQ(1));
  mrb_define_module_function_id(mrb, c, MRB_SYM(_has_input), m_has_input, MRB_ARGS_REQ(1));
  mrb_define_module_function_id(mrb, c, MRB_SYM(_has_output), m_has_output, MRB_ARGS_REQ(1));
  mrb_define_module_function_id(mrb, c, MRB_SYM(_connected), m_connected, MRB_ARGS_REQ(1));
  mrb_define_module_function_id(mrb, c, MRB_SYM(_bit), m_bit, MRB_ARGS_REQ(1));
  mrb_define_module_function_id(mrb, c, MRB_SYM(_key), m_key, MRB_ARGS_REQ(1));
  mrb_define_module_function_id(mrb, c, MRB_SYM(_send_packet), m_send_packet, MRB_ARGS_REQ(6));
  mrb_define_module_function_id(mrb, c, MRB_SYM(_bytes_available), m_bytes_available, MRB_ARGS_REQ(1));
  mrb_define_module_function_id(mrb, c, MRB_SYM(_read_available), m_read_available, MRB_ARGS_REQ(1));
}

void mrb_picoruby_web_midi_gem_final(mrb_state *mrb)
{
  (void)mrb;
}
