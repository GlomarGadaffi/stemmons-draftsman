/*
 * SPDX-FileCopyrightText: 2026 GlomarGadaffi
 * SPDX-License-Identifier: Apache-2.0
 *
 * Central pin map for the Guition JC3248W535EN (ESP32-S3). Verified against the
 * upstream NorthernMan54/JC3248W535EN pincfg.h and the Guition specification.
 */
#pragma once

#include "esp_adc/adc_oneshot.h"

/* ---- Display: AXS15231B, 320x480 IPS, QSPI ---- */
#define LCD_H_RES   320
#define LCD_V_RES   480
#define PIN_CS      45
#define PIN_SCK     47
#define PIN_D0      21
#define PIN_D1      48
#define PIN_D2      40
#define PIN_D3      39
#define PIN_BL       1   /* backlight — PWM driven */
#define LCD_HOST    SPI2_HOST

/* ---- Capacitive touch: AXS15231B over I2C (addr 0x3B) ---- */
#define PIN_TOUCH_SCL   8
#define PIN_TOUCH_SDA   4
#define TOUCH_I2C_HZ    400000

/* ---- microSD: SD_MMC (SDIO) 1-bit mode ---- */
#define PIN_SD_CLK   12
#define PIN_SD_CMD   11
#define PIN_SD_D0    13

/* ---- Audio: I2S to NS4168 / AX98357A class-D amp -> speaker ---- */
#define PIN_I2S_BCLK 42
#define PIN_I2S_WS    2
#define PIN_I2S_DOUT 41

/* ---- Battery voltage: divider into ADC1 (GPIO5 == ADC1 channel 4) ---- */
#define PIN_BAT_ADC_CHANNEL  ADC_CHANNEL_4
#define BAT_DIVIDER_RATIO    2.0f   /* confirm against the board; assumed 1:2 */
