#pragma once
// ============================================================================
// Smart Door Lock — pin map  (SSN32 / ESP32-WROOM-32E, board=esp32dev)
// Single source of truth for every GPIO in this project.
// Verified against bench wiring 2026-09-04.
// ============================================================================

// --- Card reader: ZKTeco KR602E, Wiegand-26 -------------------------------
// D0/D1 idle HIGH at ~4.9 V (measured). Each line goes through a 10k/15k
// resistor divider down to ~3.0 V before the GPIO. Divider only senses the
// line, so use plain INPUT (NOT INPUT_PULLUP, which would fight the divider).
#define PIN_WIEGAND_D0 4  // green wire
#define PIN_WIEGAND_D1 25 // white wire  (moved off GPIO16 2026-09-04: GPIO16
                          //  is dead on this board — pin-side fault confirmed
                          //  by swap test, likely WROVER PSRAM uses 16/17)

// --- Lock unlock trigger --------------------------------------------------
// Drives a transistor/opto that momentarily shorts the AP108 `PUSH` terminal
// to GND, in parallel with the physical exit button. The AP108 relay + DELAY
// makes the actual ~5 s unlock; the ESP32 only needs a brief pulse.
// Active-HIGH = fire.  Idle LOW = locked.
//
//  !! GPIO12 is a BOOT STRAPPING PIN (MTDI, flash voltage select). It MUST be
//     LOW at reset or the board may fail to boot. Keep it an output driven LOW
//     at startup, and add an external ~10k pull-DOWN so it can't float HIGH
//     during boot. See README.
#define PIN_UNLOCK 12

// --- Onboard status LED ---------------------------------------------------
// GPIO2 is a strapping pin but fine as an informational output after boot.
#define PIN_STATUS_LED 2

// --- Reader LED control ---------------------------------------------------
// Drives an optocoupler connected to the reader's LED wire.
#define PIN_READER_LED 14

// --- Beeper control -------------------------------------------------------
// Drives the beeper through an optocoupler.
#define PIN_BEEPER 27

// --- Exit-button / PUSH pulse sense ---------------------------------------
// Reads the AP108 `PUSH` terminal through a divider: idle HIGH (~3.2 V),
// press = LOW. Detect the FALLING edge to log an "out"/exit event.
// Plain INPUT (divider present).
#define PIN_EXIT_SENSE 21
