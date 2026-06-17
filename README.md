# stemmons-draftsman

working ESP-IDF driver and bring-up demo for the Guition JC3248W535EN (ESP32-S3, AXS15231B QSPI 320x480 LCD). fixes inverted disp_on_off that black-screens the panel, plus a complete feature showcase: QSPI display + PWM backlight, capacitive touch, microSD, I2S audio, Wi-Fi, battery sensing.

## the gotcha

the manufacturer init table does NOT issue DISPON, so the panel stays backlit-but-black until you call `esp_lcd_panel_disp_on_off(panel, true)` after init. documented in main.c line 12.

## hardware

- **ESP32-S3** dual-core, WiFi/BLE
- **AXS15231B** QSPI display controller (320×480, 18-bit color)
- **FT5336** capacitive touch controller (I2C)
- **SD card slot** (SPI), configurable pull detection
- **I2S codec** (audio input/output)
- **battery monitor** (ADC on a GPIO)
- **PWM backlight** (LEDC, 10-bit, 5 kHz)

## features

| demo | what it shows |
|------|---------------|
| touchpaint | capacitive input, pixel drawing to framebuffer |
| audio | I2S mic input, line-out via internal codec DAC |
| sdcard | mount microSD and list files |
| wifi_battery | scan networks, display RSSI and battery voltage (ADC) |

menu-driven UI with idle dimming (touch wakes backlight to full).

## build & flash

```bash
idf.py set-target esp32-s3
idf.py build
idf.py flash
idf.py monitor
```

## notes

see `/docs` for hardware pinout, build checklist, and LVGL integration notes. 40 MHz is stable ceiling on QSPI clock (80 MHz yields noise on I2C touch lines).
