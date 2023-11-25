/*
    Hello World!
*/

#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_err.h"
#include "esp_flash.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/sdmmc_types.h"
#include "rc522.h"

// Local includes

#include "globals.h"
#include "wifi.h"

#if defined USE_RC522 == 1
#include "rfid-rc522.h"
#endif

#if defined USE_ESP32CAM == 1 // rfid reader and camera both use SPI protocol, so excluding camera when using rfid reader
#include "camera.h"
#define ESP32_CAM_LED_BUILTIN_PIN 33
#define ESP32_CAM_CAMERA_FLASH_PIN 4 // This LED works with inverted logic, so you send a LOW signal to turn it on and a HIGH signal to turn it off.
#endif

#include "sd-card.h"
#include "events.h"
#include "ota.h"
// --------------

#define LED_BUILTIN_PIN 2

ESP_EVENT_DEFINE_BASE(RFID_A_S_EVENTS);

void initialize_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        // 1.OTA app partition table has a smaller NVS partition size than the non-OTA
        // partition table. This size mismatch may cause NVS initialization to fail.
        // 2.NVS partition contains data in new format and cannot be recognized by this version of code.
        // If this happens, we erase NVS partition and initialize NVS again.
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

void app_main(void)
{
    /* Print chip information */
    esp_chip_info_t chip_info;
    uint32_t flash_size;
    esp_chip_info(&chip_info);
    printf("This is %s chip with %d CPU core(s), WiFi%s%s, ",
           CONFIG_IDF_TARGET,
           chip_info.cores,
           (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
           (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");

    unsigned major_rev = chip_info.revision / 100;
    unsigned minor_rev = chip_info.revision % 100;
    printf("silicon revision v%u.%u, ", major_rev, minor_rev);
    if (esp_flash_get_size(NULL, &flash_size) != ESP_OK)
    {
        printf("Get flash size failed");
        return;
    }

    printf("%luMB %s flash\n", flash_size / (1024 * 1024),
           (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

    printf("Minimum free heap size: %lu bytes\n", esp_get_minimum_free_heap_size());

    ESP_LOGI(TAG, "Initializing nvs\n");
    initialize_nvs();

    // if not connected to wifi
    // try connecting to wifi
    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA\n");
    wifi_init_sta();

    // start ota
    if (ESP_OK != start_ota_process())
        ESP_LOGE(TAG, "Couldn't start ota process.");

    // set some delay to clear up before initializing camera (if high power is consumed)
    // vTaskDelay(1000/portTICK_PERIOD_MS);

    sdmmc_card_t *card = NULL;

    rc522_handle_t scanner = NULL;

    /** Not using when using rc522 as both use SPI protocol */
    if (USE_ESP32CAM == 1)
    {
        // initialize spiffs
        initialize_spiffs();

        // initializing the camera
        camera_init();

        init_sd_card(&card);

        // the reference to the card should be valid until it is deinitialized
        // starting the camera feed task
        xTaskCreate(start_camera_feed,
                    "Camera_Feed_Task",
                    4096,
                    card,
                    CAMERA_FEED_TASK_PRIORITY,
                    &camera_feed_task_handle);
    }

    if (USE_RC522 == 1)
    {
        // initialize rfid stuffs
        initialize_rc522(&scanner);
    }

    // blinking led every 500ms
    gpio_set_direction(LED_BUILTIN_PIN, GPIO_MODE_OUTPUT);
#if defined USE_ESP32CAM == 1
    if (USE_ESP32CAM == 1)
    {
        gpio_set_direction(ESP32_CAM_LED_BUILTIN_PIN, GPIO_MODE_OUTPUT);
        // gpio_set_direction(ESP32_CAM_CAMERA_FLASH_PIN, GPIO_MODE_OUTPUT);
    }
#endif

    uint count = 0;
    while (1) // debug
    {
        count += 1;
        vTaskDelay(500 / portTICK_PERIOD_MS);
        gpio_set_level(LED_BUILTIN_PIN, 1);
        vTaskDelay(500 / portTICK_PERIOD_MS);
        gpio_set_level(LED_BUILTIN_PIN, 0);

        // mock rfid scan
        if (USE_ESP32CAM == 1)
        {
            rc522_tag_t tag = {
                .serial_number = 911101686122, // a mock serial number
            };

            rfid_a_s_event_data_t _data = {
                .tag = &tag};

            if (count % 10 == 0)
            {
                ESP_LOGI(TAG, "Mocking a rfid scan");
                esp_event_post(RFID_A_S_EVENTS, RFID_A_S_RFID_SCANNED, &_data, sizeof(rfid_a_s_event_data_t), portMAX_DELAY);
            }
        }

        if (USE_ESP32CAM == 1)
        {
            // for esp32-cam
            vTaskDelay(500 / portTICK_PERIOD_MS);
            gpio_set_level(ESP32_CAM_LED_BUILTIN_PIN, 0); // on
            vTaskDelay(500 / portTICK_PERIOD_MS);
            gpio_set_level(ESP32_CAM_LED_BUILTIN_PIN, 1); // off
        }
    }
}
