#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

    struct esp_err_t;
    struct camera_fb_t;
    struct rc522_tag_t;

#ifndef ESP_EVENT_ANY_ID
#define ESP_EVENT_ANY_ID -1
#endif
    esp_err_t upload_jpeg(camera_fb_t *fb, rc522_tag_t *rfid_tag);

#ifdef __cplusplus
}
#endif
