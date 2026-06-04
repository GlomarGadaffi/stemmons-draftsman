# Building & Flashing

## Toolchain

- **ESP-IDF v5.3.x** (the display compiles cleanly here). v6.0.x currently hits a GCC internal compiler error in `esp_lcd_panel_rgb.c`.
- Target: `esp32s3`.

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash monitor
```

## ⚠️ The git-bash trap (Windows)

ESP-IDF's `export.sh` **refuses to run under git-bash / MSYS**:

```text
ERROR: MSys/Mingw is not supported. Please follow the getting started guide ...
```

If you `source export.sh` in a git-bash shell, `idf.py` never lands on `PATH`, and depending on your wrapper the build can **silently no-op or reuse a stale binary** — so you flash old code and debug phantom results for hours.

**Always build from PowerShell:**
```powershell
. C:\esp\v5.3.5\esp-idf\export.ps1
idf.py build
```

After a build, sanity-check that the binary actually changed — grep it for a known string you added, or use `esptool ... verify_flash` — before trusting on-target behavior.

## Flashing (works from any shell)

`esptool` flashing is fine from git-bash, cmd, or PowerShell:

```bash
python -m esptool --chip esp32s3 -p <PORT> -b 460800 \
  --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x0     build/bootloader/bootloader.bin \
  0x8000  build/partition_table/partition-table.bin \
  0x10000 build/jc3248w535_axs15231b_demo.bin
```

The board uses the ESP32-S3 native USB-Serial/JTAG, so it enumerates as a COM port with no extra driver.

## Expected result

The screen cycles **red → green → blue → white → black** every 1.5 s and the monitor prints each color. If you see that, the panel is fully alive.
