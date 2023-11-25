#pragma once

#include "globals.h" // to get the upload address

#ifdef __cplusplus
extern "C"
{
#endif

#define OTA_HASH_LEN 32

    struct esp_err_t;

    esp_err_t start_ota_process();

#ifdef __cplusplus
}
#endif
