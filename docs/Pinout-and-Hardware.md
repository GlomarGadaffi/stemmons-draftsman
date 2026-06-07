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
| INT | 3 | Touch interrupt (active-low); polled in this repo |
| RST | — | NC — software reset |

On-board **4.7 kΩ** pull-ups to 3.3 V on SDA/SCL (schematic R3/R4). The `SCL 8 / SDA 4`
mapping is the vendor's (`pincfg.h`) and is confirmed by working touch on hardware — I²C
cannot function with SCL/SDA reversed, so don't "fix" it to match a mislabeled schematic crop.

## microSD (SD_MMC, 1-bit / SDIO)

| Signal | GPIO |
|--------|------|
| CLK | 12 |
| CMD | 11 |
| D0  | 13 |

1-bit mode only (CLK/CMD/D0 are the only lines broken out). Mount with the
`sdmmc` host, not SPI.

## Audio (I²S → on-board class-D amp → speaker)

| Signal | GPIO | Notes |
|--------|------|-------|
| BCLK | 42 | via 0 Ω link |
| WS / LRCLK | 2 | via 0 Ω link |
| DOUT / DIN | 41 | via 0 Ω link |
| MCLK | — | unused |

Confirmed from the **JC3248W535 V1.0 schematic**: the amp is a **mono BTL class-D
(NS4168-style, differential VO+/VO− → LC filter)** driving the external speaker on the
**"Speak"** JST 1.25 2P connector. It is **powered from `VOUT-BAT`** (the battery/boost
rail, not 3.3 V), and its **SD/shutdown pin is tied high (~1 MΩ) → always enabled** — there
is no GPIO mute. Start/stop the I²S clock to play/silence. The speaker is **not** included
in the amp path — plug one into "Speak" or you'll hear nothing even though the demo runs.

## Power / battery

- **Type-C (USB)** supplies 5 V; native USB-Serial/JTAG (no driver needed).
- A battery charger/boost (U2) produces the `VOUT-BAT` rail. A single-cell Li-ion connects
  to the **JST 1.25 2P "Battery"** connector, with a **power slide switch (SW1)** and a
  separate **4P Power** JST.
- **Battery voltage sense:** GPIO5 = ADC1 ch4 (`BAT_DIVIDER_RATIO` in `board.h` is assumed
  1:2 — confirm the divider resistor values on your board; on USB power with no battery it
  reads the ~5 V rail).

## Rear connectors (from the vendor photo)

Boot button · Reset button · Type-C · JST 1.25 2P Battery + battery switch ·
JST 1.25 4P Power · **JST 1.25 8P IO port** (GPIO expansion, ~12 free IO) ·
JST 1.25 4P · HC1.0 4P · **Speak** (speaker).

## SoC / memory

- ESP32-S3 (dual Xtensa LX7, 240 MHz)
- **8 MB OPI PSRAM** — frame buffers live here; `CONFIG_SPIRAM_MODE_OCT=y`
- **16 MB QIO flash** — use a custom partition table for a >1.5 MB app
- Native USB-Serial/JTAG → enumerates as a COM port, no driver needed

## Notes

- The AXS15231B is a combined LCD driver + in-cell capacitive touch controller. The LCD talks QSPI; touch talks I2C on a separate bus.
- `DC` and `RST` being NC is normal for the QSPI variant — don't try to wire them.
- The TE pin (GPIO38) enables tear-free updates if you implement vsync sync; not required to get an image (tearing ≠ black screen).
