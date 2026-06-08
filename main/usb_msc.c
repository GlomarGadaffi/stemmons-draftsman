/*
 * SPDX-FileCopyrightText: 2026 GlomarGadaffi
 * SPDX-License-Identifier: Apache-2.0
 */
#include "usb_msc.h"
#include "board.h"
#include <stdlib.h>
#include "esp_attr.h"
#include "esp_system.h"
#include "esp_check.h"
#include "esp_log.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_msc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "usb_msc";

/* One-shot request flag in RTC memory: survives esp_restart() (soft reset) but is
 * lost when the 3V3 rail drops on USB unplug — so a replug always comes up normal. */
#define USB_MSC_MAGIC 0x05B70D02u
static RTC_NOINIT_ATTR uint32_t s_msc_flag;

bool usb_msc_requested(void)
{
    if (s_msc_flag == USB_MSC_MAGIC) {
        s_msc_flag = 0;
        return true;
    }
    return false;
}

void usb_msc_request_reboot(void)
{
    s_msc_flag = USB_MSC_MAGIC;
    esp_restart();
}

/* Bring up the SD card raw (no VFS mount) so it can be handed to TinyUSB MSC. */
static esp_err_t sd_card_init(sdmmc_card_t **out_card)
{
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags = SDMMC_HOST_FLAG_1BIT;
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = PIN_SD_CLK;
    slot.cmd = PIN_SD_CMD;
    slot.d0  = PIN_SD_D0;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    ESP_RETURN_ON_ERROR(sdmmc_host_init(), TAG, "sdmmc host init");
    ESP_RETURN_ON_ERROR(sdmmc_host_init_slot(host.slot, &slot), TAG, "sdmmc slot init");

    sdmmc_card_t *card = calloc(1, sizeof(sdmmc_card_t));
    if (!card) return ESP_ERR_NO_MEM;
    esp_err_t err = sdmmc_card_init(&host, card);
    if (err != ESP_OK) {
        free(card);
        return err;
    }
    *out_card = card;
    return ESP_OK;
}

static void center_text(ui_t *ui, int y, const char *s, int scale, uint16_t color)
{
    ui_text(ui, LCD_H_RES / 2 - ui_text_w(s, scale) / 2, y, s, scale, color);
}

static void msc_fail(ui_t *ui, const char *line)
{
    ui_fill(ui, ui_rgb(20, 8, 8));
    center_text(ui, 90,  "USB DRIVE", 3, ui_rgb(255, 120, 120));
    center_text(ui, 210, line, 2, ui_rgb(255, 170, 170));
    center_text(ui, LCD_V_RES - 40, "Unplug USB to exit", 2, ui_rgb(200, 200, 200));
    ui_flush(ui);
    while (1) vTaskDelay(pdMS_TO_TICKS(500));   /* exit = power cycle */
}

void usb_msc_run(ui_t *ui)
{
    ESP_LOGI(TAG, "entering USB mass-storage mode");
    ui_fill(ui, ui_rgb(10, 14, 28));
    center_text(ui, 90, "USB DRIVE", 3, ui_rgb(120, 200, 255));
    center_text(ui, 200, "starting...", 2, ui_rgb(200, 210, 230));
    ui_flush(ui);

    sdmmc_card_t *card = NULL;
    esp_err_t err = sd_card_init(&card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SD init failed: %s", esp_err_to_name(err));
        msc_fail(ui, "No SD card");
    }

    /* Three steps: install the MSC driver, register the SD card as a LUN (mount_point
     * defaults to _MOUNT_USB = exposed to host), then install the TinyUSB device —
     * that last call is what actually enumerates on the PC. */
    const tinyusb_msc_driver_config_t mdrv = { .callback = NULL, .callback_arg = NULL };
    err = tinyusb_msc_install_driver(&mdrv);
    if (err == ESP_OK) {
        tinyusb_msc_storage_handle_t storage = NULL;
        const tinyusb_msc_storage_config_t scfg = { .medium.card = card };
        err = tinyusb_msc_new_storage_sdmmc(&scfg, &storage);
    }
    if (err == ESP_OK) {
        tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
        err = tinyusb_driver_install(&tusb_cfg);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "USB MSC bring-up failed: %s", esp_err_to_name(err));
        msc_fail(ui, "USB init failed");
    }

    /* Connected screen + a slow breathing sonar ring so it reads as "alive". */
    ui_fill(ui, ui_rgb(10, 14, 28));
    center_text(ui, 56,  "USB DRIVE", 3, ui_rgb(120, 200, 255));
    center_text(ui, 110, "Connected to PC", 2, ui_rgb(180, 255, 200));
    center_text(ui, LCD_V_RES - 40, "Unplug USB to return", 2, ui_rgb(170, 180, 190));
    ui_flush(ui);

    const int rcx = LCD_H_RES / 2, rcy = 280, box = 170;
    for (int phase = 0;; phase++) {
        ui_rect(ui, rcx - box / 2, rcy - box / 2, box, box, ui_rgb(10, 14, 28));
        int r = 30 + (phase % 50);                 /* expand 30..80 then wrap */
        int b = 235 - (r - 30) * 200 / 50;
        uint16_t c = ui_rgb(0, (uint8_t)b, (uint8_t)b);
        ui_circle(ui, rcx, rcy, r, c);
        ui_circle(ui, rcx, rcy, r + 1, c);
        ui_rect(ui, rcx - 4, rcy - 4, 8, 8, ui_rgb(0, 255, 255));
        ui_flush(ui);
        vTaskDelay(pdMS_TO_TICKS(70));
    }
}
