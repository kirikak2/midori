// NVS on the page's localStorage: key "midori-nvs/<namespace>/<key>".
// Strings are stored as they are, blobs and integers as hex / decimal text.
// What NVS keeps on the device (MIDI Devices patches, settings) is per
// viewer here; when storage is unavailable every read misses.
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <emscripten.h>
#include "nvs.h"

EM_JS(int, nvs_js_get, (const char* key, char* out, int cap), {
  let v = null;
  try { v = localStorage.getItem(UTF8ToString(key)); } catch (e) {}
  if (v === null) return -1;
  const n = lengthBytesUTF8(v);
  if (out && cap > 0) stringToUTF8(v, out, cap);
  return n;
});

EM_JS(void, nvs_js_set, (const char* key, const char* value), {
  try {
    if (value) localStorage.setItem(UTF8ToString(key), UTF8ToString(value));
    else localStorage.removeItem(UTF8ToString(key));
  } catch (e) {}
});

EM_JS(void, nvs_js_erase_prefix, (const char* prefix), {
  try {
    const p = UTF8ToString(prefix);
    for (let i = localStorage.length - 1; i >= 0; i--) {
      const k = localStorage.key(i);
      if (k && k.startsWith(p)) localStorage.removeItem(k);
    }
  } catch (e) {}
});

#define MAX_HANDLES 16
static char s_ns[MAX_HANDLES][16];
static bool s_used[MAX_HANDLES];

static void make_key(nvs_handle_t h, const char* key, char* out, size_t cap)
{
  snprintf(out, cap, "midori-nvs/%s/%s", s_ns[h - 1], key ? key : "");
}

esp_err_t nvs_open(const char* ns, nvs_open_mode_t mode, nvs_handle_t* out)
{
  (void)mode;
  for (int i = 0; i < MAX_HANDLES; i++) {
    if (s_used[i]) continue;
    s_used[i] = true;
    strncpy(s_ns[i], ns, sizeof(s_ns[i]) - 1);
    s_ns[i][sizeof(s_ns[i]) - 1] = 0;
    *out = (nvs_handle_t)(i + 1);
    return ESP_OK;
  }
  return ESP_ERR_NO_MEM;
}

void nvs_close(nvs_handle_t h) { if (h >= 1 && h <= MAX_HANDLES) s_used[h - 1] = false; }
esp_err_t nvs_commit(nvs_handle_t h) { (void)h; return ESP_OK; }

esp_err_t nvs_get_str(nvs_handle_t h, const char* key, char* out, size_t* len)
{
  char k[64];
  make_key(h, key, k, sizeof(k));
  int n = nvs_js_get(k, NULL, 0);
  if (n < 0) return ESP_ERR_NVS_NOT_FOUND;
  if (!out) { *len = (size_t)n + 1; return ESP_OK; }
  if (*len < (size_t)n + 1) { *len = (size_t)n + 1; return ESP_ERR_INVALID_SIZE; }
  nvs_js_get(k, out, (int)*len);
  *len = (size_t)n + 1;
  return ESP_OK;
}

esp_err_t nvs_set_str(nvs_handle_t h, const char* key, const char* value)
{
  char k[64];
  make_key(h, key, k, sizeof(k));
  nvs_js_set(k, value);
  return ESP_OK;
}

esp_err_t nvs_get_blob(nvs_handle_t h, const char* key, void* out, size_t* len)
{
  char k[64];
  make_key(h, key, k, sizeof(k));
  int n = nvs_js_get(k, NULL, 0);
  if (n < 0) return ESP_ERR_NVS_NOT_FOUND;
  size_t bytes = (size_t)n / 2;
  if (!out) { *len = bytes; return ESP_OK; }
  if (*len < bytes) { *len = bytes; return ESP_ERR_INVALID_SIZE; }
  char* hex = malloc((size_t)n + 1);
  if (!hex) return ESP_ERR_NO_MEM;
  nvs_js_get(k, hex, n + 1);
  for (size_t i = 0; i < bytes; i++) {
    unsigned v = 0;
    sscanf(hex + i * 2, "%2x", &v);
    ((uint8_t*)out)[i] = (uint8_t)v;
  }
  free(hex);
  *len = bytes;
  return ESP_OK;
}

esp_err_t nvs_set_blob(nvs_handle_t h, const char* key, const void* value, size_t len)
{
  char* hex = malloc(len * 2 + 1);
  if (!hex) return ESP_ERR_NO_MEM;
  for (size_t i = 0; i < len; i++) sprintf(hex + i * 2, "%02x", ((const uint8_t*)value)[i]);
  hex[len * 2] = 0;
  esp_err_t r = nvs_set_str(h, key, hex);
  free(hex);
  return r;
}

static esp_err_t get_int(nvs_handle_t h, const char* key, long long* out)
{
  char buf[32];
  size_t len = sizeof(buf);
  esp_err_t r = nvs_get_str(h, key, buf, &len);
  if (r == ESP_OK) *out = strtoll(buf, NULL, 10);
  return r;
}

static esp_err_t set_int(nvs_handle_t h, const char* key, long long v)
{
  char buf[32];
  snprintf(buf, sizeof(buf), "%lld", v);
  return nvs_set_str(h, key, buf);
}

esp_err_t nvs_get_u8(nvs_handle_t h, const char* key, uint8_t* out) { long long v; esp_err_t r = get_int(h, key, &v); if (!r) *out = (uint8_t)v; return r; }
esp_err_t nvs_set_u8(nvs_handle_t h, const char* key, uint8_t v) { return set_int(h, key, v); }
esp_err_t nvs_get_i32(nvs_handle_t h, const char* key, int32_t* out) { long long v; esp_err_t r = get_int(h, key, &v); if (!r) *out = (int32_t)v; return r; }
esp_err_t nvs_set_i32(nvs_handle_t h, const char* key, int32_t v) { return set_int(h, key, v); }
esp_err_t nvs_get_u32(nvs_handle_t h, const char* key, uint32_t* out) { long long v; esp_err_t r = get_int(h, key, &v); if (!r) *out = (uint32_t)v; return r; }
esp_err_t nvs_set_u32(nvs_handle_t h, const char* key, uint32_t v) { return set_int(h, key, v); }

esp_err_t nvs_erase_key(nvs_handle_t h, const char* key)
{
  char k[64];
  make_key(h, key, k, sizeof(k));
  if (nvs_js_get(k, NULL, 0) < 0) return ESP_ERR_NVS_NOT_FOUND;
  nvs_js_set(k, NULL);
  return ESP_OK;
}

esp_err_t nvs_erase_all(nvs_handle_t h)
{
  char k[64];
  make_key(h, "", k, sizeof(k));
  nvs_js_erase_prefix(k);
  return ESP_OK;
}
