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
    ui_text(ui, 150, 16, "microSD", 2, ui_rgb(255, 255, 255));
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

        /* Green status bar + on-screen card summary. */
        ui_rect(ui, 0, UI_BACK_H, LCD_H_RES, 28, ui_rgb(0, 150, 0));
        ui_text(ui, 8, UI_BACK_H + 6, "MOUNTED", 2, ui_rgb(255, 255, 255));

        sdmmc_card_print_info(stdout, card);
        uint64_t cap_bytes = (uint64_t)card->csd.capacity * card->csd.sector_size;
        unsigned cap_mb = (unsigned)(cap_bytes / (1024ULL * 1024ULL));
        ESP_LOGI(TAG, "Card mounted: %s, capacity %u MiB", card->cid.name, cap_mb);

        static char line[280];   /* static (single-task demo) to keep it off the stack */
        snprintf(line, sizeof(line), "%s  %u MB", card->cid.name, cap_mb);
        ui_text(ui, 8, UI_BACK_H + 36, line, 2, ui_rgb(200, 255, 200));

        /* List the root directory; log every entry, draw the first dozen. */
        int ty = UI_BACK_H + 64;
        DIR *dir = opendir(SD_MOUNT_POINT);
        if (dir == NULL) {
            ESP_LOGW(TAG, "Could not open %s for listing", SD_MOUNT_POINT);
            ui_text(ui, 8, ty, "(cannot list)", 2, ui_rgb(255, 200, 200));
        } else {
            ESP_LOGI(TAG, "Listing %s:", SD_MOUNT_POINT);
            struct dirent *ent;
            int count = 0;
            while ((ent = readdir(dir)) != NULL) {
                if (ent->d_name[0] == '\0') {
                    continue;
                }
                static char path[300];
                int n = snprintf(path, sizeof(path), "%s/%s", SD_MOUNT_POINT, ent->d_name);
                bool isdir = false; long sz = 0; bool have = false;
                if (n > 0 && n < (int)sizeof(path)) {
                    struct stat st;
                    if (stat(path, &st) == 0) { isdir = S_ISDIR(st.st_mode); sz = (long)st.st_size; have = true; }
                }
                if (have && isdir)  ESP_LOGI(TAG, "  %s/ (dir)", ent->d_name);
                else if (have)      ESP_LOGI(TAG, "  %s (%ld bytes)", ent->d_name, sz);
                else                ESP_LOGW(TAG, "  %s (stat failed)", ent->d_name);
                if (ty < LCD_V_RES - 20) {   /* draw the first entries that fit */
                    snprintf(line, sizeof(line), "%s%s", ent->d_name, isdir ? "/" : "");
                    ui_text(ui, 8, ty, line, 2, ui_rgb(220, 220, 220));
                    ty += 18;
                }
                count++;
            }
            closedir(dir);
            ESP_LOGI(TAG, "%d entr%s in root", count, count == 1 ? "y" : "ies");
        }
        ui_flush(ui);
    } else {
        /* Red status bar; warn but do NOT abort — the app must run with no card. */
        ui_rect(ui, 0, UI_BACK_H, LCD_H_RES, 28, ui_rgb(180, 0, 0));
        ui_text(ui, 8, UI_BACK_H + 6, "NO CARD", 2, ui_rgb(255, 255, 255));
        ui_text(ui, 8, UI_BACK_H + 40, "insert FAT card", 2, ui_rgb(255, 210, 210));
        ui_flush(ui);
        ESP_LOGW(TAG, "SD mount failed (%s) - insert a FAT-formatted microSD",
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
