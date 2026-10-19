#pragma once

#include "globals.h" // to get the upload address

#ifdef __cplusplus
extern "C"
{
#endif

#define OTA_HASH_LEN 32
#define OTA_ACCEPT_CRICICALITY_BELOW 11               // accept otas below this criticality

#define OTA_CHANNEL_APLHA 0  // all the channels are accepted
#define OTA_CHANNEL_BETA 1   // beta and stable are accepted
#define OTA_CHANNEL_STABLE 2 // only stable channel is accepted

    struct esp_err_t;

    esp_err_t start_ota_process();

#ifdef __cplusplus
}
#endif
