# JC3248W535EN — GPIO pinout

Verified against the Guition board's own pinout sheet (`2025/02/01` rev).

## LCD (AXS15231B, QSPI)

| Signal | GPIO | Notes |
|--------|------|-------|
| CLK / SCK | 47 | |
| CS | 45 | |
| DC (RS) | — | Not connected (QSPI encodes D/C in the command) |
| RST | — | Not connected (software reset via SWRESET) |
| TE | 38 | Tearing-effect output (optional; unused in this demo) |
| BL | 1 | **Backlight — PWM driven** (LEDC), not a logic level |
| DATA0 | 21 | |
| DATA1 | 48 | |
| DATA2 | 40 | |
| DATA3 | 39 | |

## Capacitive touch (AXS15231B, I2C)

| Signal | GPIO | Notes |
|--------|------|-------|
| SDA | 4 | |
| SCL | 8 | |
| RST | — | Not connected |
| INT | — | Not connected |

I2C runs at 400 kHz. Module has on-board 10 kΩ pull-ups.

## Board

- MCU: ESP32-S3 (dual-core Xtensa, 240 MHz)
- PSRAM: 8 MB OPI
- Flash: 16 MB QIO
- Display: 320 × 480, RGB565
- USB: native USB-Serial/JTAG (appears as COM port)
