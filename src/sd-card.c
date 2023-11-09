#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"
#include "sdmmc_cmd.h"
#include "esp_err.h"
#include "esp_vfs_fat.h"
#include "esp_log.h"

// local includes

#include "globals.h"
#include "sd-card.h"

// ---------------

#define SDCARD_CS (13)  // the chip select pin for sdcard

#define MOUNT_POINT "/sdcard"

esp_err_t init_sd_card(sdmmc_card_t **out)
{

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

