/*
 * SPDX-FileCopyrightText: 2026 GlomarGadaffi
 * SPDX-License-Identifier: Apache-2.0
 *
 * USB Mass-Storage ("Mount as USB Drive") mode. The SD-card demo asks for it via
 * usb_msc_request_reboot(); a one-shot flag in RTC memory survives the reboot but
 * is wiped when USB power is removed — so unplugging the cable returns the board
 * to normal mode (with the USB-Serial/JTAG console) on the next plug-in.
 */
#pragma once

#include "ui.h"

/* True exactly once if USB-drive mode was requested before this boot (clears the flag). */
bool usb_msc_requested(void);

/* Set the one-shot flag and reboot into USB-drive mode. Does not return. */
void usb_msc_request_reboot(void);

/* Run USB Mass-Storage mode: expose the SD card to the USB host. Does not return
 * (exit by unplugging USB, which power-cycles back to normal mode). */
void usb_msc_run(ui_t *ui);
