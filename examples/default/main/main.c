/**
 * @file main.c
 * @brief Basic example for the RV-3028-C7 component.
 *
 * Copyright (c) 2026 Javier Salvador
 *
 * This file is distributed under the MIT License as described in LICENSE.md.
 */

#include <inttypes.h>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sdkconfig.h>

#include "rv-3028-c7.h"

#define INITIAL_UNIX_TIME UINT32_C(1784462400) // 2026-07-19 12:00:00 UTC

static const char *TAG = "rv3028_example";

void app_main(void)
{
    i2c_dev_t rtc = {0};
    struct tm initial_time = {
        .tm_sec = 0,
        .tm_min = 0,
        .tm_hour = 12,
        .tm_mday = 19,
        .tm_mon = 7 - 1,
        .tm_year = 2026 - 1900,
        .tm_wday = 0,
        .tm_isdst = -1,
    };
    uint8_t id;

    ESP_ERROR_CHECK(i2cdev_init());
    ESP_ERROR_CHECK(rv3028_init_desc(
        &rtc, (i2c_port_t)CONFIG_EXAMPLE_I2C_PORT,
        (gpio_num_t)CONFIG_EXAMPLE_I2C_SDA,
        (gpio_num_t)CONFIG_EXAMPLE_I2C_SCL));

    ESP_ERROR_CHECK(rv3028_get_id(&rtc, &id));
    ESP_LOGI(TAG, "Module ID: 0x%02x", id);

    ESP_ERROR_CHECK(rv3028_set_24H(&rtc));
    ESP_ERROR_CHECK(rv3028_set_time(&rtc, &initial_time));
    ESP_ERROR_CHECK(rv3028_set_unix_time(&rtc, INITIAL_UNIX_TIME));

    TickType_t last_wake = xTaskGetTickCount();

    ESP_LOGI(TAG, "Calendar time and UNIX time are independent");
    while (1)
    {
        struct tm timeinfo;
        uint32_t unix_time;

        ESP_ERROR_CHECK(rv3028_get_time(&rtc, &timeinfo));
        ESP_ERROR_CHECK(rv3028_get_unix_time(&rtc, &unix_time));

        ESP_LOGI(TAG,
                 "%04d-%02d-%02d %02d:%02d:%02d | UNIX: %" PRIu32,
                 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1,
                 timeinfo.tm_mday, timeinfo.tm_hour,
                 timeinfo.tm_min, timeinfo.tm_sec, unix_time);

        xTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1000));
    }
}
