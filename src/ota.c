#include "cJSON.h"
#include <sys/param.h>

#include "esp_ota_ops.h"
#include "esp_https_ota.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_tls.h"

// Local includes

#include "globals.h"
#include "ota.h"

//

#define CURRENT_OTA_CHANNEL OTA_CHANNEL_APLHA
#define VERSION_LEN 15
#define TEMP_BUFFER_SIZE 20
#define URL_BUFFER_SIZE 100

static const char *SERVER_URL_WITH_PLACEHOLDER = "https://" SERVER_ADDRESS "/%s";

typedef struct version_t
{
    uint8_t major;
    uint8_t minor;
    uint8_t patch;
} version_t;

/**
 * Splits the version like 0.10.1 into
 * ```
 version_t {
    major: 0,
    minor: 10,
    patch: 1
}
 ```
 The struct is saved on stack
*/
version_t split_to_versions(char *whole_version)
{
    char *tok = strtok(whole_version, ".");
    uint8_t major = atoi(tok);
    tok = strtok(NULL, ".");
    uint8_t minor = atoi(tok);
    tok = strtok(NULL, ".");
    uint8_t patch = atoi(tok);

    version_t ver = {.major = major, .minor = minor, .patch = patch};
    return ver;
}

// function declaration
esp_err_t start_ota(const char *firmware_url);

/**
 * For handling the downloading of manifest.json from ota server
 *
 */
static esp_err_t ota_manifest_http_event_handler(esp_http_client_event_t *evt)
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
            esp_app_desc_t *app_desc = malloc(sizeof(esp_app_desc_t));
            esp_ota_get_partition_description(esp_ota_get_running_partition(), app_desc);

            // keeping track of best versions
            version_t best_vesion = {
                .major = 0,
                .minor = 0,
                .patch = 0};
            char *firmware_url_of_the_best_version = NULL;

            // current version ignoring other tags
            char *current_version_string = calloc(sizeof(char), VERSION_LEN + 1);
            size_t current_version_len = (size_t)((strchr(app_desc->version, '-')) - (app_desc->version + 1));

            // getting all the string between 'v' and first '-'(excluding '-')
            strncpy(current_version_string, app_desc->version + 1, current_version_len);

            version_t cur_ver = split_to_versions(current_version_string);

            // the obtained output_buffer is manifest.json string

            // parsing the string
            cJSON *json = cJSON_Parse(output_buffer);
            cJSON *entry;
            cJSON_ArrayForEach(entry, json)
            {
                cJSON *name = cJSON_GetObjectItemCaseSensitive(entry, "name");
                cJSON *build_type = cJSON_GetObjectItemCaseSensitive(entry, "build-type");
                cJSON *board = cJSON_GetObjectItemCaseSensitive(entry, "board");
                cJSON *firmware_url = cJSON_GetObjectItemCaseSensitive(entry, "firmware-url");
                cJSON *version_short = cJSON_GetObjectItemCaseSensitive(entry, "version-short");
                cJSON *version = cJSON_GetObjectItemCaseSensitive(entry, "version");
                cJSON *version_long = cJSON_GetObjectItemCaseSensitive(entry, "version-long");
                cJSON *criticality = cJSON_GetObjectItemCaseSensitive(entry, "criticality");

                version_t incoming_ver = split_to_versions(version->valuestring);

                // debug
                ESP_LOGI(TAG, "Ota name: %s", name->valuestring);
                ESP_LOGI(TAG, "Board: %s", board->valuestring);
                ESP_LOGI(TAG, "Current Version String: %s", current_version_string);
                ESP_LOGI(TAG, "Incoming Version string: %s", version_long->valuestring);
                ESP_LOGI(TAG, "Incoming Version Short: %lf", version_short->valuedouble);
                ESP_LOGI(TAG, "Incoming Version: %s", version->valuestring);
                ESP_LOGI(TAG, "Firmware Url: %s", firmware_url->valuestring);

                // skip if the short version is invalid or the version is same or ota is less critical than we desire
                if (!cJSON_IsNumber(version_short) || strcmp(version_long->valuestring, app_desc->version) == 0 || !cJSON_IsNumber(criticality) || criticality->valuedouble > OTA_ACCEPT_CRICICALITY_BELOW)
                {
                    continue;
                }

                // skip if the subscribed ota channel is of more stability then the update
                if ((CURRENT_OTA_CHANNEL == OTA_CHANNEL_STABLE && strcmp(build_type->valuestring, "stable") != 0) || (CURRENT_OTA_CHANNEL == OTA_CHANNEL_BETA && (strcmp(build_type->valuestring, "stable") != 0 && strcmp(build_type->valuestring, "beta") != 0)) || (CURRENT_OTA_CHANNEL == OTA_CHANNEL_APLHA && (strcmp(build_type->valuestring, "stable") != 0 && strcmp(build_type->valuestring, "beta") != 0 && strcmp(build_type->valuestring, "alpha") != 0)))
                {
                    continue;
                }

                // if the incoming version is less than the current one, there is not point in keeping track of it
                if (incoming_ver.major < cur_ver.major && incoming_ver.minor < cur_ver.minor && incoming_ver.patch <= cur_ver.patch)
                {
                    continue;
                }

                ESP_LOGI(TAG, "Current Version: %d.%d.%d, Incoming Version: %d.%d.%d",
                         cur_ver.major, cur_ver.minor, cur_ver.patch,
                         incoming_ver.major, incoming_ver.minor, incoming_ver.patch);

                // now comparing with the previous best versions
                if (best_vesion.major == 0 && best_vesion.minor == 0 /** Realese with only a patch is unlikely, so no checking for patch==0*/)
                {
                    best_vesion = cur_ver;
                }

                if (incoming_ver.major > cur_ver.major || (incoming_ver.major == cur_ver.major && incoming_ver.minor > cur_ver.minor) || (incoming_ver.major == cur_ver.major && incoming_ver.minor == cur_ver.minor && incoming_ver.patch > cur_ver.patch))
                {
                    best_vesion = cur_ver;
                    firmware_url_of_the_best_version = firmware_url->valuestring;
                }
                else
                {
                    continue;
                }
            }
            if (firmware_url_of_the_best_version != NULL)
            {
                char *whole_url = calloc(sizeof(char), URL_BUFFER_SIZE);
                snprintf(whole_url, URL_BUFFER_SIZE, SERVER_URL_WITH_PLACEHOLDER, firmware_url_of_the_best_version);

                if (ESP_OK == start_ota(whole_url))
                {
                    ESP_LOGI(TAG, "Ota Update successful, restarting...");
                    esp_restart();
                }
                else
                {
                    ESP_LOGE(TAG, "Ota Update Failed.");
                }
                free(whole_url);
            }
            else
            {
                ESP_LOGI(TAG, "Aborting ota update download. (Version constraint unsatisfied) \n\
                 Current version: %d.%d.%d ; Best available version: %d.%d.%d",
                         cur_ver.major, cur_ver.minor, cur_ver.patch,
                         best_vesion.major, best_vesion.minor, best_vesion.patch);
            }

            free(output_buffer);
            free(app_desc);
            free(current_version_string);
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

