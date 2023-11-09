#include "driver/sdmmc_types.h"
#include "lwip/netdb.h"
#include "ping/ping_sock.h"
#include "inttypes.h"

#include "esp_camera.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_camera.h"

#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "rc522.h"

// local includes
#include "globals.h"
#include "camera.h"
#include "events.h"
#include "upload.h"

//---------------

#define PING_COUNT 8
#define PING_TARGET "www.espressif.com"

TaskHandle_t camera_feed_task_handle = NULL;
EventGroupHandle_t eth_event_group;

// function definition
void initialize_ping(photo_taken_args_t *photo_taken_args, rfid_a_s_event_data_t *rfid_a_s_data);

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

    .xclk_freq_hz = 20000000, // EXPERIMENTAL: Set to 16MHz on ESP32-S2 or ESP32-S3 to enable EDMA mode
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format = PIXFORMAT_JPEG, // YUV422,GRAYSCALE,RGB565,JPEG
    .frame_size = FRAMESIZE_UXGA,   // QQVGA-UXGA, For ESP32, do not use sizes above QVGA when not JPEG. The performance of the ESP32-S series has improved a lot, but JPEG mode always gives better frame rates.

    .jpeg_quality = 12,                  // 0-63, for OV series camera sensors, lower number means higher quality
    .fb_count = 1,                       // When jpeg mode is used, if fb_count more than one, the driver will work in continuous mode.
    .grab_mode = CAMERA_GRAB_WHEN_EMPTY, // CAMERA_GRAB_LATEST. Sets when buffers should be filled
    .sccb_i2c_port = 0};

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

esp_err_t camera_capture(rc522_tag_t *rfid_tag, sdmmc_card_t *card)
{
    // acquire a frame
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb)
    {
        ESP_LOGE(TAG, "Camera Capture Failed");
        return ESP_FAIL;
    }

    rfid_a_s_event_data_t event_data = {
        .fb = fb,
        .tag = rfid_tag};

    // publish the event only
    esp_event_post(RFID_A_S_EVENTS, RFID_A_S_PHOTO_TAKEN, &event_data, sizeof(rfid_a_s_event_data_t), portMAX_DELAY);

    photo_taken_args_t photo_taken_args = {
        .sdcard = card,
    };

    // since long running tasks cannot be handled in event handlers
    // directly calling the function
    initialize_ping(&photo_taken_args, &event_data);

    // return the frame buffer back to the driver for reuse
    esp_camera_fb_return(fb);
    return ESP_OK;
}

void take_photo(rfid_scanned_args_t *rfid_scanned_args, rfid_a_s_event_data_t *rfid_a_s_event_data, photo_taken_args_t *photo_taken_args)
{
    esp_err_t ret;

    // pause the rfid scanning during this period
    if (ESP_OK != (ret = rc522_pause(rfid_scanned_args->rc522)))
    {
        ESP_LOGE(TAG, "Couldn't pause rfid scanning. %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "Starting countdown for taking photo.");

    // countdown from 10 (10 seconds)
    for (uint8_t i = 10; i > 0; i--)
    {
        // vTaskDelay cannot be used inside of an event handler so, requries another method for countdown
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        // display the current countdown
        ESP_LOGI(TAG, "%u", i);
    }

    // sending the rid tag too, for keeping the identity in image
    camera_capture(rfid_a_s_event_data->tag, photo_taken_args->sdcard);

    rc522_resume(rfid_scanned_args->rc522);
}

void register_photo_task(void *args)
{
    take_photo_args_t *take_photo_args = (take_photo_args_t *)args;
    rfid_scanned_args_t *rfid_scanned_args = take_photo_args->rfid_scanned_args;
    rfid_a_s_event_data_t *rfid_a_s_event_data = take_photo_args->rfid_a_s_event_data;
    photo_taken_args_t *photo_taken_args = take_photo_args->photo_taken_args;

    take_photo(rfid_scanned_args, rfid_a_s_event_data, photo_taken_args);
}

static esp_err_t write_to_file_path(const char *path, char *data)
{
    ESP_LOGI(TAG, "Opening file %s", path);
    FILE *f = fopen(path, "w");
    if (f == NULL)
    {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return ESP_FAIL;
    }
    fprintf(f, data);
    fclose(f);
    ESP_LOGI(TAG, "File written");

    return ESP_OK;
}

static void on_ping_success(esp_ping_handle_t hdl, void *args)
{
    ping_callbacks_args_t *callback_args = (ping_callbacks_args_t *)args;
    camera_fb_t *fb = callback_args->rfid_a_s_event_data->fb;

    // upload to server
    upload_jpeg(fb, callback_args->rfid_a_s_event_data->tag);

    uint8_t ttl;
    uint16_t seqno;
    uint32_t elapsed_time, recv_len;
    ip_addr_t target_addr;
    esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TTL, &ttl, sizeof(ttl));
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
    esp_ping_get_profile(hdl, ESP_PING_PROF_SIZE, &recv_len, sizeof(recv_len));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &elapsed_time, sizeof(elapsed_time));
    printf("%lu bytes from %s icmp_seq=%d ttl=%d time=%lu ms\n",
           recv_len, inet_ntoa(target_addr.u_addr.ip4), seqno, ttl, elapsed_time);
}

static void on_ping_timeout(esp_ping_handle_t hdl, void *args)
{
    ping_callbacks_args_t *callback_args = (ping_callbacks_args_t *)args;
    camera_fb_t *fb = callback_args->rfid_a_s_event_data->fb;

    // save to sdcard
    rc522_tag_t *tag = callback_args->rfid_a_s_event_data->tag;

    // if the control reaches this part, the tag and frame buffer should never be null
    // todo: remove at production
    assert(tag != NULL);
    assert(fb != NULL);

    // create filename combining the rfid_tag and current timestamp

    // obtain file path

    // save the frame buffer to the file path

    uint16_t seqno;
    ip_addr_t target_addr;
    esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
    printf("From %s icmp_seq=%d timeout\n", inet_ntoa(target_addr.u_addr.ip4), seqno);
}

static void on_ping_end(esp_ping_handle_t hdl, void *args)
{
    uint32_t transmitted;
    uint32_t received;
    uint32_t total_time_ms;

    esp_ping_get_profile(hdl, ESP_PING_PROF_REQUEST, &transmitted, sizeof(transmitted));
    esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY, &received, sizeof(received));
    esp_ping_get_profile(hdl, ESP_PING_PROF_DURATION, &total_time_ms, sizeof(total_time_ms));
    printf("%lu packets transmitted, %lu received, time %lums\n", transmitted, received, total_time_ms);
}

void initialize_ping(photo_taken_args_t *photo_taken_args, rfid_a_s_event_data_t *rfid_a_s_data)
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

    ping_callbacks_args_t ping_callbacks_args = {
        .event_group_handle = eth_event_group,
        .photo_taken_args = photo_taken_args,
        .rfid_a_s_event_data = rfid_a_s_data,
    };

    cbs.cb_args = &ping_callbacks_args;

    esp_ping_handle_t ping;
    ESP_LOGI(TAG, "Pinging  " PING_TARGET);
    esp_ping_new_session(&ping_config, &cbs, &ping);
}

void start_camera_feed(void *card)
{
    card = (sdmmc_card_t *)card;

    if (!card)
        ESP_LOGE(TAG, "SdCard isn't initialized so cannot save images.");

    while (1)
    {
        // get the current frame buffer
        camera_fb_t *fb = esp_camera_fb_get();

        esp_camera_fb_return(fb);

        /** Might require handling of case when countdown is going on*/

        // send to display
        // ...

        // simulate some delay
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}
