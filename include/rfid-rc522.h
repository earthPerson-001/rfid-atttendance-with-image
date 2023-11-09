#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

    struct esp_err_t;
    struct rc522_handle_t;

    esp_err_t initialize_rc522(rc522_handle_t *out);

#ifdef __cplusplus
}
#endif
