#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"
#include "driver/gpio.h"
#include "sys/stat.h"
#include "sys/time.h"
#include "unistd.h"

#include "sdmmc_cmd.h"

#include "esp_err.h"
#include "esp_vfs_fat.h"
#include "esp_log.h"

// local includes

#include "globals.h"
#include "sd-card.h"

// ---------------

#define SDCARD_CS (13) // the chip select pin for sdcard

#define MOUNT_POINT "/sdcard"
#define IMAGES_FOLDER "images"
#define REMAINING_UPLOAD_FILE "remaining_upload.csv"
#define REMAINING_UPLOAD_FILE_LINE_WIDTH 100

esp_err_t init_sd_card(sdmmc_card_t **out)
{
    // the sda pin should be pulled up to deselect any of the devices
    // but this doesn't solve the problem, hardware pull register might be required
    // https://stackoverflow.com/questions/73178340/esp32-cam-sd-and-camera-use-up-all-the-pins
    gpio_pullup_en(SDCARD_CS);
    gpio_set_pull_mode(SDCARD_CS, GPIO_PULLUP_ONLY);
    // To use 1-line SD mode, uncomment the following line:
    // slot_config.width = 1;

    // debug: to check if this will solve sd mount issue
    // for esp32cam board

    // GPIOs 15, 2, 4, 12, 13 should have external 10k pull-ups.
    // Internal pull-ups are not sufficient. However, enabling internal pull-ups
    // does make a difference some boards, so we do that here.
    gpio_set_pull_mode(15, GPIO_PULLUP_ONLY); // CMD, needed in 4- and 1- line modes
    gpio_set_pull_mode(2, GPIO_PULLUP_ONLY);  // D0, needed in 4- and 1-line modes
    // gpio_set_pull_mode(4, GPIO_PULLUP_ONLY);  // D1, needed in 4-line mode only
    gpio_set_pull_mode(12, GPIO_PULLUP_ONLY); // D2, needed in 4-line mode only
    gpio_set_pull_mode(13, GPIO_PULLUP_ONLY); // D3, needed in 4- and 1-line modes

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SPI_MOSI,
        .miso_io_num = SPI_MISO,
        .sclk_io_num = SPI_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    esp_err_t ret = spi_bus_initialize(host.slot, &bus_cfg, SPI2_HOST);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize bus.");
        return ret;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SDCARD_CS;
    slot_config.host_id = host.slot;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024};

    sdmmc_card_t *card;
    ret = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK)
    {
        return ret;
    }
    sdmmc_card_print_info(stdout, card);
    *out = card;

    return ESP_OK;
}

esp_err_t deinit_sd_card(sdmmc_card_t *card)
{
    esp_err_t ret;

    // All done, unmount partition and disable SPI peripheral
    ret = esp_vfs_fat_sdcard_unmount(MOUNT_POINT, card);
    ESP_LOGI(TAG, "Card unmounted");

    if (ESP_OK == ret)
    {
        // deinitialize the bus after all devices are removed
        ret = spi_bus_free(card->host.slot);
    }

    return ret;
}

/**
 * Get time in microseconds and use it to construct file name
 * The out_filepath must be a valid buffer in which filepath is stored (length of 50 char might include all cases)
 */
esp_err_t get_new_image_filepath(uint64_t img_identifier, char *extension, char *out_filepath, size_t out_filepath_length)
{
    if (out_filepath == NULL)
    {
        ESP_LOGE(TAG, "out_filepath buffer cannot be NULL, it must point to already initialized char buffer.");
        return ESP_ERR_INVALID_ARG;
    }

    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);
    int64_t time_us = (int64_t)tv_now.tv_sec * 1000000L + (int64_t)tv_now.tv_usec;
    //                                      /sdcard/images/<rfid_tag>_<time_us>.jpg
    snprintf(out_filepath, out_filepath_length, "%s/%" PRIu64 "_%" PRIi64 "%s", get_images_folder(), img_identifier, time_us, extension);

    return ESP_OK;
}

/**
 * Creates the /sdcard/images directory if it doesn't exists
 * returns the images folder path.
 *
 */
