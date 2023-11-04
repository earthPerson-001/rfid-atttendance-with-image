#if defined _WIFI_H_ == 0 // _WIFI_H_
#define _WIFI_H_ 1

#define EXAMPLE_ESP_WIFI_SSID "internet"
#define EXAMPLE_ESP_WIFI_PASS "prospectus502715"
#define EXAMPLE_ESP_MAXIMUM_RETRY 10

// just setting one for now
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HUNT_AND_PECK
#define EXAMPLE_H2E_IDENTIFIER ""

void wifi_init_sta(void);

#endif // _WIFI_H_