/**
 * For handling the actual firmware download from ota server
 * It is needed because the whole firmware data cannot be stored in buffer as its size maybe up to couple of MBs.
 */
static esp_err_t ota_fimmware_http_event_handler(esp_http_client_event_t *evt)
{
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
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
        break;
    case HTTP_EVENT_REDIRECT:
        ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
        break;
    }
    return ESP_OK;
}

esp_err_t start_ota(const char *firmware_url)
{
    ESP_LOGI(TAG, "Starting OTA upgrade task");

    // getting the manifest.json file
    esp_http_client_config_t config = {
        .url = firmware_url,
        .method = HTTP_METHOD_GET,
#if USE_CA_CERTIFICATE_BUNDLE
        .crt_bundle_attach = esp_crt_bundle_attach,
#else
        .cert_pem = (char *)server_cert_pem_start,
#endif /* USE_CA_CERTIFICATE_BUNDLE */
        .event_handler = ota_fimmware_http_event_handler,
        .keep_alive_enable = true,
        .common_name = SERVER_COMMON_NAME,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &config,
    };
    ESP_LOGI(TAG, "Attempting to download update from %s", config.url);

    return esp_https_ota(&ota_config);
}

void simple_ota_example_task(void *pvParameter)
{
    char *local_response_buffer = calloc(sizeof(char), MAX_HTTP_RECV_BUFFER + 1);
    char *url_buffer = calloc(sizeof(char), URL_BUFFER_SIZE + 1);

    snprintf(url_buffer, URL_BUFFER_SIZE, SERVER_URL_WITH_PLACEHOLDER, "manifest.json");

    // getting the manifest.json file
    esp_http_client_config_t config = {
        .url = url_buffer,
        .method = HTTP_METHOD_GET,
#if USE_CA_CERTIFICATE_BUNDLE
        .crt_bundle_attach = esp_crt_bundle_attach,
#else
        .cert_pem = (char *)server_cert_pem_start,
#endif /* USE_CA_CERTIFICATE_BUNDLE */
        .event_handler = ota_manifest_http_event_handler,
        .keep_alive_enable = true,
        .common_name = SERVER_COMMON_NAME,
    };

    // the ota_manifest_http_event_handler should take care of other stuffs
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %" PRId64,
                 esp_http_client_get_status_code(client),
                 esp_http_client_get_content_length(client));
    }
    else
    {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    }
    ESP_LOG_BUFFER_HEX(TAG, local_response_buffer, strlen(local_response_buffer));

    free(local_response_buffer);
    free(url_buffer);

    // delete for now, can if started again if required
    vTaskDelete(NULL);
}

static void print_sha256(const uint8_t *image_hash, const char *label)
{
    char hash_print[OTA_HASH_LEN * 2 + 1];
    hash_print[OTA_HASH_LEN * 2] = 0;
    for (int i = 0; i < OTA_HASH_LEN; ++i)
    {
        sprintf(&hash_print[i * 2], "%02x", image_hash[i]);
    }
    ESP_LOGI(TAG, "%s %s", label, hash_print);
}

static void get_sha256_of_partitions(void)
{
    uint8_t sha_256[OTA_HASH_LEN] = {0};
    esp_partition_t partition;

    // get sha256 digest for bootloader
    partition.address = ESP_BOOTLOADER_OFFSET;
    partition.size = ESP_PARTITION_TABLE_OFFSET;
    partition.type = ESP_PARTITION_TYPE_APP;
    esp_partition_get_sha256(&partition, sha_256);
    print_sha256(sha_256, "SHA-256 for bootloader: ");

    // get sha256 digest for running partition
    esp_partition_get_sha256(esp_ota_get_running_partition(), sha_256);
    print_sha256(sha_256, "SHA-256 for current firmware: ");
}

esp_err_t start_ota_process()
{
    get_sha256_of_partitions();

    return (pdPASS == xTaskCreate(&simple_ota_example_task, "ota_example_task", 8192, NULL, OTA_TASK_PRIORITY, NULL))
               ? ESP_OK
               : ESP_FAIL;
}
