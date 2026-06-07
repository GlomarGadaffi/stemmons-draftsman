# esp-idf-jc3248w535-axs15231b

A **working, hardware-validated** ESP-IDF driver + bring-up demo for the **Guition JC3248W535EN** — the cheap 3.5″ ESP32-S3 board with an **AXS15231B** QSPI display (320×480) and in-cell capacitive touch.

The vendor ZIP doesn't build cleanly, and most forks render nothing, a garbled image, or a black screen. This repo boots, lights the panel, and cycles a solid-color test out of the box — and, more importantly, **documents every gotcha** that makes this board look dead.

> **Status:** built with **ESP-IDF 6.0.1** and flashed to a real JC3248W535EN — the demo cycles red → green → blue → white → black on the panel. Also builds on ESP-IDF **5.3+**.

---

## ⚠️ The one thing that wastes everyone a day

The verified JC3248W535EN init table does **not** issue `DISPON` (0x29) itself, so you **must** turn the display output stage on after init — otherwise the panel stays perfectly black but perfectly backlit, no matter what you write to GRAM:

```c
ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
esp_lcd_panel_disp_on_off(panel, true);   // true == ON (standard esp_lcd contract)
```

If your backlight is on but the screen is black no matter what you draw, a missing/incorrect `disp_on_off` is almost certainly why. **Check it first.**

> **Porting from an older fork?** Earlier revisions of the `esp_lcd_axs15231b` driver had an **inverted** `disp_on_off()` handler where `true` meant *OFF*, so you had to pass `false` to turn the display on. **This repo fixes that** — the handler now follows the standard esp_lcd contract (`true == ON`), so it also composes correctly with LVGL / `esp_lvgl_port`, which call `disp_on_off(panel, true)` internally. If your old code passed `false`, flip it to `true`.

---

## Quick start

```powershell
# ESP-IDF 5.3 ... 6.0 (validated on 6.0.1). Windows: use PowerShell, not git-bash.
. C:\esp\v6.0.1\esp-idf\export.ps1
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash monitor
```

You should see the screen cycle **red → green → blue → white → black** every 1.5 s, with the serial log printing each color:

```
I (...) jc3248w535: screen = RED
I (...) jc3248w535: screen = GREEN
...
```

If it does, your panel is fully alive and you can layer LVGL (or anything) on top.

> **Windows / git-bash trap:** ESP-IDF's `export.sh` **aborts under git-bash/MSYS** (`MSys/Mingw is not supported`); `idf.py` never lands on `PATH` and the build can silently no-op, so you flash stale code and chase phantom results. Build from **PowerShell** with `export.ps1`. See [Building & Flashing](docs/Building-and-Flashing.md).

---

## 📖 Documentation

Full bring-up docs in [`docs/`](docs/Home.md):

- **[The DISPON / disp_on_off gotcha](docs/disp_on_off-Inversion.md)** — the black-screen killer, in depth
- **[Bring-Up Checklist](docs/Bring-Up-Checklist.md)** — everything that has to be right, ordered by pain
- **[Adding LVGL](docs/Adding-LVGL.md)** — LVGL 8.x without the classic crashes
- **[Pinout & Hardware](docs/Pinout-and-Hardware.md)** · **[Building & Flashing](docs/Building-and-Flashing.md)**

---

## Pinout

See [docs/Pinout-and-Hardware.md](docs/Pinout-and-Hardware.md). LCD (QSPI): CS 45, CLK 47, D0–D3 = 21/48/40/39, BL 1 (PWM), TE 38. Touch (I2C): SDA 4, SCL 8. DC and RST are not connected (QSPI encodes D/C in the opcode; reset is software `SWRESET`).

---

## Bring-up checklist

Every one of these was required to get from "dead board" to "rendering UI", in rough order of how badly each one bites:

