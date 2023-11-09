#pragma once

#include "events.h"

#ifdef __cplusplus
extern "C"
{
#endif

    struct esp_err_t;
    struct sdmmc_card_t;
    struct TaskHandle_t;

// AI-Thinker PIN Map
#define CAM_PIN_PWDN 32  // power down is not used
#define CAM_PIN_RESET -1 // software reset will be performed
#define CAM_PIN_XCLK 0
#define CAM_PIN_SIOD 26
#define CAM_PIN_SIOC 27

#define CAM_PIN_D7 35
#define CAM_PIN_D6 34
#define CAM_PIN_D5 39
#define CAM_PIN_D4 36
#define CAM_PIN_D3 21
#define CAM_PIN_D2 19
#define CAM_PIN_D1 18
#define CAM_PIN_D0 5
#define CAM_PIN_VSYNC 25
#define CAM_PIN_HREF 23
#define CAM_PIN_PCLK 22

    extern TaskHandle_t camera_feed_task_handle;
    extern rc522_handle_t scanner;

    typedef struct ping_callbacks_args_t
    {
        EventGroupHandle_t event_group_handle;
        photo_taken_args_t *photo_taken_args;
        rfid_a_s_event_data_t *rfid_a_s_event_data;
    } ping_callbacks_args_t;

    typedef struct take_photo_args_t
    {
        rfid_scanned_args_t *rfid_scanned_args;
        rfid_a_s_event_data_t *rfid_a_s_event_data;
        photo_taken_args_t *photo_taken_args;
    } take_photo_args_t;

    esp_err_t camera_init();

    void register_photo_task(void *args);

    /**
     * @param card: The sdcard to store the captured images when internet connection is unavailable.
     */
    void start_camera_feed(void *card);

#ifdef __cplusplus
}
#endif