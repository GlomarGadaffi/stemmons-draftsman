/*
 * SPDX-FileCopyrightText: 2026 GlomarGadaffi
 * SPDX-License-Identifier: Apache-2.0
 */
#include "ui.h"
#include "font8x8.h"
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define BAND_ROWS 80   /* flush in 80-row bands (480 % 80 == 0) */

/* The framebuffer lives in PSRAM; DMAing straight from it to the panel can
 * underflow the SPI TX FIFO when another bus master (e.g. SDMMC) contends for
 * PSRAM bandwidth, wedging the LCD bus. So we copy each band into a small
 * internal-RAM bounce buffer and DMA from there, waiting for each band's
 * transfer to finish (via s_flush_sem) before reusing the buffer. */
static uint16_t *s_bounce;            /* one band, internal DMA-capable RAM */
static SemaphoreHandle_t s_flush_sem; /* given by ui_color_trans_done */

bool ui_color_trans_done(esp_lcd_panel_io_handle_t io,
                         esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    (void)io; (void)edata; (void)user_ctx;
    BaseType_t hp = pdFALSE;
    if (s_flush_sem) xSemaphoreGiveFromISR(s_flush_sem, &hp);
    return hp == pdTRUE;
}

void ui_init(ui_t *ui)
{
    (void)ui;
    if (!s_flush_sem) s_flush_sem = xSemaphoreCreateBinary();
    if (!s_bounce) {
        s_bounce = heap_caps_malloc(LCD_H_RES * BAND_ROWS * sizeof(uint16_t),
                                    MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    }
    if (!s_bounce || !s_flush_sem) {
        ESP_LOGE("ui", "bounce/sem alloc failed; flush will fall back to PSRAM DMA");
    }
}

uint16_t ui_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t v = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    return (uint16_t)((v >> 8) | (v << 8));   /* AXS15231B expects big-endian RGB565 */
}

void ui_fill(ui_t *ui, uint16_t color)
{
    for (int i = 0; i < LCD_H_RES * LCD_V_RES; i++) ui->fb[i] = color;
}

void ui_rect(ui_t *ui, int x, int y, int w, int h, uint16_t color)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > LCD_H_RES) w = LCD_H_RES - x;
    if (y + h > LCD_V_RES) h = LCD_V_RES - y;
    for (int yy = y; yy < y + h; yy++) {
        uint16_t *row = &ui->fb[yy * LCD_H_RES];
        for (int xx = x; xx < x + w; xx++) row[xx] = color;
    }
}

void ui_text(ui_t *ui, int x, int y, const char *s, int scale, uint16_t color)
{
    if (scale < 1) scale = 1;
    int cx = x;
    for (const char *p = s; *p; p++) {
        unsigned char ch = (unsigned char)*p;
        if (ch >= 128) ch = '?';
        const unsigned char *g = font8x8_basic[ch];
        for (int row = 0; row < 8; row++) {
            unsigned char bits = g[row];
            for (int col = 0; col < 8; col++) {
                if (bits & (1u << col))
                    ui_rect(ui, cx + col * scale, y + row * scale, scale, scale, color);
            }
        }
        cx += 8 * scale;
    }
}

int ui_text_w(const char *s, int scale)
{
    if (scale < 1) scale = 1;
    return (int)strlen(s) * 8 * scale;
}

void ui_flush(ui_t *ui)
{
    /* Full-screen, top-to-bottom in bands: matches the QSPI driver's
     * RAMWR(top band) + RAMWRC(continuation) scheme. */
    if (!s_bounce || !s_flush_sem) {
        /* Fallback (alloc failed): DMA straight from PSRAM. */
        for (int y = 0; y < LCD_V_RES; y += BAND_ROWS)
            esp_lcd_panel_draw_bitmap(ui->panel, 0, y, LCD_H_RES, y + BAND_ROWS, &ui->fb[y * LCD_H_RES]);
        return;
    }
    for (int y = 0; y < LCD_V_RES; y += BAND_ROWS) {
        memcpy(s_bounce, &ui->fb[y * LCD_H_RES], LCD_H_RES * BAND_ROWS * sizeof(uint16_t));
        /* Only wait for completion if the color transfer was actually queued.
         * draw_bitmap propagates errors and may return before issuing tx_color
         * (e.g. CASET failed) — in that case no on_color_trans_done fires, so an
         * unconditional take would block forever. ESP_OK <=> a give is coming. */
        if (esp_lcd_panel_draw_bitmap(ui->panel, 0, y, LCD_H_RES, y + BAND_ROWS, s_bounce) == ESP_OK)
            xSemaphoreTake(s_flush_sem, portMAX_DELAY);
    }
}

void ui_back_bar(ui_t *ui)
{
    ui_rect(ui, 0, 0, LCD_H_RES, UI_BACK_H, ui_rgb(60, 60, 60));
    ui_text(ui, 12, (UI_BACK_H - 8 * 2) / 2, "< BACK", 2, ui_rgb(255, 255, 255));
}

bool ui_in_back(uint16_t x, uint16_t y)
{
    (void)x;
    return y < UI_BACK_H;
}

bool board_touch(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y)
{
    if (esp_lcd_touch_read_data(tp) != ESP_OK) return false;
    esp_lcd_touch_point_data_t pt[1];
    uint8_t n = 0;
    if (esp_lcd_touch_get_data(tp, pt, &n, 1) == ESP_OK && n > 0) {
        *x = pt[0].x;
        *y = pt[0].y;
        return true;
    }
    return false;
}