1. **`disp_on_off(panel, true)` to turn the display ON** — the init table doesn't issue DISPON (see above). The black-screen killer.
2. **Use the right init sequence.** The AXS15231B ships in several panel variants; the wrong gamma/power table shows nothing or garbage. The table baked into this driver is verified for the JC3248W535EN and ends by setting the full CASET/RASET window (`0x2A`/`0x2B`).
3. **Backlight is PWM, not a GPIO level.** Drive `BL` (GPIO1) with LEDC at full duty. A bare `gpio_set_level(1)` looks dim/dead.
4. **Big-endian RGB565.** The panel wants byte-swapped pixels. In raw esp_lcd, swap each `uint16_t`; with LVGL set `LV_COLOR_16_SWAP 1` (or `CONFIG_LV_COLOR_16_SWAP=y`).
5. **Touch I2C `scl_speed_hz`.** `ESP_LCD_TOUCH_IO_I2C_AXS15231B_CONFIG()` leaves it `0`; the `i2c_master` driver rejects that — set `400000`.
6. **App partition size.** A real UI binary overflows `SINGLE_APP_LARGE`; this repo ships a custom `partitions.csv` with a 4 MB app slot on the 16 MB flash.
7. **Draw path.** This driver skips per-flush RASET in QSPI mode and fills via `RAMWR` (top band) + `RAMWRC` continuation — it expects a **full-screen, top-to-bottom** flush (LVGL `full_refresh`). For partial-update UIs, flush the whole screen each frame or extend the driver to send `CASET+RASET+RAMWR` per area.

---

## ESP-IDF 6.0 notes

This repo builds on the 6.0 series (validated on 6.0.1). Two 6.0 API changes are already handled in the source — relevant if you adapt this driver elsewhere:

- **`color_space` was removed** from `esp_lcd_panel_dev_config_t`. The driver uses the current `rgb_ele_order` field.
- **The monolithic `driver` component was split.** LEDC/GPIO/SPI now live in `esp_driver_ledc` / `esp_driver_gpio` / `esp_driver_spi`; the app's `idf_component.yml`/`CMakeLists.txt` requires them so `driver/ledc.h` resolves.

---

## Adding LVGL on top

This demo is deliberately LVGL-free so the panel path is unambiguous. When you wire LVGL (8.3/8.4):

- Call **`lv_init()` before any other LVGL API** (skipping it leaves the timer linked-list uninitialized → garbage-size malloc → `tlsf.c block_locate_free` assert in `lv_disp_drv_register`).
- `LV_COLOR_DEPTH 16`, `LV_COLOR_16_SWAP 1`.
- Use a **full-screen / `full_refresh` flush** so the QSPI `RAMWR`+`RAMWRC` draw path (checklist #7) gets its top-to-bottom band order.
- LVGL is **single-threaded**: guard every `lv_*`/UI call with one mutex if you render from a task and mutate widgets from another, or you'll fault in `get_prop_core` during layout.
- Draw buffers: partial bands in internal DMA-capable RAM flush fine; a full-screen PSRAM buffer pushed as one giant `tx_color` can fail — flush in bands.

---

## Credits

The `esp_lcd_axs15231b` driver is **Espressif's** (Apache-2.0); this repo keeps their copyright and ships the JC3248W535EN-specific init table, packaging, the ESP-IDF 6.0 build fixes, and the `disp_on_off` contract fix + documentation. Pin/init/color details were cross-checked against the excellent community references:

- [NorthernMan54/JC3248W535EN](https://github.com/NorthernMan54/JC3248W535EN) — board pinout, schematics, working LVGL BSP
- [tsebelev/esp32_JC3248W535EN_work_exampl_lvgl_axs15231b](https://github.com/tsebelev/esp32_JC3248W535EN_work_exampl_lvgl_axs15231b) — working ESP-IDF + LVGL example

## License

Apache-2.0. See [LICENSE](LICENSE). The driver retains its original Espressif SPDX headers.
