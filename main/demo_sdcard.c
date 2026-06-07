/*
 * SPDX-FileCopyrightText: 2026 GlomarGadaffi
 * SPDX-License-Identifier: Apache-2.0
 *
 * microSD demo: mount the on-board card slot over SD_MMC (SDIO, 1-bit mode),
 * print card info, and list the root directory. Draws a green status bar on a
 * successful mount or a red one on failure (e.g. no card / not FAT-formatted),
 * then idles until the user taps the back bar.
 *
 * Fully re-entrant: every return path unmounts the card and releases the SDMMC
 * slot, so re-opening the demo mounts cleanly.
 */
#include "demos.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>

static const char *TAG = "demo.sdcard";

#define SD_MOUNT_POINT "/sdcard"

void demo_sdcard(ui_t *ui, esp_lcd_touch_handle_t tp)
{
    ESP_LOGI(TAG, "microSD demo — tap the back bar to exit");

    /* Base screen + back bar first so the UI is immediately responsive. */
    ui_fill(ui, ui_rgb(0, 0, 40));
    ui_back_bar(ui);
    ui_flush(ui);

    sdmmc_card_t *card = NULL;
    bool mounted = false;

    /* SD_MMC host in 1-bit mode (board exposes CLK/CMD/D0 only). */
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags = SDMMC_HOST_FLAG_1BIT;
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = PIN_SD_CLK;
    slot.cmd = PIN_SD_CMD;
    slot.d0 = PIN_SD_D0;
    /* No external pullups on the board's SD lines — enable the internal ones. */
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_vfs_fat_sdmmc_mount_config_t mcfg = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    esp_err_t ret = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot, &mcfg, &card);
    if (ret == ESP_OK) {
        mounted = true;

        /* Green status bar just under the back bar. */
        ui_rect(ui, 0, UI_BACK_H, LCD_H_RES, 40, ui_rgb(0, 180, 0));
        ui_flush(ui);

        /* Dump card details and capacity to the log. */
        sdmmc_card_print_info(stdout, card);
        uint64_t cap_bytes = (uint64_t)card->csd.capacity * card->csd.sector_size;
        ESP_LOGI(TAG, "Card mounted: %s, capacity %llu MiB",
                 card->cid.name, cap_bytes / (1024ULL * 1024ULL));

        /* List the root directory; never crash on a bad entry. */
        DIR *dir = opendir(SD_MOUNT_POINT);
        if (dir == NULL) {
            ESP_LOGW(TAG, "Could not open %s for listing", SD_MOUNT_POINT);
        } else {
            ESP_LOGI(TAG, "Listing %s:", SD_MOUNT_POINT);
            struct dirent *ent;
            int count = 0;
            while ((ent = readdir(dir)) != NULL) {
                if (ent->d_name[0] == '\0') {
                    continue;
                }
                char path[300];
                int n = snprintf(path, sizeof(path), "%s/%s", SD_MOUNT_POINT, ent->d_name);
                if (n <= 0 || n >= (int)sizeof(path)) {
                    ESP_LOGW(TAG, "  %s (path too long, size unknown)", ent->d_name);
                    count++;
                    continue;
                }
                struct stat st;
                if (stat(path, &st) == 0) {
                    if (S_ISDIR(st.st_mode)) {
                        ESP_LOGI(TAG, "  %s/ (dir)", ent->d_name);
                    } else {
                        ESP_LOGI(TAG, "  %s (%ld bytes)", ent->d_name, (long)st.st_size);
                    }
                } else {
                    ESP_LOGW(TAG, "  %s (stat failed)", ent->d_name);
                }
                count++;
            }
            closedir(dir);
            ESP_LOGI(TAG, "%d entr%s in root", count, count == 1 ? "y" : "ies");
        }
    } else {
        /* Red status bar; warn but do NOT abort — the app must run with no card. */
        ui_rect(ui, 0, UI_BACK_H, LCD_H_RES, 40, ui_rgb(200, 0, 0));
        ui_flush(ui);
        ESP_LOGW(TAG, "SD mount failed (%s) — insert a FAT-formatted microSD",
                 esp_err_to_name(ret));
    }

    /* Idle ~33 Hz until the user taps the back bar. */
    uint16_t x, y;
    while (1) {
        if (board_touch(tp, &x, &y) && ui_in_back(x, y)) {
            goto cleanup;
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }

cleanup:
    if (mounted) {
        /* IDF 6.0 signature: esp_vfs_fat_sdcard_unmount(base_path, card).
         * This unmounts FATFS, deinits the card, and frees the SDMMC slot,
         * so a subsequent demo entry can mount again. */
        esp_err_t uret = esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, card);
        if (uret != ESP_OK) {
            ESP_LOGW(TAG, "Unmount returned %s", esp_err_to_name(uret));
        } else {
            ESP_LOGI(TAG, "Card unmounted");
        }
        mounted = false;
        card = NULL;
    }
}
