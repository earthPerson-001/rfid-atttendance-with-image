#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

    struct esp_err_t;

    esp_err_t init_sd_card(sdmmc_card_t **out);

    esp_err_t deinit_sd_card(sdmmc_card_t *card);

    esp_err_t get_new_image_filepath(uint64_t img_identifier, char *extension, char *out_filepath, size_t out_filepath_length);

    char *get_images_folder();

    esp_err_t save_image_to_sdcard(uint8_t *image_buffer, int64_t rfid_serial_number);

#ifdef __cplusplus
}
#endif