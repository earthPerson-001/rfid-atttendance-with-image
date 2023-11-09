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

    static const char *TAG = "RFID Based Attendance System";

/*
 * SPI for sdcard of esp32-cam and rc522
 */
#define SPI_MISO 2
#define SPI_MOSI 15
#define SPI_SCLK 14

#ifdef __cplusplus
}
#endif
