#include "driver/sdmmc_types.h"
#include "lwip/netdb.h"
#include "ping/ping_sock.h"
#include "inttypes.h"

#include "esp_camera.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_camera.h"
#include "esp_spiffs.h"

#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "rc522.h"

// local includes
#include "globals.h"
#include "camera.h"
#include "events.h"
#include "upload.h"
#include "sd-card.h"
#include "wifi.h"
//---------------

TaskHandle_t camera_feed_task_handle = NULL;
EventGroupHandle_t eth_event_group;
QueueHandle_t rfid_photo_queue;

bool photo_being_taken = pdFALSE;
rc522_tag_t *current_tag = NULL;

#define PING_SUCCESS_BIT BIT0
#define PING_FAILED_BIT BIT1

// function declaration
void initialize_and_start_ping(rfid_a_s_event_data_t *rfid_a_s_data);

static camera_config_t camera_config = {
    .pin_pwdn = CAM_PIN_PWDN,
    .pin_reset = CAM_PIN_RESET,
    .pin_xclk = CAM_PIN_XCLK,
    .pin_sccb_sda = CAM_PIN_SIOD,
    .pin_sccb_scl = CAM_PIN_SIOC,

    .pin_d7 = CAM_PIN_D7,
    .pin_d6 = CAM_PIN_D6,
    .pin_d5 = CAM_PIN_D5,
    .pin_d4 = CAM_PIN_D4,
    .pin_d3 = CAM_PIN_D3,
    .pin_d2 = CAM_PIN_D2,
    .pin_d1 = CAM_PIN_D1,
    .pin_d0 = CAM_PIN_D0,
    .pin_vsync = CAM_PIN_VSYNC,
    .pin_href = CAM_PIN_HREF,
    .pin_pclk = CAM_PIN_PCLK,

    .xclk_freq_hz = 16500000, // https://github.com/espressif/esp32-camera/issues/150
                              // EXPERIMENTAL: Set to 16MHz on ESP32-S2 or ESP32-S3 to enable EDMA mode
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format = PIXFORMAT_JPEG, // YUV422,GRAYSCALE,RGB565,JPEG
    .frame_size = FRAMESIZE_SVGA,   // QQVGA-UXGA, For ESP32, do not use sizes above QVGA when not JPEG. The performance of the ESP32-S series has improved a lot, but JPEG mode always gives better frame rates.
    // use higher quality initially as described in : https://github.com/espressif/esp32-camera/issues/185#issue-716800775
    .jpeg_quality = 5, // 0-63, for OV series camera sensors, lower number means higher quality
    .fb_count = 3,      // When jpeg mode is used, if fb_count more than one, the driver will work in continuous mode.
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode = CAMERA_GRAB_LATEST, // CAMERA_GRAB_LATEST. Sets when buffers should be filled
    .sccb_i2c_port = 0};

esp_err_t initialize_spiffs()
{
    ESP_LOGI(TAG, "Initializing SPIFFS");

    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true};

    // Use settings defined above to initialize and mount SPIFFS filesystem.
    // Note: esp_vfs_spiffs_register is an all-in-one convenience function.
    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK)
    {
        if (ret == ESP_FAIL)
        {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        }
        else if (ret == ESP_ERR_NOT_FOUND)
        {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        }
        else
        {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return ret;
    }
    return ret;
}

esp_err_t camera_init()
{
    // initialize the camera
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Camera Init Failed");
        return err;
    }

    return ESP_OK;
}

esp_err_t camera_capture(rc522_tag_t *rfid_tag, sdmmc_card_t *card, camera_fb_t *fb)
{

    if (!fb)
    {
        ESP_LOGE(TAG, "The frame buffer is emtpy");
        return ESP_FAIL;
    }

    // if the wifi isn't connected, there is no point in pinging
    if (WIFI_CONNECTED_BIT & xEventGroupGetBits(s_wifi_event_group))
    {
        rfid_a_s_event_data_t event_data = {
            .fb = fb,
            .tag = rfid_tag,
            .card = card};

        // since long running tasks cannot be handled in event handlers
        // directly calling the function
        initialize_and_start_ping(&event_data);
    }
    else
    {
        // directly try to save to sdcard
        if (NULL == card)
        {
            ESP_LOGE(TAG, "Couldn't save to sdcard as it wasn't initialized");
        }
        else
        {
            ESP_LOGI(TAG, "saving image to sdcard.");
            if (ESP_OK != save_image_to_sdcard(fb->buf, rfid_tag->serial_number))
            {
                ESP_LOGE(TAG, "Couldn't save image for rfid_tag: %" PRIu64 " to sdcard.", rfid_tag->serial_number);
            }
        }
    }

    return ESP_OK;
}