char *get_images_folder()
{
    // since this is a string literal, it's lifetime is static
    char *images_folder = MOUNT_POINT "/" IMAGES_FOLDER;

    // checking if folder exists
    if (0 != access(images_folder, F_OK))
    {
        int mk_ret = mkdir(images_folder, 0775);
        ESP_LOGI(TAG, "mkdir %s returned %d", images_folder, mk_ret);
    }

    // todo: check necessary read/write permissions

    return images_folder;
}

/**
 * Creates the /sdcard/remaining_upload.csv file if it doesn't exists
 * returns the remaining_upload.csv path
 *
 */
char *get_remaining_upload_file()
{
    // since this is a string literal, it's lifetime is static
    char *remaining_upload_file = MOUNT_POINT "/" REMAINING_UPLOAD_FILE;

    // checking if the file exists
    if (0 != access(remaining_upload_file, F_OK))
    {
        FILE *fp = fopen(remaining_upload_file, "w+");

        if (fp == NULL)
        {
            ESP_LOGE(TAG, "Couldn't open file %s", remaining_upload_file);
            return NULL;
        }

        char *a_line = calloc(sizeof(char), REMAINING_UPLOAD_FILE_LINE_WIDTH);
        ESP_LOGI(TAG, "The previous content of %s", remaining_upload_file);
        // displaying the content of the csv file
        while (fgets(a_line, REMAINING_UPLOAD_FILE_LINE_WIDTH, fp))
        {
            ESP_LOGI(TAG, "%s", a_line);
        }

        free(a_line);
        fclose(fp);
    }

    return remaining_upload_file;
}

esp_err_t write_to_file_path(const char *path, const char *data, char *mode)
{
    if (mode == NULL)
    {
        mode = "w";
    }

    ESP_LOGI(TAG, "Opening file %s", path);
    FILE *f = fopen(path, mode);
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

esp_err_t save_image_to_sdcard(uint8_t *image_buffer, uint64_t rfid_serial_number)
{
    // create filename combining the rfid_tag and current timestamp
    char full_img_filepath[IMAGE_FILEPATH_LENGTH];
    get_new_image_filepath(rfid_serial_number, ".jpg", full_img_filepath, IMAGE_FILEPATH_LENGTH);

    // checking if the image already exist
    struct stat st;

    if (stat(full_img_filepath, &st) == 0)
        ESP_LOGE(TAG, "The image %s already exists.", full_img_filepath);
    return ESP_FAIL;

    esp_err_t ret = ESP_OK;
    // else we can write the new image
    if (ESP_OK == (ret = write_to_file_path(full_img_filepath, (char *)image_buffer, "w")))
    {
        ESP_LOGI(TAG, "Successfully saved image %s.", full_img_filepath);

        // getting the timestamp
        // full_img_filepath is in the form : /sdcard/images/<rfid_tag>_<time_us>.jpg
        char *tok;
        char *timestamp_us;
        tok = strtok(full_img_filepath, "_");
        while (tok != 0)
        {
            timestamp_us = tok; 
            tok = strtok(NULL, "_");
        }

        // since this needs to be serialized again, there is no point in converting back to int64_t
        // assuming the last value is <time_us>.jpg and taking the first slice
        timestamp_us = strtok(timestamp_us, "."); 

        // log to file for uploading later
        const char *remaining_upload_file = get_remaining_upload_file();

        // creating a buffer to write a line at once
        char *line_buf = calloc(sizeof(char), REMAINING_UPLOAD_FILE_LINE_WIDTH);

        // the csv file will be in the form of: timestamp_us,rfid_serial_number,image_path
        sprintf(line_buf, timestamp_us);
        strcat(line_buf, ",");
        stract(line_buf, "%" PRIu64 ",%s\n", rfid_serial_number, full_img_filepath);
        write_to_file_path(remaining_upload_file, line_buf, "w+");
        ESP_LOGI(TAG, "Wrote %s to %s", line_buf, remaining_upload_file);

        free(line_buf);
    }

    return ret;
}
