# ~~GPS / GLONASS (u-blox 7) on the JC3248W535EN~~

> ⛔ **REJECTED ALTERNATIVE (ADR-002).** On-board GPS was evaluated and **not
> adopted** — the receiver is handled off-board. This page is retained as a
> record of the analysis that led there. Everything below describes the approach
> we did **not** take.

How to add a **u-blox 7** (NEO-7 / G7020, GPS + GLONASS) receiver to this board.
There are two transports, because the board's single USB PHY makes the obvious
"just plug the USB dongle in" path more involved than it looks. Pick one — see
the open decision in [Open questions](#open-questions).

## Why this isn't just "plug in the dongle"

- The Type-C is the board's **native USB-Serial/JTAG** (console + flashing) and
  is a 5 V **input** — it *powers* the board. See
  [Pinout & Hardware](Pinout-and-Hardware.md).
- The ESP32-S3 has **one** internal USB PHY (GPIO19 = D−, GPIO20 = D+), shared
  between USB-Serial/JTAG and USB-OTG. Only **one role at a time**.
- This repo already uses that PHY as a **USB device** (TinyUSB MSC, "mount as USB
  drive"). Reading a GNSS dongle needs the opposite role — USB **host** — which
  conflicts with both the console and MSC.

| | USB host (read a dongle) | UART direct |
|---|---|---|
| Uses internal USB PHY | yes (kills console + MSC) | no |
| Needs external 5 V VBUS | **yes** | no |
| Console stays on USB | no → UART0 + adapter | **yes** |
| Free GPIOs needed | none | 1–2 (RX, optional TX) |

## Option A — USB Host (CDC-ACM)

The dongle enumerates as a CDC-ACM serial device; the S3 runs USB-OTG host and
reads NMEA. Component: the IDF USB host library + `espressif/usb_host_cdc_acm`.

Requirements / costs:

- **External 5 V VBUS.** The board can't source VBUS on its Type-C (it's a power
  input). Feed 5 V to the dongle with a powered USB-OTG "Y" cable.
- **Console moves to UART0** (GPIO43 TX / GPIO44 RX): USB-OTG seizes the PHY from
  USB-Serial/JTAG, so set `CONFIG_ESP_CONSOLE_UART_DEFAULT=y` +
  `CONFIG_ESP_CONSOLE_SECONDARY_NONE=y` and keep a USB-UART adapter handy.
- Mutually exclusive with the TinyUSB MSC "USB drive" mode (same PHY).

Bring-up checklist:

1. Reroute the console to UART0, reflash, confirm boot logs over UART.
2. Provide external 5 V VBUS to the dongle via a powered OTG cable.
3. `lsusb -v` the dongle on a PC (VID `0x1546`). `bInterfaceClass 0x02` (CDC) →
   the standard host-open path works; `0xFF` (vendor-specific) → open by explicit
   interface + bulk endpoints instead.
4. Expect `$GNGGA` / `$GPGSV` / `$GLGSV` within a few seconds of attach; a cold
   position fix follows in ~26 s.

## Option B — UART direct ~~(recommended for most cases)~~

Wire the u-blox **UART** to free GPIOs on the rear **JST 1.25 8P IO port**
(~12 free IO; see [Pinout & Hardware](Pinout-and-Hardware.md)). No VBUS rig, no
PHY fight, the USB console stays alive.

| u-blox pin | ESP32-S3 | Notes |
|---|---|---|
| TX  | a free GPIO (RX) | NMEA out, 9600 8N1 default |
| RX  | a free GPIO (TX) | optional — only needed to send UBX config |
| GND | GND | |
| VCC | 3V3 (or 5V per module) | check the module's regulator |

Pick two GPIOs from the 8P IO port that are **not** in the LCD / touch / SD / I²S
map. Drive it with `uart_driver_install` + `uart_read_bytes`, then parse the same
NMEA as Option A.

> A **sealed USB-stick** dongle usually does *not* expose UART; a **bare module**
> (NEO-7M breakout) does. That fact decides A vs B — see #3.

## NMEA / GLONASS notes

- Default output: **NMEA-0183, 9600 baud, 1 Hz**. Useful sentences:
  - `GGA` — fix quality, lat/lon, altitude, sats used, HDOP
  - `RMC` — UTC date/time, status (A/V), speed, course
  - `GSV` — satellites in view, **per constellation** (`GP` = GPS, `GL` = GLONASS)
- Coordinates are `ddmm.mmmm`; convert to decimal degrees and apply the N/S/E/W
  sign.
- **Concurrent GPS + GLONASS:** the 7-series tracks two constellations at once. If
  `$GLGSV` never appears, send a UBX `CFG-GNSS` frame to enable the GPS+GLONASS
  pair (needs the TX line in Option B, or the OUT endpoint in Option A).

## Decision

**Resolved (ADR-002): on-board GPS was not adopted.** The unit is a sealed USB
stick, so it is handled off-board. Issues #3 / #4 / #5 are closed as superseded.
This page is kept as the rejected-alternative record.