void register_photo_task(void *args)
{
    rfid_a_s_event_data_t *queue_data = malloc(sizeof(rfid_a_s_event_data_t));

    while (1)
    {
        // block on queue

        // it doesn't completely block, but okay for now
        if (pdTRUE == xQueueReceive(rfid_photo_queue, (void *)queue_data, 600000 / portTICK_PERIOD_MS))
        {
            rfid_a_s_event_data_t *rfid_a_s_event_data = (rfid_a_s_event_data_t *)queue_data;

            // take photo
            // sending the rid tag too, for keeping the identity in image
            camera_capture(rfid_a_s_event_data->tag, rfid_a_s_event_data->card, rfid_a_s_event_data->fb);
        }
    }

    // if in case the flow returns here
    free(queue_data);
    vTaskDelete(NULL);
}

static void on_ping_success(esp_ping_handle_t hdl, void *args)
{
    rfid_a_s_event_data_t *rfid_a_s_data = (rfid_a_s_event_data_t *)args;

    uint8_t ttl;
    uint16_t seqno;
    uint32_t elapsed_time, recv_len;
    ip_addr_t target_addr;
    esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TTL, &ttl, sizeof(ttl));
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
    esp_ping_get_profile(hdl, ESP_PING_PROF_SIZE, &recv_len, sizeof(recv_len));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &elapsed_time, sizeof(elapsed_time));
    ESP_LOGI(TAG, "%lu bytes from %s icmp_seq=%d ttl=%d time=%lu ms\n",
             recv_len, inet_ntoa(target_addr.u_addr.ip4), seqno, ttl, elapsed_time);

    // stopping the ping session and deleting it
    esp_err_t ret;
    if (ESP_OK != (ret = esp_ping_stop(hdl)))
    {
        ESP_LOGE(TAG, "Couldn't stop ping session. (error : %s)", esp_err_to_name(ret));
    }
    if (ESP_OK != (ret = esp_ping_delete_session(hdl)))
    {
        ESP_LOGE(TAG, "Couldn't delete ping session. (error : %s)", esp_err_to_name(ret));
    }

    // the memory freeing of `*args` must be handled inside of the task
    // as the lifetime of task's args need to be valid inside of task

    // one success is enough
    xTaskCreate(
        upload_jpeg_task,
        "Upload_JPEG",
        TASK_UPLOAD_JPEG_STACK_SIZE,
        rfid_a_s_data,
        UPLOAD_JPEG_TASK_CORE_AFFINITY,
        NULL);
}

static void on_ping_timeout(esp_ping_handle_t hdl, void *args)
{
    rfid_a_s_event_data_t *rfid_a_s_data = (rfid_a_s_event_data_t *)args;

    uint16_t seqno;
    ip_addr_t target_addr;
    esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
    ESP_LOGI(TAG, "From %s icmp_seq=%d timeout\n", inet_ntoa(target_addr.u_addr.ip4), seqno);

    // if the last sequence fails
    if (seqno == PING_COUNT)
    {
        // stopping the ping session and deleting it
        esp_err_t ret;
        if (ESP_OK != (ret = esp_ping_stop(hdl)))
        {
            ESP_LOGE(TAG, "Couldn't stop ping session. (error : %s)", esp_err_to_name(ret));
        }
        if (ESP_OK != (ret = esp_ping_delete_session(hdl)))
        {
            ESP_LOGE(TAG, "Couldn't delete ping session. (error : %s)", esp_err_to_name(ret));
        }
        ESP_LOGI(TAG, "Saving image locally because of unavailability of internet access.");

        // keeping a copy for freeing the callback args later on
        esp_err_t delete_ret = ret;

        // if the control reaches this part, the tag and frame buffer should never be null
        // todo: remove at production
        assert(rfid_a_s_data->tag != NULL);
        assert(rfid_a_s_data->fb != NULL);

        // save the frame buffer to the file path
        sdmmc_card_t *card = rfid_a_s_data->card;
        if (card == NULL)
        {
            ESP_LOGE(TAG, "Couldn't save to sdcard as it wasn't initialized");
        }
        else
        {
            ESP_LOGI(TAG, "saving image to sdcard.");
            if (ESP_OK != save_image_to_sdcard(rfid_a_s_data->fb->buf, rfid_a_s_data->tag->serial_number))
            {
                ESP_LOGE(TAG, "Couldn't save image for rfid_tag: %" PRIu64 " to sdcard.", rfid_a_s_data->tag->serial_number);
            }
        }

        // free the callback args if the ping sessions was sucessfully deleted
        if (ESP_OK == delete_ret) // after all ping ends
        {
            // freeing the memory
            free(args);
        }
    }
}

static void on_ping_end(esp_ping_handle_t hdl, void *args)
{
    uint32_t transmitted;
    uint32_t received;
    uint32_t total_time_ms;

    esp_ping_get_profile(hdl, ESP_PING_PROF_REQUEST, &transmitted, sizeof(transmitted));
    esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY, &received, sizeof(received));
    esp_ping_get_profile(hdl, ESP_PING_PROF_DURATION, &total_time_ms, sizeof(total_time_ms));
    ESP_LOGI(TAG, "%lu packets transmitted, %lu received, time %lums\n", transmitted, received, total_time_ms);

    // the image should already have been uploaded if there was a success
}

