#pragma once
typedef struct { const char* version; const char* project_name; } esp_app_desc_t;
#ifdef __cplusplus
extern "C" {
#endif
const esp_app_desc_t* esp_app_get_description(void);
#ifdef __cplusplus
}
#endif
