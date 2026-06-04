# Pinout & Hardware

Guition **JC3248W535EN**. Verified against the board's own pinout sheet.

## LCD (AXS15231B, QSPI)

| Signal | GPIO | Notes |
|--------|------|-------|
| CLK / SCK | 47 | |
| CS | 45 | |
| DC (RS) | — | NC — QSPI encodes D/C in the command opcode |
| RST | — | NC — software reset (SWRESET) |
| TE | 38 | Tearing-effect output; optional, unused in the demo |
| BL | 1 | **Backlight — PWM (LEDC)**, not a logic level |
| DATA0 | 21 | |
| DATA1 | 48 | |
| DATA2 | 40 | |
| DATA3 | 39 | |

## Capacitive touch (AXS15231B, I2C @ 400 kHz)

| Signal | GPIO | Notes |
|--------|------|-------|
| SDA | 4 | |
| SCL | 8 | |
| RST / INT | — | NC. On-board 10 kΩ pull-ups |

## SoC / memory

- ESP32-S3 (dual Xtensa LX7, 240 MHz)
- **8 MB OPI PSRAM** — frame buffers live here; `CONFIG_SPIRAM_MODE_OCT=y`
- **16 MB QIO flash** — use a custom partition table for a >1.5 MB app
- Native USB-Serial/JTAG → enumerates as a COM port, no driver needed

## Notes

- The AXS15231B is a combined LCD driver + in-cell capacitive touch controller. The LCD talks QSPI; touch talks I2C on a separate bus.
- `DC` and `RST` being NC is normal for the QSPI variant — don't try to wire them.
- The TE pin (GPIO38) enables tear-free updates if you implement vsync sync; not required to get an image (tearing ≠ black screen).