void initialize_and_start_ping(rfid_a_s_event_data_t *rfid_a_s_data)
{
    /* convert URL to IP address */
    ip_addr_t target_addr;
    struct addrinfo hint;
    struct addrinfo *res = NULL;
    memset(&hint, 0, sizeof(hint));
    memset(&target_addr, 0, sizeof(target_addr));
    getaddrinfo(PING_TARGET, NULL, &hint, &res);
    struct in_addr addr4 = ((struct sockaddr_in *)(res->ai_addr))->sin_addr;
    inet_addr_to_ip4addr(ip_2_ip4(&target_addr), &addr4);
    freeaddrinfo(res);

    esp_ping_config_t ping_config = ESP_PING_DEFAULT_CONFIG();
    ping_config.target_addr = target_addr; // target IP address
    ping_config.count = PING_COUNT;

    /* set callback functions */
    esp_ping_callbacks_t cbs;
    cbs.on_ping_success = on_ping_success;
    cbs.on_ping_timeout = on_ping_timeout;
    cbs.on_ping_end = on_ping_end;

    rfid_a_s_event_data_t *callback_args = malloc(sizeof(rfid_a_s_event_data_t));
    memcpy(callback_args, rfid_a_s_data, sizeof(*rfid_a_s_data));
    cbs.cb_args = (void *)callback_args;

    esp_ping_handle_t ping;
    esp_err_t ret = ESP_OK;
    if (ESP_OK != (ret = esp_ping_new_session(&ping_config, &cbs, &ping)))
    {
        ESP_LOGI(TAG, "Pinging " PING_TARGET " failed");
    }
    else
    {
        if (ESP_OK == (ret = esp_ping_start(ping)))
        {
            ESP_LOGI(TAG, "Pinging  " PING_TARGET);
        }
        else
        {
            ESP_LOGE(TAG, "Couldn't start the ping session to ping " PING_TARGET);
        }
    }
}

void handle_tag_scanned(void *ptr, esp_event_base_t base, int32_t event_id, void *event_data)
{
    rfid_a_s_event_data_t *evt_data = (rfid_a_s_event_data_t *)event_data;

    switch (event_id)
    {
    case RFID_A_S_RFID_SCANNED:
        photo_being_taken = pdTRUE;
        current_tag = evt_data->tag;
        break;
    default:
        break;
    }
}

void start_camera_feed(void *card)
{
    esp_err_t ret = ESP_OK;

    card = (sdmmc_card_t *)card;

    if (!card)
        ESP_LOGE(TAG, "SdCard isn't initialized so cannot save images.");

    rfid_photo_queue = xQueueCreate(RFID_PHOTO_QUEUE_SIZE, sizeof(rfid_a_s_event_data_t));

    ret = esp_event_handler_register(RFID_A_S_EVENTS, RFID_A_S_RFID_SCANNED, handle_tag_scanned, NULL);

    if (ESP_OK != ret)
    {
        ESP_LOGE(TAG, "Couldn't register event handler (error : %s)", esp_err_to_name(ret));
    }

    // start all image upload/save as a new task
    xTaskCreate(register_photo_task,
                "Register_Photo_Task",
                4096,
                NULL,
                REGISTER_PHOTO_TASK_PRIORITY,
                &camera_feed_task_handle);

    camera_fb_t *fb;

    // now use lower quality as described in : https://github.com/espressif/esp32-camera/issues/185#issue-716800775
    sensor_t *ss = esp_camera_sensor_get();
    ss->set_quality(ss, 12);

    while (1)
    {

        // get the current frame buffer
        fb = esp_camera_fb_get();

        // do some display stuffs
        vTaskDelay(50 / portTICK_PERIOD_MS);

        // release the buffer
        esp_camera_fb_return(fb);

        if (photo_being_taken == pdTRUE && fb != NULL && current_tag != NULL)
        {
            // the current frame buffer
            fb = esp_camera_fb_get();

            /** Might require handling of case when countdown is going on*/

            rfid_a_s_event_data_t queue_data = {
                .fb = fb,
                .tag = current_tag,
                .card = card,
            };
            // logging the captured frame size
            ESP_LOGI(TAG, "The captured frame size is: %zu", fb->len);
            // publish the event only
            esp_event_post(RFID_A_S_EVENTS, RFID_A_S_PHOTO_TAKEN, &queue_data, sizeof(rfid_a_s_event_data_t), portMAX_DELAY);

            // send to queue for other task
            // this will fail if queue is full
            // we don't care about lost images when the queue is full, as this condition implies some other thing isn't working
            // i.e. either upload or save functionality
            // so just logging
            if (pdTRUE != (ret = xQueueSend(rfid_photo_queue, (void *)&queue_data, 100)))
            {
                ESP_LOGE(TAG, "Couldn't send to queue `rfid_photo_queue` (error : %s)", esp_err_to_name(ret));
            }

            esp_camera_fb_return(fb);

            // reset the variables
            photo_being_taken = pdFALSE;
            current_tag = NULL;
        }
    }

    // if somehow it exits
    vTaskDelete(NULL);
}
