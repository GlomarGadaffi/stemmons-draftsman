/*
 * SPDX-FileCopyrightText: 2026 GlomarGadaffi
 * SPDX-License-Identifier: Apache-2.0
 *
 * Touch-paint demo: paint a small square wherever the user touches. Tap the top
 * back bar to return to the home menu. Uses the tiny software-framebuffer UI
 * (no LVGL); the panel and touch controller are already initialized by the
 * caller, so there is nothing to set up or tear down here.
 */
#include "demos.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "demo.touchpaint";

#define BRUSH 16   /* width/height of the painted square, in pixels */

/* Cycle a lively color through the HSV-ish color wheel as strokes accumulate. */
static uint16_t paint_color(uint32_t step)
{
    uint8_t phase = (uint8_t)(step * 11);   /* advance the hue each paint */
    uint8_t seg = phase / 43;               /* 6 segments across 0..255  */
    uint8_t t = (uint8_t)((phase % 43) * 6);/* 0..255 ramp within segment */
    uint8_t r = 0, g = 0, b = 0;
    switch (seg) {
        case 0: r = 255;     g = t;       b = 0;       break;
        case 1: r = 255 - t; g = 255;     b = 0;       break;
        case 2: r = 0;       g = 255;     b = t;       break;
        case 3: r = 0;       g = 255 - t; b = 255;     break;
        case 4: r = t;       g = 0;       b = 255;     break;
        default:r = 255;     g = 0;       b = 255 - t; break;
    }
    return ui_rgb(r, g, b);
}

void demo_touchpaint(ui_t *ui, esp_lcd_touch_handle_t tp)
{
    ESP_LOGI(TAG, "touch-paint demo started — tap the back bar to exit");

    ui_fill(ui, ui_rgb(0, 0, 0));
    ui_back_bar(ui);
    ui_flush(ui);

    uint32_t strokes = 0;
    uint16_t x, y;
    while (1) {
        if (board_touch(tp, &x, &y)) {
            if (ui_in_back(x, y)) {
                ESP_LOGI(TAG, "back bar tapped — exiting (%lu paints)",
                         (unsigned long)strokes);
                return;
            }

            /* Center the brush on the touch, clamped to the canvas below the
             * back bar so we never overwrite it (and never run off-screen). */
            int px = (int)x - BRUSH / 2;
            int py = (int)y - BRUSH / 2;
            if (px < 0)                  px = 0;
            if (px > LCD_H_RES - BRUSH)  px = LCD_H_RES - BRUSH;
            if (py < UI_BACK_H)          py = UI_BACK_H;
            if (py > LCD_V_RES - BRUSH)  py = LCD_V_RES - BRUSH;

            ui_rect(ui, px, py, BRUSH, BRUSH, paint_color(strokes));
            ui_flush(ui);

            ESP_LOGI(TAG, "paint #%lu at (%u,%u) -> rect (%d,%d)",
                     (unsigned long)strokes, x, y, px, py);
            strokes++;
        }
        vTaskDelay(pdMS_TO_TICKS(30));   /* ~33 Hz sampling */
    }
}
