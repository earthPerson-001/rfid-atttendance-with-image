#include <string.h>
#include <sys/param.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>

#include "esp_http_client.h"
#include "esp_timer.h"
#include "esp_camera.h"
#include "esp_tls.h"
#include "esp_err.h"
#include "esp_log.h"

#include "rc522.h"

// local includes

#include "globals.h"
#include "events.h"
#include "upload.h"
// --------------

#define HTTP_POST_REQUEST_BODY_SIZE 512   // allocate this on heap
#define HTTP_POST_REQUEST_HEADER_SIZE 512 // allocate header on heap
#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE = "multipart/form-data;boundary=" PART_BOUNDARY;
static const char *_CONTENT_DISPOSITION = "form-data; name=\"upfile\"; filename=\"%s\"; ";
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_CONTENT_TYPE_HEADER = "Content-Type: multipart/form-data;boundary=" PART_BOUNDARY "\r\n";

static esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    static char *output_buffer; // Buffer to store response of http request from event handler
    static int output_len;      // Stores number of bytes read
    switch (evt->event_id)
    {
    case HTTP_EVENT_ERROR:
        ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
        // Clean the buffer in case of a new request
        if (output_len == 0 && evt->user_data)
        {
            // we are just starting to copy the output data into the use
            memset(evt->user_data, 0, MAX_HTTP_OUTPUT_BUFFER);
        }
        /*
         *  Check for chunked encoding is added as the URL for chunked encoding used in this example returns binary data.
         *  However, event handler can also be used in case chunked encoding is used.
         */
        if (!esp_http_client_is_chunked_response(evt->client))
        {
            // If user_data buffer is configured, copy the response into the buffer
            int copy_len = 0;
            if (evt->user_data)
            {
                // The last byte in evt->user_data is kept for the NULL character in case of out-of-bound access.
                copy_len = MIN(evt->data_len, (MAX_HTTP_OUTPUT_BUFFER - output_len));
                if (copy_len)
                {
                    memcpy(evt->user_data + output_len, evt->data, copy_len);
                }
            }
            else
            {
                int content_len = esp_http_client_get_content_length(evt->client);
                if (output_buffer == NULL)
                {
                    // We initialize output_buffer with 0 because it is used by strlen() and similar functions therefore should be null terminated.
                    output_buffer = (char *)calloc(content_len + 1, sizeof(char));
                    output_len = 0;
                    if (output_buffer == NULL)
                    {
                        ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
                        return ESP_FAIL;
                    }
                }
                copy_len = MIN(evt->data_len, (content_len - output_len));
                if (copy_len)
                {
                    memcpy(output_buffer + output_len, evt->data, copy_len);
                }
            }
            output_len += copy_len;
        }

        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
        if (output_buffer != NULL)
        {
            // Response is accumulated in output_buffer. Uncomment the below line to print the accumulated response
            // ESP_LOG_BUFFER_HEX(TAG, output_buffer, output_len);
            free(output_buffer);
            output_buffer = NULL;
        }
        output_len = 0;
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
        int mbedtls_err = 0;
        esp_err_t err = esp_tls_get_and_clear_last_error((esp_tls_error_handle_t)evt->data, &mbedtls_err, NULL);
        if (err != 0)
        {
            ESP_LOGI(TAG, "Last esp error code: 0x%X", err);
            ESP_LOGI(TAG, "Last mbedtls failure: 0x%X", mbedtls_err);
        }
        if (output_buffer != NULL)
        {
            free(output_buffer);
            output_buffer = NULL;
        }
        output_len = 0;
        break;
    case HTTP_EVENT_REDIRECT:
        ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
        esp_http_client_set_header(evt->client, "From", "user@example.com");
        esp_http_client_set_header(evt->client, "Accept", "text/html");
        esp_http_client_set_redirection(evt->client);
        break;
    }
    return ESP_OK;
}

inline void send_crlf(esp_http_client_handle_t client)
{
    esp_http_client_write(client, "\r\n", 2);
};

