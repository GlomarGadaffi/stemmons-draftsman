/*
 * SPDX-FileCopyrightText: 2026 GlomarGadaffi
 * SPDX-License-Identifier: Apache-2.0
 *
 * STUB — to be implemented. Touch-paint demo: draw where the user touches.
 */
#include "demos.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "demo.touchpaint";

void demo_touchpaint(ui_t *ui, esp_lcd_touch_handle_t tp)
{
    ESP_LOGI(TAG, "touch-paint demo (stub) — tap the back bar to exit");
    ui_fill(ui, ui_rgb(0, 0, 0));
    ui_back_bar(ui);
    ui_flush(ui);

    uint16_t x, y;
    while (1) {
        if (board_touch(tp, &x, &y) && ui_in_back(x, y)) return;
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}
