/*
 * SPDX-FileCopyrightText: 2026 GlomarGadaffi
 * SPDX-License-Identifier: Apache-2.0
 *
 * Wi-Fi scan + battery voltage demo. On entry it brings up the Wi-Fi station,
 * runs a blocking scan and logs every visible AP, then draws an AP count and a
 * battery-level bar that refreshes roughly every two seconds. The battery
 * voltage is read from ADC1 (oneshot) with curve-fitting calibration when the
 * eFuse supports it. Tap the top back bar to exit.
 *
 * Re-entrant: everything created here (Wi-Fi driver, the default-STA netif, the
 * ADC oneshot unit and its calibration scheme) is torn down before returning,
 * so the user can leave and come back. The process-global singletons (NVS, the
 * default event loop, esp_netif_init) are created once and tolerate being
 * "already initialized" on a second entry.
 */
#include "demos.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "demo.wifi_battery";

#define SCAN_MAX_APS     20             /* cap how many AP records we fetch   */
#define BAT_REFRESH_MS   2000           /* battery re-read interval           */
#define POLL_MS          30             /* touch sampling period              */
#define BAT_MV_MIN       3000           /* empty   (~3.0 V) -> bar at 0%      */
#define BAT_MV_MAX       4200           /* full    (~4.2 V) -> bar at 100%    */

/* Human-readable name for a Wi-Fi authmode, for logging. */
static const char *authmode_str(wifi_auth_mode_t m)
{
    switch (m) {
        case WIFI_AUTH_OPEN:            return "OPEN";
        case WIFI_AUTH_WEP:             return "WEP";
        case WIFI_AUTH_WPA_PSK:         return "WPA-PSK";
        case WIFI_AUTH_WPA2_PSK:        return "WPA2-PSK";
        case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/WPA2-PSK";
        case WIFI_AUTH_WPA3_PSK:        return "WPA3-PSK";
        case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/WPA3-PSK";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-ENT";
        default:                        return "?";
    }
}

/* One-time process globals. nvs_flash_init / esp_netif_init /
 * esp_event_loop_create_default are idempotent across demo entries: NVS reports
 * success when already up, the other two return ESP_ERR_INVALID_STATE which we
 * treat as "already done". */
static void ensure_process_globals(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(err);
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(err);
    }
}

/* Set from the Wi-Fi event handler when an async scan finishes. */
static volatile bool s_scan_done;
static void scan_done_cb(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)id; (void)data;
    s_scan_done = true;
}

/* One frame of the "scanning" animation: cyan sonar rings pulsing outward from a
 * center node, with a title and the back bar (so the scan stays cancelable). */
static void draw_scan_frame(ui_t *ui, int phase)
{
    const int cx = LCD_H_RES / 2;
    const int cy = UI_BACK_H + 210;
    const int max_r = 130;
    const int spacing = max_r / 3;

    ui_fill(ui, ui_rgb(8, 12, 24));
    ui_back_bar(ui);
    ui_text(ui, cx - ui_text_w("Scanning WiFi", 2) / 2, UI_BACK_H + 28,
            "Scanning WiFi", 2, ui_rgb(220, 230, 255));

    for (int k = 0; k < 3; k++) {                 /* three rings, staggered */
        int r = (phase + k * spacing) % max_r;
        int b = 235 - (r * 210 / max_r);          /* fade as the ring expands */
        if (b < 20) b = 20;
        uint16_t c = ui_rgb(0, (uint8_t)b, (uint8_t)b);
        ui_circle(ui, cx, cy, r, c);
        ui_circle(ui, cx, cy, r + 1, c);          /* 2px ring for visibility */
    }
    ui_rect(ui, cx - 3, cy - 3, 6, 6, ui_rgb(0, 255, 255));   /* center node */
    ui_flush(ui);
}

