#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * For proto-typing, esp32cam and rc522 both use SPI, so both cannot be enabled at once.
 */
#define USE_ESP32CAM 1
#define USE_RC522 0
#define USE_SDCARD 0

    static const char *TAG = "RFID Based Attendance System";

// priorities of various tasks
// main task has priority 1
#define OTA_TASK_PRIORITY (UBaseType_t)2            // ota task must be handled first
#define CAMERA_FEED_TASK_PRIORITY (UBaseType_t)3    // less priority than the main task
#define REGISTER_PHOTO_TASK_PRIORITY (UBaseType_t)4 // less priority than pushing the video to screen
#define UPLOAD_JPEG_TASK_PRIORITY (UBaseType_t)5    // least priority of them all

// for http client
#define MAX_HTTP_RECV_BUFFER 512
#define MAX_HTTP_OUTPUT_BUFFER 1024 // since only post request is needed

// stack sizes of tasks
#define TASK_CAMERA_FEED_STACK_SIZE 2048
#define TASK_REGISTER_PHOTO_STACK_SIZE 2048
#define TASK_UPLOAD_JPEG_STACK_SIZE 2048 + MAX_HTTP_OUTPUT_BUFFER + MAX_HTTP_RECV_BUFFER

// pinning these tasks to separate cores as camera feed task needs to run all the time
#define CAMERA_FEED_TASK_CORE_AFFINITY (UBaseType_t)1 // only this on separate core
#define REGISTER_PHOTO_TASK_CORE_AFFINITY (UBaseType_t)0
#define UPLOAD_JPEG_TASK_CORE_AFFINITY (UBaseType_t)0

// upload jpeg related
#define PING_COUNT 4
#define PING_TARGET "www.espressif.com"
#define SERVER_ADDRESS "192.168.1.112:8000" //testing locally 

// full filepath of image
#define IMAGE_FILEPATH_LENGTH 50

#define SERVER_COMMON_NAME "esp32localserver.com" // To avoid CN error in https for self signed certificates

// for providing ota via publicly accessible servers
// 1 -> bundle certificate and use that
// 0 -> use the provided certificate
#define USE_CA_CERTIFICATE_BUNDLE 0

#if USE_CA_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#else
extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_ca_cert_pem_end");
#endif

/*
 * SPI for sdcard of esp32-cam and rc522
 */
#define SPI_MISO 2
#define SPI_MOSI 15
#define SPI_SCLK 14

#ifdef __cplusplus
}
#endif
