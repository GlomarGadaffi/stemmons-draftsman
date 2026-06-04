# esp-idf-jc3248w535-axs15231b

A **working** ESP-IDF driver + bring-up demo for the **Guition JC3248W535EN** — the cheap 3.5″ ESP32-S3 board with an **AXS15231B** QSPI display (320×480) and capacitive touch.

The vendor ZIP doesn't build cleanly, and most forks render nothing, a garbled image, or a black screen. This repo boots, lights the panel, and cycles a color test out of the box — and, more importantly, **documents every gotcha** that makes this board look dead.

---

## ⚠️ The one bug that wastes everyone a day

This panel's `esp_lcd` driver has an **inverted `disp_on_off()` handler**. Its bool argument means **"off"**, not the usual esp_lcd "on", and esp_lcd passes the value straight through:

```c
// WRONG — sends DISPOFF. Black, perfectly backlit panel, no matter what you draw.
esp_lcd_panel_disp_on_off(panel, true);

// RIGHT — false turns the display ON.
esp_lcd_panel_disp_on_off(panel, false);
```

If your backlight is on but the screen is black no matter what you write to GRAM, **this is almost certainly why.** Check it first.

---

## Quick start

```bash
# ESP-IDF v5.3+ (v6.0.x currently ICEs compiling esp_lcd_panel_rgb.c)
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash monitor
```

You should see the screen cycle **red → green → blue → white → black** every 1.5 s, and the serial log print each color. If it does, your panel is fully alive and you can layer LVGL (or anything) on top.

> **Windows note:** build from **PowerShell** (`. C:\esp\v5.3.5\esp-idf\export.ps1`). ESP-IDF's `export.sh` **aborts under git-bash/MSYS** ("MSys/Mingw is not supported") and the build silently no-ops.

---

## Pinout

See [docs/pinout.md](docs/pinout.md). LCD: CS 45, CLK 47, D0–D3 21/48/40/39, BL 1 (PWM), TE 38. Touch (I2C): SDA 4, SCL 8.

---

## Full bring-up checklist

Every one of these was required to get from "dead board" to "rendering UI". In rough order of how badly each one bites:

1. **`disp_on_off(panel, false)` to turn the display ON** (inverted handler — see above). The black-screen killer.
2. **Use the right init sequence.** The AXS15231B ships in several panel variants; the wrong gamma/power table shows nothing or garbage. The table baked into this driver is verified for the JC3248W535EN, and it ends by setting the full CASET/RASET window (`0x2A`/`0x2B`).
3. **Backlight is PWM, not a GPIO level.** Drive `BL` (GPIO1) with LEDC at full duty. A bare `gpio_set_level(1)` looks dim/dead.
4. **Big-endian RGB565.** The panel wants byte-swapped pixels. In raw esp_lcd, swap each `uint16_t`; with LVGL set `LV_COLOR_16_SWAP 1` (or `CONFIG_LV_COLOR_16_SWAP=y`).
5. **Touch I2C `scl_speed_hz`.** `ESP_LCD_TOUCH_IO_I2C_AXS15231B_CONFIG()` leaves it `0`; the `i2c_master` driver rejects that. Set `400000`.
6. **App partition size.** A real UI binary overflows `SINGLE_APP_LARGE`; this repo ships a custom `partitions.csv` with a 4 MB app slot on the 16 MB flash.
7. **Draw path.** This driver skips per-flush RASET in QSPI mode and fills via `RAMWR` (top band) + `RAMWRC` continuation — it expects a **full-screen, top-to-bottom** flush. For partial-update UIs, either flush the whole screen each frame or switch to `CASET+RASET+RAMWR` per area.

## Adding LVGL on top

This demo is deliberately LVGL-free so the panel path is unambiguous. When you wire LVGL (8.3/8.4):

- Call **`lv_init()` before any other LVGL API** (skipping it leaves the timer linked-list uninitialized → garbage-size malloc → `tlsf.c block_locate_free` assert in `lv_disp_drv_register`).
- `LV_COLOR_DEPTH 16`, `LV_COLOR_16_SWAP 1`.
- LVGL is **single-threaded**: guard every `lv_*`/UI call with one mutex if you render from a task and mutate widgets from another, or you'll fault in `get_prop_core` during layout.
- Draw buffers: partial bands in internal DMA-capable RAM flush fine; a full-screen PSRAM buffer pushed as one giant `tx_color` can fail — flush in bands.

---

## Credits

The `esp_lcd_axs15231b` driver is **Espressif's** (Apache-2.0); this repo keeps their copyright and ships the JC3248W535EN-specific init table + packaging + the disp_on_off documentation. Pin/init/color details were cross-checked against the excellent community references:

- [NorthernMan54/JC3248W535EN](https://github.com/NorthernMan54/JC3248W535EN) — board pinout, schematics, working LVGL BSP
- [tsebelev/esp32_JC3248W535EN_work_exampl_lvgl_axs15231b](https://github.com/tsebelev/esp32_JC3248W535EN_work_exampl_lvgl_axs15231b) — working ESP-IDF + LVGL example

## License

Apache-2.0. See [LICENSE](LICENSE). The driver retains its original Espressif SPDX headers.
