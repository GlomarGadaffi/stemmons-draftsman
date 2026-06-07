/*
 * SPDX-FileCopyrightText: 2026 GlomarGadaffi
 * SPDX-License-Identifier: Apache-2.0
 *
 * STUB — to be implemented. microSD (SD_MMC 1-bit) mount + file listing.
 */
#include "demos.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "demo.sdcard";

void demo_sdcard(ui_t *ui, esp_lcd_touch_handle_t tp)
{
    ESP_LOGI(TAG, "microSD demo (stub) — tap the back bar to exit");
    ui_fill(ui, ui_rgb(0, 0, 40));
    ui_back_bar(ui);
    ui_flush(ui);

    uint16_t x, y;
    while (1) {
        if (board_touch(tp, &x, &y) && ui_in_back(x, y)) return;
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}
