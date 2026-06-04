/*
 * SPDX-FileCopyrightText: 2026 GlomarGadaffi
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal, self-contained bring-up demo for the Guition JC3248W535EN
 * (ESP32-S3 + AXS15231B QSPI LCD, 320x480). No LVGL — just the esp_lcd driver,
 * the LEDC PWM backlight, and a solid-color test pattern, so you can confirm the
 * panel is alive before layering a UI on top.
 *
 * THE ONE THING that wastes everyone a day: this driver's disp_on_off() handler
 * has INVERTED semantics. See the call near the bottom of app_main().
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_axs15231b.h"

static const char *TAG = "jc3248w535";

/* ---- Board pins (Guition JC3248W535EN, verified against the board pinout) ---- */
#define LCD_H_RES   320
#define LCD_V_RES   480
#define PIN_CS      45
#define PIN_SCK     47
#define PIN_D0      21
#define PIN_D1      48
#define PIN_D2      40
#define PIN_D3      39
#define PIN_BL       1   /* backlight — PWM driven, NOT a plain GPIO level */
#define LCD_HOST    SPI2_HOST

/* ---- Backlight: the BL pin is PWM-driven. A bare gpio level looks dim. ---- */
static void backlight_init(void)
{
    ledc_timer_config_t t = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num       = LEDC_TIMER_1,
        .freq_hz         = 5000,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&t));
    ledc_channel_config_t c = {
        .gpio_num   = PIN_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_0,
        .timer_sel  = LEDC_TIMER_1,
        .intr_type  = LEDC_INTR_DISABLE,
        .duty       = 1023,   /* 100% on a 10-bit resolution */
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&c));
}

void app_main(void)
{
    ESP_LOGI(TAG, "JC3248W535EN / AXS15231B QSPI bring-up demo");

    backlight_init();

    /* ---- QSPI bus ---- */
    const spi_bus_config_t buscfg = AXS15231B_PANEL_BUS_QSPI_CONFIG(
        PIN_SCK, PIN_D0, PIN_D1, PIN_D2, PIN_D3,
        LCD_H_RES * LCD_V_RES * sizeof(uint16_t));
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    /* ---- Panel IO (QSPI) ---- */
    esp_lcd_panel_io_handle_t io = NULL;
    const esp_lcd_panel_io_spi_config_t io_cfg =
        AXS15231B_PANEL_IO_QSPI_CONFIG(PIN_CS, NULL, NULL);
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &io));

    /* ---- Panel driver (uses the built-in JC3248W535 init sequence) ---- */
    axs15231b_vendor_config_t vendor = { .flags = { .use_qspi_interface = 1 } };
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = -1,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config  = &vendor,
    };
    esp_lcd_panel_handle_t panel = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_axs15231b(io, &panel_cfg, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));

    /*
     * !!!!!!!!!!!!!!!!!!!!!!!!!!  READ THIS  !!!!!!!!!!!!!!!!!!!!!!!!!!
     * This driver's disp_on_off() handler is INVERTED: its bool argument
     * means "off", not the usual esp_lcd "on". esp_lcd passes the value
     * straight through, so you MUST pass `false` to turn the display ON.
     * Passing `true` sends DISPOFF and you get a perfectly black, perfectly
     * backlit panel no matter what you write to GRAM. This single line is
     * the #1 reason this board appears "dead".
     */
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, false)); /* false == ON here */

    /* ---- Solid-color test: prove the panel + backlight + draw path ---- */
    uint16_t *fb = heap_caps_malloc(LCD_H_RES * LCD_V_RES * sizeof(uint16_t),
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_ERROR_CHECK(fb ? ESP_OK : ESP_ERR_NO_MEM);

    const uint16_t colors[] = { 0xF800, 0x07E0, 0x001F, 0xFFFF, 0x0000 }; /* R G B W K */
    const char    *names[]  = { "RED", "GREEN", "BLUE", "WHITE", "BLACK" };
    int i = 0;
    while (1) {
        /* AXS15231B expects big-endian RGB565, so byte-swap each pixel. */
        uint16_t c = (uint16_t)((colors[i] >> 8) | (colors[i] << 8));
        for (int p = 0; p < LCD_H_RES * LCD_V_RES; p++) fb[p] = c;
        /* Draw top-to-bottom in bands: the driver issues RAMWR on the first
         * band (y==0) then RAMWRC continuation for the rest. */
        for (int y = 0; y < LCD_V_RES; y += 80)
            esp_lcd_panel_draw_bitmap(panel, 0, y, LCD_H_RES, y + 80, &fb[y * LCD_H_RES]);
        ESP_LOGI(TAG, "screen = %s", names[i]);
        i = (i + 1) % (sizeof(colors) / sizeof(colors[0]));
        vTaskDelay(pdMS_TO_TICKS(1500));
    }
}
