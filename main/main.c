/*
 * SPDX-FileCopyrightText: 2026 GlomarGadaffi
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal, self-contained bring-up demo for the Guition JC3248W535EN
 * (ESP32-S3 + AXS15231B QSPI LCD, 320x480) plus its capacitive touch panel
 * (AXS15231B over I2C). No LVGL — just the esp_lcd driver, the LEDC PWM
 * backlight, a solid-color test pattern, and a touch read-out, so you can
 * confirm the display + backlight + touch are all alive before layering a UI.
 *
 * THE ONE THING that wastes everyone a day: the init table does NOT issue DISPON,
 * so you must call esp_lcd_panel_disp_on_off(panel, true) after init or the panel
 * stays black-but-backlit. See the call near the bottom of app_main().
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
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

/* Capacitive touch (AXS15231B, I2C). dc=-1 on the LCD's QSPI bus leaves GPIO8
 * free, so the touch controller shares no pins with the display. */
#define PIN_TOUCH_SCL   8
#define PIN_TOUCH_SDA   4
#define TOUCH_I2C_HZ    400000

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
        .duty       = 1023,   /* near-max brightness (10-bit count maxes at 1023; 1024 == true 100%) */
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&c));
}

/* ---- Touch: bring up an I2C master bus and the AXS15231B touch driver ---- */
static esp_lcd_touch_handle_t touch_init(void)
{
    i2c_master_bus_handle_t i2c_bus = NULL;
    const i2c_master_bus_config_t i2c_cfg = {
        .i2c_port   = I2C_NUM_0,
        .sda_io_num = PIN_TOUCH_SDA,
        .scl_io_num = PIN_TOUCH_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_cfg, &i2c_bus));

    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_AXS15231B_CONFIG();
    /* The shared config macro doesn't set a clock; the IDF new I2C master
     * driver requires a non-zero per-device SCL speed. */
    tp_io_cfg.scl_speed_hz = TOUCH_I2C_HZ;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus, &tp_io_cfg, &tp_io));

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max        = LCD_H_RES,
        .y_max        = LCD_V_RES,
        .rst_gpio_num = -1,
        .int_gpio_num = -1,   /* poll rather than use the INT line (GPIO3) */
    };
    esp_lcd_touch_handle_t tp = NULL;
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_axs15231b(tp_io, &tp_cfg, &tp));
    ESP_LOGI(TAG, "touch ready (AXS15231B @0x%02X, SCL=%d SDA=%d)",
             ESP_LCD_TOUCH_IO_I2C_AXS15231B_ADDRESS, PIN_TOUCH_SCL, PIN_TOUCH_SDA);
    return tp;
}

void app_main(void)
{
    ESP_LOGI(TAG, "JC3248W535EN / AXS15231B QSPI + touch bring-up demo");

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
     * Turn the display output stage ON. The init table does NOT issue DISPON
     * (0x29) itself, so this call is required — without it you get a perfectly
     * black, perfectly backlit panel no matter what you write to GRAM.
     * (The driver follows the standard esp_lcd contract: true == ON.)
     */
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true)); /* true == display ON */

    /* ---- Touch ---- */
    esp_lcd_touch_handle_t tp = touch_init();

    /* ---- Solid-color test: prove the panel + backlight + draw path ---- */
    uint16_t *fb = heap_caps_malloc(LCD_H_RES * LCD_V_RES * sizeof(uint16_t),
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_ERROR_CHECK(fb ? ESP_OK : ESP_ERR_NO_MEM);

    const uint16_t colors[] = { 0xF800, 0x07E0, 0x001F, 0xFFFF, 0x0000 }; /* R G B W K */
    const char    *names[]  = { "RED", "GREEN", "BLUE", "WHITE", "BLACK" };
    int i = 0;
    int64_t next_swap_us = 0;  /* draw immediately on the first iteration */
    bool was_pressed = false;

    while (1) {
        /* Display heartbeat: cycle the background color every 1.5 s. */
        if (esp_timer_get_time() >= next_swap_us) {
            /* AXS15231B expects big-endian RGB565, so byte-swap each pixel. */
            uint16_t c = (uint16_t)((colors[i] >> 8) | (colors[i] << 8));
            for (int p = 0; p < LCD_H_RES * LCD_V_RES; p++) fb[p] = c;
            /* Draw top-to-bottom in bands: the driver issues RAMWR on the first
             * band (y==0) then RAMWRC continuation for the rest. */
            for (int y = 0; y < LCD_V_RES; y += 80)
                ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 0, y, LCD_H_RES, y + 80, &fb[y * LCD_H_RES]));
            ESP_LOGI(TAG, "screen = %s", names[i]);
            i = (i + 1) % (sizeof(colors) / sizeof(colors[0]));
            next_swap_us = esp_timer_get_time() + 1500000;
        }

        /* Touch read-out: poll the panel and log press/release + coordinates.
         * Use ..._WITHOUT_ABORT so a transient I2C glitch logs a warning instead
         * of rebooting the board mid-demo. */
        if (esp_lcd_touch_read_data(tp) == ESP_OK) {
            uint16_t tx[1], ty[1], tstr[1];
            uint8_t tnum = 0;
            bool pressed = esp_lcd_touch_get_coordinates(tp, tx, ty, tstr, &tnum, 1) && tnum > 0;
            if (pressed) {
                ESP_LOGI(TAG, "touch  x=%3u  y=%3u", tx[0], ty[0]);
            } else if (was_pressed) {
                ESP_LOGI(TAG, "touch  released");
            }
            was_pressed = pressed;
        } else {
            ESP_LOGW(TAG, "touch read failed (I2C)");
        }

        vTaskDelay(pdMS_TO_TICKS(30));  /* ~33 Hz touch poll */
    }
}
