#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#include "freertos/event_groups.h"

#define EXAMPLE_ESP_WIFI_SSID "internet"
#define EXAMPLE_ESP_WIFI_PASS "prospectus502715"
#define EXAMPLE_ESP_MAXIMUM_RETRY 5

// just setting one for now
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HUNT_AND_PECK
#define EXAMPLE_H2E_IDENTIFIER ""

    /* FreeRTOS event group to signal when we are connected*/
    extern EventGroupHandle_t s_wifi_event_group;

    void wifi_init_sta(void);

#ifdef __cplusplus
}
#endif