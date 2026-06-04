# The disp_on_off Inversion — the black-screen killer

## Symptom

- Backlight is clearly **on** (you can see the panel is lit / glows).
- The screen is **pure black** — or shows only frozen power-on noise.
- Init runs without errors, `esp_lcd_panel_draw_bitmap` returns `ESP_OK`, no SPI errors in the log.
- Solid-color fills, LVGL, direct draws — **nothing** appears. Even a full-screen `0xFFFF` white doesn't show.

You can burn a *day* here chasing init tables, color formats, DMA, RASET, byte order, and tearing — none of which are the problem.

## Cause

The `esp_lcd_axs15231b` driver's display-on/off handler is written with an **inverted** boolean:

```c
static esp_err_t panel_axs15231b_disp_off(esp_lcd_panel_t *panel, bool off)
{
    int command = off ? LCD_CMD_DISPOFF : LCD_CMD_DISPON;  // bool means "OFF"
    tx_param(axs15231b, io, command, NULL, 0);
    return ESP_OK;
}
```

The esp_lcd framework passes your argument straight through to this handler. The standard esp_lcd convention is `esp_lcd_panel_disp_on_off(panel, on_off)` where **`true` means ON** — but this driver treats the bool as **"off"**. So:

| You call | Handler sees | Sends | Result |
|----------|-------------|-------|--------|
| `disp_on_off(panel, true)`  | `off = true`  | **DISPOFF** | 🟥 black, backlit, GRAM ignored |
| `disp_on_off(panel, false)` | `off = false` | **DISPON**  | ✅ display on |

If your init table doesn't include `0x29` (DISPON) itself — and the verified JC3248W535 table does **not**, it relies on `disp_on_off()` — then calling it with `true` leaves the panel's output stage **off forever**.

## Fix

```c
ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
esp_lcd_panel_disp_on_off(panel, false);   // false turns the display ON
```

That's it. Backlight + GRAM + init were all fine; the output stage was just switched off.

## Why it's so easy to miss

- It looks identical to every *other* display bug (wrong init, wrong color order, DMA failure).
- The call returns `ESP_OK` — DISPOFF is a perfectly valid command.
- The working community references (NorthernMan54, tsebelev) call it with `false` too, but it's buried among dozens of other config lines and reads like an unrelated "invert color" flag (`esp_lcd_panel_disp_on_off(panel, DISPLAY_INVERT_COLOR)`).

**Check this first** whenever an AXS15231B panel is black-but-backlit.
