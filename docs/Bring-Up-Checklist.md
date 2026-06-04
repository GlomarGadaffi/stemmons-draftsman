# Bring-Up Checklist

The complete, ordered list of everything that has to be right to go from "dead board" to "rendering UI" on the JC3248W535EN. Ranked by how badly each one bites.

## 1. `disp_on_off(panel, false)` to turn the display ON
Inverted handler — see **[The disp_on_off Inversion](disp_on_off-Inversion.md)**. The black-screen killer. Passing `true` sends DISPOFF.

## 2. Use the correct init sequence
The AXS15231B ships in several panel variants with different gamma/power tables. The wrong one shows nothing or garbage. Use the JC3248W535EN table (baked into the driver in this repo). It ends by setting the full window:
```c
{0x2A, (uint8_t[]){0x00,0x00,0x01,0x3f}, 4, 0},  // CASET 0..319
{0x2B, (uint8_t[]){0x00,0x00,0x01,0xdf}, 4, 0},  // RASET 0..479
```

## 3. Backlight is PWM, not a GPIO level
`BL` = GPIO1, driven by **LEDC** at full duty. A bare `gpio_set_level(1, 1)` looks dim or dead.
```c
ledc_timer_config_t t = { .speed_mode=LEDC_LOW_SPEED_MODE, .duty_resolution=LEDC_TIMER_10_BIT,
                          .timer_num=LEDC_TIMER_1, .freq_hz=5000, .clk_cfg=LEDC_AUTO_CLK };
ledc_timer_config(&t);
ledc_channel_config_t c = { .gpio_num=1, .speed_mode=LEDC_LOW_SPEED_MODE, .channel=LEDC_CHANNEL_0,
                            .timer_sel=LEDC_TIMER_1, .duty=1023, .hpoint=0 };
ledc_channel_config(&c);
```

## 4. Big-endian RGB565
The panel wants byte-swapped pixels. Raw esp_lcd: swap each `uint16_t` (`(c>>8)|(c<<8)`). LVGL: `LV_COLOR_16_SWAP 1` / `CONFIG_LV_COLOR_16_SWAP=y`. Tell: a `0xFFFF` white frame shows white either way, but a red frame comes out near-black when the order is wrong.

## 5. Touch I2C `scl_speed_hz`
`ESP_LCD_TOUCH_IO_I2C_AXS15231B_CONFIG()` leaves `scl_speed_hz = 0`; the `i2c_master` driver rejects it with `invalid scl frequency`. Set it:
```c
touch_io_config.scl_speed_hz = 400000;
```

## 6. App partition size
A real UI binary overflows `SINGLE_APP_LARGE` (~1.5 MB). On 16 MB flash, use a custom `partitions.csv` with a bigger app slot:
```
factory,  app,  factory,  0x10000,  0x400000
```
and `CONFIG_PARTITION_TABLE_CUSTOM=y` + `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"`.

## 7. Draw path / flush order
This driver skips per-flush RASET in QSPI mode and fills via `RAMWR` (top band, `y_start==0`) + `RAMWRC` continuation — it expects a **full-screen, top-to-bottom** flush. For partial-update UIs either flush the whole screen each frame (LVGL `full_refresh` with a screen-sized buffer) or modify `draw_bitmap` to emit `CASET+RASET+RAMWR` per area.

## 8. LVGL specifics
See **[Adding LVGL](Adding-LVGL.md)** — `lv_init()` ordering, the single-thread mutex, buffer placement.

## 9. Build from the right shell
ESP-IDF `export.sh` **aborts under git-bash/MSYS** — builds silently no-op. Use PowerShell `export.ps1`. See **[Building & Flashing](Building-and-Flashing.md)**.
