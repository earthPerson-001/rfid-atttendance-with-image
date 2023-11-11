#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

    struct esp_err_t;
    struct camera_fb_t;
    struct rc522_tag_t;

#define UPLOAD_RETRY_COUNT 2 // retries on failure

#ifndef ESP_EVENT_ANY_ID
#define ESP_EVENT_ANY_ID -1
#endif

    void upload_jpeg_task(void *args);

#ifdef __cplusplus
}
#endif
