# esp-idf-jc3248w535-axs15231b

A **working, hardware-validated** ESP-IDF driver **and full feature-showcase app** for the **Guition JC3248W535EN** — the cheap 3.5″ ESP32-S3 board with an **AXS15231B** QSPI display (320×480) and in-cell capacitive touch.

The vendor ZIP doesn't build cleanly, and most forks render nothing, a garbled image, or a black screen. This repo boots into a **touch-driven menu** that demonstrates **every feature of the board** — display, backlight, capacitive touch, microSD, I²S audio, Wi-Fi, and battery sensing — in plain ESP-IDF (no Arduino, no LVGL), and **documents every gotcha** that makes this board look dead.

> **Status:** built with **ESP-IDF 6.0.1** and flashed to a real JC3248W535EN. All demos verified on hardware: SD card mounts and lists files, audio plays through the speaker, Wi-Fi scans, touch + display work. Also builds on ESP-IDF **5.3+**.

---

## What it does

On boot you get a home menu with four labeled bands. Tap one to run a demo; tap the **`< BACK`** bar to return.

| Demo | Exercises | On screen |
|------|-----------|-----------|
| **Touch Paint** | display + capacitive touch | paints color-cycling brush strokes where you touch |
| **microSD** | SD_MMC (1-bit SDIO) | mounts the card, shows its name/capacity + root folder listing (or `NO CARD`) |
| **Audio** | I²S → NS4168/AX98357A class-D amp | plays an A-major arpeggio through the speaker |
| **WiFi+Battery** | Wi-Fi STA radio + ADC1 | credential-free AP scan count + live `Batt: x.xx V` readout |

All rendering uses a tiny software framebuffer (`ui.c`) with an 8×8 bitmap font — no LVGL dependency.

---

## ⚠️ The one thing that wastes everyone a day

The verified JC3248W535EN init table does **not** issue `DISPON` (0x29) itself, so you **must** turn the display output stage on after init — otherwise the panel stays perfectly black but perfectly backlit, no matter what you write to GRAM:

```c
ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
esp_lcd_panel_disp_on_off(panel, true);   // true == ON (standard esp_lcd contract)
```

If your backlight is on but the screen is black no matter what you draw, a missing/incorrect `disp_on_off` is almost certainly why. **Check it first.**

> **Porting from an older fork?** Earlier revisions of the `esp_lcd_axs15231b` driver had an **inverted** `disp_on_off()` handler where `true` meant *OFF*. **This repo fixes that** — the handler follows the standard esp_lcd contract (`true == ON`), so it also composes correctly with LVGL / `esp_lvgl_port`. If your old code passed `false` to turn the display on, flip it to `true`.

---

## Quick start

```powershell
# ESP-IDF 5.3 ... 6.0 (validated on 6.0.1). Windows: use PowerShell, not git-bash.
. C:\esp\v6.0.1\esp-idf\export.ps1
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash monitor
```

You should see the home menu appear with four labeled bands, and the serial log print:

```
I (...) jc3248w535: JC3248W535EN feature showcase
I (...) lcd_panel.axs15231b: LCD panel create success, version: 1.1.0
I (...) jc3248w535: home menu ready — tap a band to launch a demo (top->bottom):
I (...) jc3248w535:   [0] Touch Paint
I (...) jc3248w535:   [1] microSD
I (...) jc3248w535:   [2] Audio
I (...) jc3248w535:   [3] WiFi+Battery
```

