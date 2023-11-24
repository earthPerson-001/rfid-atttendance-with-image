#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#include "freertos/event_groups.h"

#define EXAMPLE_ESP_WIFI_SSID "nointernet"
#define EXAMPLE_ESP_WIFI_PASS "prospectus502715"
#define EXAMPLE_ESP_MAXIMUM_RETRY 2 // repeated trial without delay is pointless
// todo: setup a task to check wifi connection in some interval
//       and try to connect if not connected

// just setting one for now
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HUNT_AND_PECK
#define EXAMPLE_H2E_IDENTIFIER ""

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

    /* FreeRTOS event group to signal when we are connected*/
    extern EventGroupHandle_t s_wifi_event_group;

    void wifi_init_sta(void);

#ifdef __cplusplus
}
#endif