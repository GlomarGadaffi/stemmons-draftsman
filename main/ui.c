/*
 * SPDX-FileCopyrightText: 2026 GlomarGadaffi
 * SPDX-License-Identifier: Apache-2.0
 */
#include "ui.h"

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

void ui_flush(ui_t *ui)
{
    /* Full-screen, top-to-bottom in 80-row bands: matches the QSPI driver's
     * RAMWR(top band) + RAMWRC(continuation) scheme. 480 % 80 == 0. */
    for (int y = 0; y < LCD_V_RES; y += 80)
        esp_lcd_panel_draw_bitmap(ui->panel, 0, y, LCD_H_RES, y + 80, &ui->fb[y * LCD_H_RES]);
}

void ui_back_bar(ui_t *ui)
{
    ui_rect(ui, 0, 0, LCD_H_RES, UI_BACK_H, ui_rgb(60, 60, 60));
    /* crude left-pointing "back" glyph */
    ui_rect(ui, 10, UI_BACK_H / 2 - 2, 26, 4, ui_rgb(255, 255, 255));
    ui_rect(ui, 10, UI_BACK_H / 2 - 8, 4, 16, ui_rgb(255, 255, 255));
}

bool ui_in_back(uint16_t x, uint16_t y)
{
    (void)x;
    return y < UI_BACK_H;
}

bool board_touch(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y)
{
    if (esp_lcd_touch_read_data(tp) != ESP_OK) return false;
    uint16_t xs[1], ys[1], ss[1];
    uint8_t n = 0;
    if (esp_lcd_touch_get_coordinates(tp, xs, ys, ss, &n, 1) && n > 0) {
        *x = xs[0];
        *y = ys[0];
        return true;
    }
    return false;
}
