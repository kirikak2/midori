// Stand-in for SDL2 in the MIDoRI simulator (Emscripten).
//
// LovyanGFX picks its platform by what it can include: with <SDL2/SDL.h>
// present and nothing else matching it takes platforms/sdl, whose common.cpp
// needs only a clock and a delay from SDL. The simulator draws through its own
// framebuffer panel (sim/port/panel_wasm_fb.*), so this header gives those
// few calls on top of Emscripten's clock instead of linking the SDL2 port.
#pragma once
#ifndef SDL_h_
#define SDL_h_

#include <stdint.h>
#include <emscripten.h>

#ifdef __cplusplus
extern "C" {
#endif

// Types named by Panel_sdl.hpp, which platforms/device.hpp includes on this
// platform. Panel_sdl itself is never built, so they stay incomplete.
typedef struct SDL_Window SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Texture SDL_Texture;
typedef struct SDL_mutex SDL_mutex;
typedef int SDL_Keymod;
typedef int SDL_KeyCode;
enum { SDLK_UNKNOWN = 0 };

static inline uint32_t SDL_GetTicks(void) { return (uint32_t)emscripten_get_now(); }
static inline uint64_t SDL_GetPerformanceFrequency(void) { return 1000000ull; }
static inline uint64_t SDL_GetPerformanceCounter(void) { return (uint64_t)(emscripten_get_now() * 1000.0); }
// Nothing may block in the browser: a delay returns at once.
static inline void SDL_Delay(uint32_t ms) { (void)ms; }

#ifdef __cplusplus
}
#endif
#endif
