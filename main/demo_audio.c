/*
 * SPDX-FileCopyrightText: 2026 GlomarGadaffi
 * SPDX-License-Identifier: Apache-2.0
 *
 * I2S tone playback through the on-board class-D speaker amp (NS4168 /
 * AX98357A). Plays a simple repeating arpeggio (440/554/659 Hz) and draws a
 * moving colored bar so the screen shows activity. Uses the ESP-IDF 6.0 NEW
 * I2S driver (driver/i2s_std.h). The amp has no enable pin — it keys off the
 * I2S clocks, so silence == disabling the channel.
 *
 * RE-ENTRANT: the I2S channel is fully disabled + deleted on every exit path so
 * a second entry (after going back and re-entering) succeeds.
 */
#define _USE_MATH_DEFINES
#include "demos.h"
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>

static const char *TAG = "demo.audio";

#define SAMPLE_RATE   44100
#define AMPLITUDE     0.30f   /* moderate to avoid clipping/harshness */

/* Arpeggio: A4, C#5, E5 (an A-major chord). */
static const float NOTES[] = { 440.0f, 554.37f, 659.25f };
#define N_NOTES (sizeof(NOTES) / sizeof(NOTES[0]))

/* One block ~= 25 ms of audio so we can poll touch between writes. */
#define BLOCK_SAMPLES 1024   /* per channel (mono) before stereo interleave */

void demo_audio(ui_t *ui, esp_lcd_touch_handle_t tp)
{
    ESP_LOGI(TAG, "audio demo — tap the back bar to exit");

    /* ---- Draw the screen + back bar ---- */
    ui_fill(ui, ui_rgb(0, 40, 0));
    ui_back_bar(ui);
    ui_text(ui, 150, 16, "AUDIO", 2, ui_rgb(255, 255, 255));
    ui_flush(ui);

    /* ---- Set up the NEW I2S TX channel ---- */
    i2s_chan_handle_t tx = NULL;
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx, NULL));

    i2s_std_config_t std = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = PIN_I2S_BCLK,
            .ws   = PIN_I2S_WS,
            .dout = PIN_I2S_DOUT,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = { 0 },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx, &std));
    ESP_ERROR_CHECK(i2s_channel_enable(tx));

    /*
     * Precompute one stereo block (L == R) per note. Each note's frequency
     * yields an integer-ish number of samples per period; we generate a full
     * BLOCK_SAMPLES window per note so phase resets cleanly between notes
     * (audible click is negligible and this keeps the demo simple).
     */
    static int16_t block[N_NOTES][BLOCK_SAMPLES * 2]; /* *2 = stereo interleaved */
    for (size_t n = 0; n < N_NOTES; n++) {
        const double step = 2.0 * M_PI * (double)NOTES[n] / (double)SAMPLE_RATE;
        for (int i = 0; i < BLOCK_SAMPLES; i++) {
            int16_t s = (int16_t)(sin(step * (double)i) * AMPLITUDE * 32767.0);
            block[n][2 * i + 0] = s; /* left  */
            block[n][2 * i + 1] = s; /* right */
        }
    }

    /* ---- Playback loop ---- */
    const int bar_w = 40;
    const int bar_y = UI_BACK_H + 10;
    const int bar_h = LCD_V_RES - bar_y - 10;
    int bar_x = 0;
    int bar_dir = 8;
    size_t note = 0;
    int blocks_per_note = SAMPLE_RATE / BLOCK_SAMPLES / 3; /* ~333 ms per note */
    if (blocks_per_note < 1) blocks_per_note = 1;
    int block_count = 0;

    uint16_t tx_msg, ty_msg; /* touch coords */

    while (1) {
        /* Poll touch around each write so back-bar taps are responsive. */
        if (board_touch(tp, &tx_msg, &ty_msg) && ui_in_back(tx_msg, ty_msg)) {
            break;
        }

        size_t written = 0;
        esp_err_t err = i2s_channel_write(tx, block[note], sizeof(block[note]),
                                          &written, portMAX_DELAY);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "i2s_channel_write failed: %s", esp_err_to_name(err));
            break;
        }

        /* Advance the arpeggio. */
        if (++block_count >= blocks_per_note) {
            block_count = 0;
            note = (note + 1) % N_NOTES;
        }

        /* Animate a moving colored bar (clear old, draw new, flush). */
        ui_rect(ui, bar_x, bar_y, bar_w, bar_h, ui_rgb(0, 40, 0));
        bar_x += bar_dir;
        if (bar_x <= 0) { bar_x = 0; bar_dir = -bar_dir; }
        if (bar_x + bar_w >= LCD_H_RES) { bar_x = LCD_H_RES - bar_w; bar_dir = -bar_dir; }
        uint16_t bar_col = ui_rgb(note == 0 ? 255 : 0,
                                  note == 1 ? 255 : 0,
                                  note == 2 ? 255 : 0);
        ui_rect(ui, bar_x, bar_y, bar_w, bar_h, bar_col);
        ui_flush(ui);
    }

    /* ---- Tear down: MUST run on every exit path so re-entry works ---- */
    i2s_channel_disable(tx);
    i2s_del_channel(tx);
    ESP_LOGI(TAG, "audio demo stopped, I2S channel released");
}
