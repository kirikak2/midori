// ESP-IDF stand-ins for the MIDoRI simulator (sim/README.md)
#pragma once
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_NOT_SUPPORTED 0x106
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_NVS_NOT_FOUND 0x1102
#ifdef __cplusplus
extern "C" {
#endif
const char* esp_err_to_name(esp_err_t code);
#ifdef __cplusplus
}
#endif
#define ESP_ERROR_CHECK(x) do { esp_err_t err_rc_ = (x); (void)err_rc_; } while (0)