The microSD / Audio demos are most interesting with a **FAT-formatted microSD inserted** (the demo just lists the root directory; it doesn't require any particular files).

> **Windows / git-bash trap:** ESP-IDF's `export.sh` **aborts under git-bash/MSYS** (`MSys/Mingw is not supported`); `idf.py` never lands on `PATH` and the build can silently no-op, so you flash stale code and chase phantom results. Build from **PowerShell** with `export.ps1`. See [Building & Flashing](docs/Building-and-Flashing.md).

---

## Project layout

```
components/esp_lcd_axs15231b/   # the panel + touch driver (Espressif's, Apache-2.0)
main/
  main.c               # init (display/backlight/touch) + touch home menu + dispatch
  board.h              # central pin map for every peripheral
  ui.c / ui.h          # software framebuffer: rects, 8x8 text, bounce-buffered flush
  font8x8.h            # public-domain 8x8 bitmap font
  demos.h              # the demo interface (one run() per feature)
  demo_touchpaint.c    # } one self-contained file per feature, so they can be
  demo_sdcard.c        # } developed / read independently. Each takes over the
  demo_audio.c         # } screen and returns to the menu on a back-bar tap.
  demo_wifi_battery.c  # }
```

---

## Pinout

See [docs/Pinout-and-Hardware.md](docs/Pinout-and-Hardware.md). Everything is also in [`main/board.h`](main/board.h).

| Peripheral | Pins |
|-----------|------|
| **Display** (AXS15231B QSPI) | CS 45, CLK 47, D0–D3 = 21/48/40/39, BL 1 (PWM), TE 38; DC/RST not connected |
| **Touch** (AXS15231B I²C, 0x3B) | SDA 4, SCL 8, INT 3 |
| **microSD** (SD_MMC 1-bit) | CLK 12, CMD 11, D0 13 |
| **Audio** (I²S → speaker amp) | BCLK 42, WS 2, DOUT 41 |
| **Battery** sense | ADC1 ch4 (GPIO5) |

QSPI encodes D/C in the opcode (no DC pin), and reset is software `SWRESET` — which is why GPIO8 is free to serve as touch SCL.

---

## Bring-up checklist

Every one of these was required to get from "dead board" to "rendering UI + all peripherals", in rough order of how badly each one bites:

1. **`disp_on_off(panel, true)` to turn the display ON** — the init table doesn't issue DISPON (see above). The black-screen killer.
2. **Use the right init sequence.** The AXS15231B ships in several panel variants; the wrong gamma/power table shows nothing or garbage. The table baked into this driver is verified for the JC3248W535EN and ends by setting the full CASET/RASET window (`0x2A`/`0x2B`).
3. **Backlight is PWM, not a GPIO level.** Drive `BL` (GPIO1) with LEDC at full duty. A bare `gpio_set_level(1)` looks dim/dead.
4. **Big-endian RGB565.** The panel wants byte-swapped pixels (`ui_rgb()` handles it). In raw esp_lcd swap each `uint16_t`; with LVGL set `LV_COLOR_16_SWAP 1`.
5. **Touch I²C `scl_speed_hz`.** `ESP_LCD_TOUCH_IO_I2C_AXS15231B_CONFIG()` leaves it `0`; the `i2c_master` driver rejects that — set `400000`.
6. **Flush the LCD from internal RAM, not straight from PSRAM.** The framebuffer lives in PSRAM; DMAing it directly to the QSPI panel underflows the SPI TX FIFO when another bus master (e.g. **SDMMC**) contends for PSRAM bandwidth — `DMA TX underflow` → every draw fails → **frozen screen**. `ui_flush()` copies each band into a small internal-RAM bounce buffer and waits on a color-trans-done semaphore before reuse. (This bit us specifically when opening the microSD demo.)
7. **Top-band detection must precede the gap offset.** The QSPI path skips per-flush RASET and fills via `RAMWR` (top band, `y_start==0`) + `RAMWRC` continuation; that test must be taken from the LVGL-supplied `y_start` *before* `y_gap` is added, or a non-zero gap silently demotes `RAMWR` to `RAMWRC` and garbles the frame. Expects a **full-screen, top-to-bottom** flush.
8. **App partition size.** A real UI binary overflows `SINGLE_APP_LARGE`; this repo ships a custom `partitions.csv` with a 4 MB app slot on the 16 MB flash.

---

## ESP-IDF 6.0 notes

Built on the 6.0 series (validated on 6.0.1). API specifics already handled in the source — relevant if you adapt this elsewhere:

- **`color_space` was removed** from `esp_lcd_panel_dev_config_t`; the driver uses `rgb_ele_order`.
- **The monolithic `driver` component was split** — LEDC/GPIO/SPI/I²C/I²S/SDMMC live in `esp_driver_*`; `main/CMakeLists.txt` requires them explicitly.
- **New I²C master driver** — touch uses `i2c_new_master_bus` → `esp_lcd_new_panel_io_i2c`.
- **Touch:** `board_touch()` uses `esp_lcd_touch_get_data()` (the non-deprecated replacement for `esp_lcd_touch_get_coordinates()`).
- **SD/Wi-Fi:** `esp_vfs_fat_sdcard_unmount()` and `esp_netif_destroy_default_wifi()` are the current teardown names.

---

## Known caveats

- **Battery voltage** reads the ~5 V USB rail when no battery is attached; `BAT_DIVIDER_RATIO` (assumed 1:2 in `board.h`) should be confirmed against an actual battery on the JST connector.
- On-screen output is text + colored shapes only (no images/widgets) — that's by design; this is a peripheral showcase, not a UI toolkit. To build a real UI, layer LVGL (next section).

---

## Adding LVGL on top

The showcase is deliberately LVGL-free so the panel path is unambiguous. When you wire LVGL (8.3/8.4) — full guide in [docs/Adding-LVGL.md](docs/Adding-LVGL.md):

- Call **`lv_init()` before any other LVGL API**.
- `LV_COLOR_DEPTH 16`, `LV_COLOR_16_SWAP 1`.
- Use a **full-screen / `full_refresh` flush** so the QSPI `RAMWR`+`RAMWRC` draw path gets its top-to-bottom band order.
- LVGL is **single-threaded** — guard `lv_*` calls with one mutex.
- Flush in **bands from internal DMA-capable RAM** (checklist #6), not one giant PSRAM `tx_color`.

---

## 📖 Documentation

Full bring-up docs in [`docs/`](docs/Home.md):

- **[The DISPON / disp_on_off gotcha](docs/disp_on_off-Inversion.md)** — the black-screen killer, in depth
- **[Bring-Up Checklist](docs/Bring-Up-Checklist.md)** — everything that has to be right, ordered by pain
- **[Adding LVGL](docs/Adding-LVGL.md)** · **[Pinout & Hardware](docs/Pinout-and-Hardware.md)** · **[Building & Flashing](docs/Building-and-Flashing.md)**

---

## Credits

The `esp_lcd_axs15231b` driver is **Espressif's** (Apache-2.0); this repo keeps their copyright and adds the JC3248W535EN-specific init table, the feature-showcase app, the ESP-IDF 6.0 build fixes, the `disp_on_off` contract fix, the PSRAM-DMA bounce-buffer flush, and the documentation. Pin/init/color details were cross-checked against:

- [NorthernMan54/JC3248W535EN](https://github.com/NorthernMan54/JC3248W535EN) — board pinout, schematics, working LVGL BSP, and the original DEMO_LVGL/PIC/MJPEG/MP3 references
- [tsebelev/esp32_JC3248W535EN_work_exampl_lvgl_axs15231b](https://github.com/tsebelev/esp32_JC3248W535EN_work_exampl_lvgl_axs15231b) — working ESP-IDF + LVGL example

## License

Apache-2.0. See [LICENSE](LICENSE). The driver retains its original Espressif SPDX headers.
