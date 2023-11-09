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

    typedef struct photo_taken_args_t
    {
        sdmmc_card_t *sdcard;
    } photo_taken_args_t;

    typedef struct rfid_scanned_args_t
    {
        rc522_handle_t rc522;
    } rfid_scanned_args_t;

    /**
     * rfid_a_s_event_data_t consists up of the scanned rfid tag, if rfid was scanned
     * and the frame buffer of the captured image if the image has already been captured
     */
    typedef struct rfid_a_s_event_data_t
    {
        rc522_tag_t *tag;
        camera_fb_t *fb;

    } rfid_a_s_event_data_t;

#ifdef __cplusplus
}
#endif
