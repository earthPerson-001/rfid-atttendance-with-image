#include <esp_log.h>
#include <inttypes.h>
#include "rc522.h"

// local includes

#include "globals.h"
#include "rfid-rc522.h"
#include "events.h"

//---------------

#define RC522_MISO 25
#define RC522_MOSI 23
#define RC522_SCK 19
#define RC522_SS (22) // the chip select pin for scanner

rc522_handle_t scanner = NULL;

static void rc522_handler(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    rc522_event_data_t *data = (rc522_event_data_t *)event_data;

    switch (event_id)
    {
    case RC522_EVENT_TAG_SCANNED:
    {
        rc522_tag_t *tag = (rc522_tag_t *)data->ptr;
        ESP_LOGI(TAG, "Tag scanned (sn: %" PRIu64 ")", tag->serial_number);

        rfid_a_s_event_data_t _data = {
            .tag = tag,
        };

        esp_event_post(RFID_A_S_EVENTS, RFID_A_S_RFID_SCANNED, &_data, sizeof(rfid_a_s_event_data_t), portMAX_DELAY);
    }
    break;
    }
}

esp_err_t initialize_rc522(rc522_handle_t *out)
{
    rc522_config_t config = {
#if defined CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S2
        .spi.host = HSPI_HOST,
#else
        .spi.host = SPI2_HOST
#endif
        .spi.miso_gpio = RC522_MISO,
        .spi.mosi_gpio = RC522_MOSI,
        .spi.sck_gpio = RC522_SCK,
        .spi.sda_gpio = RC522_SS,
        // .spi.bus_is_initialized = true, // use existing spi bus created for sdcard
    };

    esp_err_t ret = ESP_OK;
    if (ESP_OK == (ret = rc522_create(&config, &scanner)))
    {
        *out = scanner;

        ESP_LOGI(TAG, "Registering event handler for rc522 events.");
        rc522_register_events(scanner, RC522_EVENT_ANY, rc522_handler, NULL);

        ESP_LOGI(TAG, "Starting the rc522 scanner.");
        if (ESP_OK != (ret = rc522_start(scanner)))
        {
            ESP_LOGE(TAG, "Error %s from rc522_start", esp_err_to_name(ret));
        }
    }
    else
    {
        ESP_LOGE(TAG, "Couldn't create rc522 scanner. %s", esp_err_to_name(ret));
    }

    return ret;
}
