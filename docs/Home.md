# JC3248W535EN / AXS15231B — Bring-up Wiki

Everything you need to get the **Guition JC3248W535EN** (ESP32-S3 + AXS15231B QSPI 320×480 display + cap touch) actually rendering — and every trap that makes it look dead.

If you're here because **your panel is black but the backlight is on**, jump straight to **[the DISPON gotcha](disp_on_off-Inversion.md)**. That's the one that costs everyone a day.

## Pages

- **[The DISPON / disp_on_off gotcha](disp_on_off-Inversion.md)** — the black-screen killer. Read this first.
- **[Bring-Up Checklist](Bring-Up-Checklist.md)** — the complete ordered list of everything that has to be right.
- **[Adding LVGL](Adding-LVGL.md)** — wiring LVGL 8.x on top without the classic crashes.
- **[Pinout & Hardware](Pinout-and-Hardware.md)** — pins, backlight, PSRAM, flash.
- **[Building & Flashing](Building-and-Flashing.md)** — toolchain, the git-bash trap, flashing.

## TL;DR

```c
esp_lcd_panel_disp_on_off(panel, true);  // true == ON (standard esp_lcd contract).
```

```text
✔ disp_on_off(true) to turn ON        ✔ LEDC PWM backlight (not gpio level)
✔ correct JC3248W535 init table       ✔ big-endian RGB565 (LV_COLOR_16_SWAP=1)
✔ touch scl_speed_hz = 400000         ✔ lv_init() before any LVGL call
✔ 4 MB app partition                  ✔ build from PowerShell, not git-bash
```

## Repo

The [main repo](https://github.com/GlomarGadaffi/esp-idf-jc3248w535-axs15231b) ships a buildable driver + a no-LVGL color-test demo that proves the panel in ~30 lines of `app_main`.

## Credits

Driver is Espressif's `esp_lcd_axs15231b` (Apache-2.0). Hardware/init details cross-checked against [NorthernMan54/JC3248W535EN](https://github.com/NorthernMan54/JC3248W535EN) and [tsebelev/esp32_JC3248W535EN_work_exampl_lvgl_axs15231b](https://github.com/tsebelev/esp32_JC3248W535EN_work_exampl_lvgl_axs15231b).
