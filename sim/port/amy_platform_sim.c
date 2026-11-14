// AMY in the simulator: the platform layer AMY's engine expects (what
// i2s.c is on the device, libminiaudio-audio.c on a desktop), and the sink
// picoruby-amy's port hands its blocks to (AMY_GEM_POLLED).
//
// Nothing renders on its own: the page asks for blocks with
// AMY_GEM_render(n) as its AudioWorklet's queue drains, and each block goes
// to Module.amyOut(ptr, frames) (sim/web/amy_audio.js), which copies it out
// of the wasm heap at once.
#include <stdint.h>
#include <stddef.h>
#include <emscripten.h>
#include "amy.h"
#include "amy_gem.h"

void amy_update_tasks(void) {}
void amy_platform_init(void) {}
void amy_platform_deinit(void) {}
size_t amy_i2s_write(const uint8_t *buffer, size_t nbytes) { (void)buffer; (void)nbytes; return 0; }
int16_t *amy_render_audio(void) { return amy_simple_fill_buffer(); }
uint32_t amy_audio_device_count(uint8_t dir) { (void)dir; return 0; }
uint32_t amy_audio_device_name(uint8_t dir, uint32_t index, char *buf, uint32_t buflen)
{
  (void)dir; (void)index;
  if (buflen) buf[0] = 0;
  return 0;
}

// AMY's web build is for an AudioWorklet thread (WASM workers); here it all
// runs on the page's one thread, so its locks have nothing to wait for and
// the web control-voltage inputs stay at 0.
#include <emscripten/wasm_worker.h>
void emscripten_lock_init(emscripten_lock_t *lock) { *lock = 0; }
bool emscripten_lock_busyspin_wait_acquire(emscripten_lock_t *lock, double max_ms) { (void)max_ms; *lock = 1; return true; }
void emscripten_lock_release(emscripten_lock_t *lock) { *lock = 0; }
float amy_web_cv_1 = 0;
float amy_web_cv_2 = 0;

EM_JS(void, sim_js_amy_out, (const int16_t *samples, int frames), {
  if (Module.amyOut) Module.amyOut(samples, frames);
});

void amy_gem_polled_sink(const int16_t *samples, size_t frames)
{
  sim_js_amy_out(samples, (int)frames);
}

EMSCRIPTEN_KEEPALIVE int sim_amy_render(int blocks) { return AMY_GEM_render(blocks); }
EMSCRIPTEN_KEEPALIVE int sim_amy_running(void) { return AMY_GEM_running() ? 1 : 0; }
EMSCRIPTEN_KEEPALIVE int sim_amy_sample_rate(void) { return AMY_SAMPLE_RATE; }
EMSCRIPTEN_KEEPALIVE int sim_amy_block_size(void) { return AMY_BLOCK_SIZE; }
