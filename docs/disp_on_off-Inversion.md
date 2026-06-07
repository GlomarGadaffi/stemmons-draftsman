# DISPON / disp_on_off — the black-screen killer

## Symptom

- Backlight is clearly **on** (you can see the panel is lit / glows).
- The screen is **pure black** — or shows only frozen power-on noise.
- Init runs without errors, `esp_lcd_panel_draw_bitmap` returns `ESP_OK`, no SPI errors in the log.
- Solid-color fills, LVGL, direct draws — **nothing** appears. Even a full-screen `0xFFFF` white doesn't show.

You can burn a *day* here chasing init tables, color formats, DMA, RASET, byte order, and tearing — none of which are the problem.

## Cause

The verified JC3248W535EN init table does **not** include `0x29` (DISPON) — it relies on the
caller turning the display output stage on after init. If you never call `disp_on_off`,
the output stage stays off forever and GRAM is never scanned out.

## Fix

```c
ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
esp_lcd_panel_disp_on_off(panel, true);   // true turns the display ON
```

That's it. Backlight + GRAM + init were all fine; the output stage was just switched off.

| You call | Sends | Result |
|----------|-------|--------|
| *(never call disp_on_off)* | — | 🟥 black, backlit, GRAM ignored |
| `disp_on_off(panel, false)` | **DISPOFF** | 🟥 black, backlit, GRAM ignored |
| `disp_on_off(panel, true)`  | **DISPON**  | ✅ display on |

## Historical note: the inverted-handler bug (fixed in this repo)

Earlier revisions of the `esp_lcd_axs15231b` driver bound the **legacy** `disp_off(bool off)`
semantics to the **modern** `disp_on_off(bool on_off)` function pointer. The esp_lcd framework
(IDF ≥ 5.0) passes your argument straight through and expects **`true` to mean ON**, but the old
handler treated the bool as *"off"* — so you had to pass `false` to turn the panel on:

```c
// OLD, inverted handler — DO NOT reintroduce:
static esp_err_t panel_axs15231b_disp_off(esp_lcd_panel_t *panel, bool off)
{
    int command = off ? LCD_CMD_DISPOFF : LCD_CMD_DISPON;  // bool meant "OFF"
    ...
}
```

**Why that was worse than just a confusing API:** the moment you layer LVGL via
[`esp_lvgl_port`](https://components.espressif.com/components/espressif/esp_lvgl_port), the port
calls `esp_lcd_panel_disp_on_off(panel, true)` internally per the standard contract — and the
inverted handler turned the panel **off**, black-screening the UI with no obvious cause, because
the offending call lives inside a third-party component you don't control.

This repo **fixes the handler** so it honors the framework contract:

```c
static esp_err_t panel_axs15231b_disp_on_off(esp_lcd_panel_t *panel, bool on_off)
{
    int command = on_off ? LCD_CMD_DISPON : LCD_CMD_DISPOFF;  // true == ON
    ...
}
```

If you're porting code written against an older fork that passed `false` to turn the display on,
**flip it to `true`.**

## Why it's so easy to miss

- It looks identical to every *other* display bug (wrong init, wrong color order, DMA failure).
- The call returns `ESP_OK` — both DISPON and DISPOFF are perfectly valid commands.
- Older community references (NorthernMan54, tsebelev) call it with `false` because they target
  the *inverted* driver; copy that line onto the fixed driver and you turn the panel back off.

**Check this first** whenever an AXS15231B panel is black-but-backlit.
