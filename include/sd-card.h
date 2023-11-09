#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

    struct esp_err_t;

    esp_err_t init_sd_card(sdmmc_card_t **out);

    esp_err_t deinit_sd_card(sdmmc_card_t *card);

#ifdef __cplusplus
}
#endif