void upload_jpeg_task(void *args)
{
    u8_t retry = 0;
    rfid_a_s_event_data_t *event_data = (rfid_a_s_event_data_t *)args;

    // generic filename for now
    const char *filename = "capture.jpg";

    // if the control reaches this part, the tag and frame buffer should never be null
    // todo: remove at production
    assert(event_data->tag != NULL);
    assert(event_data->fb != NULL);

    // copying the required passed in data as we are unsure of their lifetimes
    camera_fb_t *fb = malloc(sizeof(camera_fb_t));
    memcpy(fb, event_data->fb, sizeof(camera_fb_t));

    uint64_t serial_number = event_data->tag->serial_number;

    // all the required data are already copied
    // so freeing the memory used by args
    free(args);

    esp_err_t err;
    size_t fb_len; // to store the exact jpeg length
    int64_t fr_start;

    size_t hlen;
    char chunk_len_hex[10];

    // Declare local_response_buffer with size (MAX_HTTP_OUTPUT_BUFFER + 1) to prevent out of bound access when
    // it is used by functions like strlen(). The buffer should only be used upto size MAX_HTTP_OUTPUT_BUFFER
    char local_response_buffer[MAX_HTTP_OUTPUT_BUFFER + 1];

    char *header = calloc(HTTP_POST_REQUEST_HEADER_SIZE + 1, sizeof(char));
    char *body = calloc(HTTP_POST_REQUEST_BODY_SIZE + 1, sizeof(char));
    char *temp_buffer = calloc(128 + 1, sizeof(char));

    while (1)
    {
        err = ESP_OK;

        // clearing the buffer
        memset(local_response_buffer, 0, MAX_HTTP_OUTPUT_BUFFER + 1);
        memset(header, 0, HTTP_POST_REQUEST_HEADER_SIZE);
        memset(body, 0, HTTP_POST_REQUEST_BODY_SIZE);
        memset(temp_buffer, 0, 128);

        // upload to server
        ESP_LOGI(TAG, "Uploading jpeg to server: " SERVER_ADDRESS);

        fb_len = 0;
        fr_start = esp_timer_get_time();
        ESP_LOGI(TAG, "The rfid tag: %" PRIu64, serial_number);

        /**
         * NOTE: All the configuration parameters for http_client must be spefied either in URL or as host and path parameters.
         * If host and path parameters are not set, query parameter will be ignored. In such cases,
         * query parameter should be specified in URL.
         *
         * If URL as well as host and path parameters are specified, values of host and path will be considered.
         */
        esp_http_client_config_t config = {
            .url = "http://" SERVER_ADDRESS "/post",
            .method = HTTP_METHOD_POST,
            .event_handler = _http_event_handler,
            .user_data = local_response_buffer, // Pass address of local buffer to get response
            .disable_auto_redirect = true,
        };
        esp_http_client_handle_t client = esp_http_client_init(&config);

        // if the frame buffer is a nullptr, we cannot proceed for upload
        // same for client == null
        if (!fb || !client)
        {
            esp_http_client_cleanup(client);
            break;
        }

        ESP_LOGI(TAG, "Performing a http POST request");

        // some header
        sprintf(temp_buffer, "%" PRIu64 "", serial_number);
        esp_http_client_set_header(client, "rfid-serial-number", temp_buffer);
        esp_http_client_set_header(client, "Content-Type", _STREAM_CONTENT_TYPE);

        // setup to send data as chunk
        esp_http_client_open(client, -1); // write_len=-1 sets header "Transfer-Encoding: chunked" and method to POST

        // header stuffs
        sprintf(temp_buffer, "POST %s HTTP/1.1\r\n", "/post");
        strcpy(header, temp_buffer);
        sprintf(temp_buffer, "Host: %s\r\n", SERVER_ADDRESS);
        strcat(header, temp_buffer);
        sprintf(temp_buffer, "User-Agent: esp-idf/%d.%d.%d esp32\r\n", ESP_IDF_VERSION_MAJOR, ESP_IDF_VERSION_MINOR, ESP_IDF_VERSION_PATCH);
        strcat(header, temp_buffer);
        sprintf(temp_buffer, "Accept: */*\r\n");
        strcat(header, temp_buffer);
        sprintf(temp_buffer, _STREAM_CONTENT_TYPE);
        strcat(header, _CONTENT_TYPE_HEADER);
        sprintf(temp_buffer, "rfid-serial-number: %" PRIu64 "\r\n", serial_number);
        strcat(header, temp_buffer);

        ESP_LOGI(TAG, "the sent header is %s", header);

        // the length of header in hex
        hlen = snprintf(chunk_len_hex, sizeof(chunk_len_hex), "%X", strlen(header));
        esp_http_client_write(client, strcat(chunk_len_hex, "\r\n"), hlen + 2); // assuming hlen < 8

        // send header
        esp_http_client_write(client, header, HTTP_POST_REQUEST_HEADER_SIZE);
        send_crlf(client);

        // start body
        strcpy(body, _STREAM_BOUNDARY);                       // boundary start
        sprintf(temp_buffer, _CONTENT_DISPOSITION, filename); // contentent disposition with filename
        strcat(body, temp_buffer);                            // concatenating to the body
        sprintf(temp_buffer, "Content-Type: application/octet-stream\r\n\r\n");
        strcat(body, temp_buffer);

        ESP_LOGI(TAG, "The sent body is %s", body);

        // the length of body in hex
        hlen = snprintf(chunk_len_hex, sizeof(chunk_len_hex), "%X", strlen(body));
        esp_http_client_write(client, strcat(chunk_len_hex, "\r\n"), hlen + 2); // assuming hlen < 8

        // send body
        esp_http_client_write(client, body, HTTP_POST_REQUEST_BODY_SIZE);
        send_crlf(client);

        if (err == ESP_OK)
        {
            if (fb->format == PIXFORMAT_JPEG)
            {
                fb_len = fb->len;
                // the length of frame buffer in hex
                hlen = snprintf(chunk_len_hex, sizeof(chunk_len_hex), "%X", fb_len);
                esp_http_client_write(client, strcat(chunk_len_hex, "\r\n"), hlen + 2); // assuming hlen < 8
                if (-1 == esp_http_client_write(client, (char *)fb->buf, fb_len))
                { // the frame buffer
                    ESP_LOGE(TAG, "Couldn't write frame buffer.");
                }
            }
            else
            {
                uint8_t *jpeg_buffer;
                if (ESP_OK == err && frame2jpg(fb, 80, &jpeg_buffer, &fb_len))
                {
                    // the length of frame buffer in hex
                    hlen = snprintf(chunk_len_hex, sizeof(chunk_len_hex), "%X", fb_len);
                    esp_http_client_write(client, strcat(chunk_len_hex, "\r\n"), hlen + 2); // assuming hlen < 8
                    if (-1 == esp_http_client_write(client, (char *)jpeg_buffer, fb_len))
                    { // the frame buffer
                        ESP_LOGE(TAG, "Couldn't write frame buffer.");
                    }
                }
            }
            send_crlf(client);
        }

        // the length of stream boundary in hex
        hlen = snprintf(chunk_len_hex, sizeof(chunk_len_hex), "%X", strlen(_STREAM_BOUNDARY));
        esp_http_client_write(client, strcat(chunk_len_hex, "\r\n"), hlen + 2); // assuming hlen < 8
        esp_http_client_write(client, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        send_crlf(client);

        // end
        err = esp_http_client_write(client, "0\r\n", 3);
        send_crlf(client);
        // // converting fb_len to string
        // char fb_len_str[(size_t)((ceil(log10(fb_len)) + 1) * sizeof(char))];
        // sprintf(fb_len_str, "%zu", fb_len);

        // esp_http_client_set_header(client, "Content-Length", fb_len_str);
        // err = esp_http_client_perform(client);
        if (err == ESP_OK)
        {
            ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %" PRId64,
                     esp_http_client_get_status_code(client),
                     esp_http_client_get_content_length(client));
            int64_t fr_end = esp_timer_get_time();
            ESP_LOGI(TAG, "JPG: %luKB %lums", (uint32_t)(fb_len / 1024), (uint32_t)((fr_end - fr_start) / 1000));

            break;
        }
        else
        {
            ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
            retry += 1;

            if (retry > UPLOAD_RETRY_COUNT)
                break;
        }
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
    }
    free(fb); // clearing the frame buffer that we created
    free(body);
    free(temp_buffer);
    vTaskDelete(NULL);
}