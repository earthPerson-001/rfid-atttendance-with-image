#pragma once

#include "rc522.h"
#include "driver/sdmmc_types.h"
#include "esp_camera.h"

#ifdef __cplusplus
extern "C"
{
#endif

#ifndef ESP_EVENT_ANY_ID
#define ESP_EVENT_ANY_ID -1
#endif

    ESP_EVENT_DECLARE_BASE(RFID_A_S_EVENTS); // RFID based attendance system base events

    typedef enum
    {
        RFID_A_S_EVENT_ANY = ESP_EVENT_ANY_ID,
        RFID_A_S_EVENT_NONE,
        RFID_A_S_RFID_SCANNED,
        RFID_A_S_PHOTO_TAKEN,
    } rfid_a_s_event_t;


    /**
     * rfid_a_s_event_data_t consists up of the scanned rfid tag, if rfid was scanned
     * the sdcard if it has already been initialized
     * and the frame buffer of the captured image if the image has already been captured
     */
    typedef struct rfid_a_s_event_data_t
    {
        rc522_tag_t *tag;
        camera_fb_t *fb;
        sdmmc_card_t *card;

    } rfid_a_s_event_data_t;

#ifdef __cplusplus
}
#endif