void demo_wifi_battery(ui_t *ui, esp_lcd_touch_handle_t tp)
{
    ESP_LOGI(TAG, "wifi+battery demo started — tap the back bar to exit");

    /* ---------- Battery ADC (oneshot + curve-fitting calibration) ---------- */
    adc_oneshot_unit_handle_t adc = NULL;
    adc_oneshot_unit_init_cfg_t uinit = { .unit_id = ADC_UNIT_1 };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&uinit, &adc));

    adc_oneshot_chan_cfg_t ccfg = {
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc, PIN_BAT_ADC_CHANNEL, &ccfg));

    adc_cali_handle_t cali = NULL;
    adc_cali_curve_fitting_config_t cc = {
        .unit_id  = ADC_UNIT_1,
        .chan     = PIN_BAT_ADC_CHANNEL,
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    bool have_cali =
        (adc_cali_create_scheme_curve_fitting(&cc, &cali) == ESP_OK);
    if (!have_cali) {
        ESP_LOGW(TAG, "ADC curve-fitting calibration unavailable; raw only");
    }

    /* ---------- Wi-Fi station bring-up + blocking scan ---------- */
    ensure_process_globals();

    esp_netif_t *netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wcfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* Power-save while the radio is up, then run an ASYNC scan so the UI stays
     * responsive — animate sonar rings until WIFI_EVENT_SCAN_DONE (or a back-tap). */
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

    int ap_count = 0;
    bool cancelled = false;
    s_scan_done = false;
    esp_event_handler_instance_t scan_h = NULL;
    esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE,
                                        scan_done_cb, NULL, &scan_h);
    wifi_scan_config_t scfg = {
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = { .active = { .min = 60, .max = 150 } },   /* bound per-channel dwell */
    };
    if (esp_wifi_scan_start(&scfg, false) == ESP_OK) {
        uint16_t sx, sy;
        /* guard caps the wait (~9 s) in case the done-event never arrives */
        for (int phase = 0, guard = 0; !s_scan_done && guard < 300; phase += 5, guard++) {
            if (board_touch(tp, &sx, &sy) && ui_in_back(sx, sy)) {
                cancelled = true;
                esp_wifi_scan_stop();
                break;
            }
            draw_scan_frame(ui, phase);
            vTaskDelay(pdMS_TO_TICKS(30));
        }
    } else {
        ESP_LOGW(TAG, "esp_wifi_scan_start failed");
    }
    esp_event_handler_instance_unregister(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, scan_h);

    if (cancelled) {
        ESP_LOGI(TAG, "scan cancelled by user");
        goto teardown;
    }

    {
        uint16_t n = 0;
        esp_wifi_scan_get_ap_num(&n);
        if (n > SCAN_MAX_APS) {
            n = SCAN_MAX_APS;
        }
        if (n > 0) {
            wifi_ap_record_t *recs = calloc(n, sizeof(*recs));
            if (recs) {
                esp_wifi_scan_get_ap_records(&n, recs);
                ap_count = (int)n;
                ESP_LOGI(TAG, "found %d AP(s):", ap_count);
                for (int i = 0; i < ap_count; i++) {
                    ESP_LOGI(TAG, "  %2d. %-32s rssi=%4d ch=%2d %s",
                             i + 1, (const char *)recs[i].ssid,
                             recs[i].rssi, recs[i].primary,
                             authmode_str(recs[i].authmode));
                }
                free(recs);
            } else {
                ESP_LOGE(TAG, "calloc(%u ap_records) failed", n);
            }
        } else {
            ESP_LOGI(TAG, "no APs found");
        }
    }

    /* ---------- UI loop: AP count (static) + live battery bar ---------- */
    const uint16_t col_bg     = ui_rgb(20, 24, 40);
    const uint16_t col_panel  = ui_rgb(45, 50, 70);
    const uint16_t col_track  = ui_rgb(70, 75, 95);
    const uint16_t col_bat    = ui_rgb(60, 210, 90);
    const uint16_t col_bat_lo = ui_rgb(220, 70, 60);
    const uint16_t col_apdot  = ui_rgb(90, 170, 255);

    /* Battery bar geometry (below the back bar, leaving room for the AP row). */
    const int bar_x = 24;
    const int bar_y = UI_BACK_H + 120;
    const int bar_w = LCD_H_RES - 2 * bar_x;
    const int bar_h = 40;

    /* AP-count indicator row: a small dot per detected AP. */
    const int dot_y = UI_BACK_H + 40;
    const int dot_sz = 14;
    const int dot_gap = 6;

    ui_fill(ui, col_bg);
    ui_back_bar(ui);
    ui_text(ui, 120, 16, "WiFi+Batt", 2, ui_rgb(255, 255, 255));

    /* AP count panel: label + one dot per AP, capped to what fits. */
    ui_rect(ui, 12, UI_BACK_H + 16, LCD_H_RES - 24, 56, col_panel);
    char apline[24];
    snprintf(apline, sizeof(apline), "WiFi: %d APs", ap_count);
    ui_text(ui, 18, UI_BACK_H + 22, apline, 2, ui_rgb(220, 230, 255));
    int max_dots = (LCD_H_RES - 24) / (dot_sz + dot_gap);
    int shown = ap_count < max_dots ? ap_count : max_dots;
    for (int i = 0; i < shown; i++) {
        ui_rect(ui, 18 + i * (dot_sz + dot_gap), dot_y, dot_sz, dot_sz,
                col_apdot);
    }

    /* Static battery track; the fill inside it is redrawn each refresh. */
    ui_rect(ui, bar_x - 3, bar_y - 3, bar_w + 6, bar_h + 6, col_panel);
    ui_rect(ui, bar_x, bar_y, bar_w, bar_h, col_track);
    ui_flush(ui);

    uint16_t x, y;
    int since_refresh = BAT_REFRESH_MS;   /* force an immediate first read */

    while (1) {
        if (board_touch(tp, &x, &y) && ui_in_back(x, y)) {
            ESP_LOGI(TAG, "back bar tapped — exiting (%d APs)", ap_count);
            break;
        }

        if (since_refresh >= BAT_REFRESH_MS) {
            since_refresh = 0;

            int raw = 0;
            esp_err_t rerr = adc_oneshot_read(adc, PIN_BAT_ADC_CHANNEL, &raw);
            int battery_mv = 0;

            if (rerr != ESP_OK) {
                ESP_LOGW(TAG, "adc read failed: %s", esp_err_to_name(rerr));
            } else if (have_cali) {
                int mv = 0;
                if (adc_cali_raw_to_voltage(cali, raw, &mv) == ESP_OK) {
                    battery_mv = (int)(mv * BAT_DIVIDER_RATIO);
                    ESP_LOGI(TAG, "battery: raw=%d pin=%dmV vbat=%dmV",
                             raw, mv, battery_mv);
                } else {
                    ESP_LOGW(TAG, "raw->voltage conversion failed (raw=%d)", raw);
                }
            } else {
                ESP_LOGI(TAG, "battery: raw=%d (uncalibrated)", raw);
            }

            /* Map battery_mv to fill width; clamp to the track. 0 mV (read or
             * conversion failure) shows as an empty, red bar. */
            int pct = 0;
            if (battery_mv > 0) {
                pct = (battery_mv - BAT_MV_MIN) * 100 /
                      (BAT_MV_MAX - BAT_MV_MIN);
            }
            if (pct < 0)   pct = 0;
            if (pct > 100) pct = 100;
            int fill_w = bar_w * pct / 100;

            uint16_t fill_col = (pct <= 20) ? col_bat_lo : col_bat;

            /* Voltage read-out above the bar. */
            char bline[24];
            if (battery_mv > 0)
                snprintf(bline, sizeof(bline), "Batt: %d.%02d V",
                         battery_mv / 1000, (battery_mv % 1000) / 10);
            else
                snprintf(bline, sizeof(bline), "Batt: --");
            ui_rect(ui, bar_x, bar_y - 28, bar_w, 18, col_bg);
            ui_text(ui, bar_x, bar_y - 28, bline, 2, ui_rgb(220, 255, 220));

            /* Redraw the track, then the fill on top. */
            ui_rect(ui, bar_x, bar_y, bar_w, bar_h, col_track);
            if (fill_w > 0) {
                ui_rect(ui, bar_x, bar_y, fill_w, bar_h, fill_col);
            }
            ui_flush(ui);
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
        since_refresh += POLL_MS;
    }

teardown:
    /* ---------- Teardown (order matters; reverse of bring-up) ---------- */
    esp_wifi_stop();
    esp_wifi_deinit();
    esp_netif_destroy_default_wifi(netif);   /* undo create_default_wifi_sta */

    if (have_cali) {
        adc_cali_delete_scheme_curve_fitting(cali);
    }
    adc_oneshot_del_unit(adc);

    ESP_LOGI(TAG, "wifi+battery demo torn down");
}
