// ============================================================================
// wiegand-test  —  card reader bring-up (Stage 2)
//
// Reads the ZKTeco KR602E Wiegand-26 output on D0=GPIO4 (green), D1=GPIO16
// (white) via the 10k/15k dividers, and prints the bit count + decoded
// facility/card + a parity check for every card tap.
//
//   pio run -e wiegand-test -t upload -t monitor
//
// Expect: tap a card -> "bits=26 ... parity=OK". Any other bit count means
// noise / partial capture -> check the dividers and the COMMON GROUND first.
// ============================================================================
#include <Arduino.h>
#include "pins.h"

// Wiegand bits are shifted in MSB-first as they arrive (first bit ends up in
// the highest position). 64-bit accumulator covers 26/34/37-bit formats.
volatile uint64_t wgData = 0;
volatile uint16_t wgBits = 0;
volatile uint32_t wgLast = 0;
volatile uint16_t wgD0   = 0;   // per-line pulse counts (diagnostic)
volatile uint16_t wgD1   = 0;

void IRAM_ATTR onD0() { wgData <<= 1;              wgBits++; wgD0++; wgLast = millis(); }
void IRAM_ATTR onD1() { wgData = (wgData << 1) | 1; wgBits++; wgD1++; wgLast = millis(); }

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[wiegand-test] tap a card...");

  pinMode(PIN_WIEGAND_D0, INPUT);   // divider present — do NOT use INPUT_PULLUP
  pinMode(PIN_WIEGAND_D1, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_WIEGAND_D0), onD0, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_WIEGAND_D1), onD1, FALLING);
}

void loop() {
  // A card is complete once the lines have been idle for >25 ms.
  if (wgBits > 0 && (millis() - wgLast) > 25) {
    noInterrupts();
    uint64_t data = wgData; uint16_t bits = wgBits;
    uint16_t d0 = wgD0, d1 = wgD1;
    wgData = 0; wgBits = 0; wgD0 = 0; wgD1 = 0;
    interrupts();

    // Print the 64-bit raw as two 32-bit halves — ESP32 printf %llX is unreliable.
    uint32_t hi = (uint32_t)(data >> 32), lo = (uint32_t)data;
    if (hi) Serial.printf("bits=%u  D0=%u D1=%u  raw=0x%lX%08lX\n", bits, d0, d1, hi, lo);
    else    Serial.printf("bits=%u  D0=%u D1=%u  raw=0x%lX\n",       bits, d0, d1, lo);

    if (d1 == 0) Serial.println("  >> D1 count is 0 - white/GPIO16 path is dead (swap-test it)");

    if (bits == 26 || bits == 34) {
      // Card frame: [EP][facility][card(16)][OP]. Facility is 8 bits on W26,
      // 16 bits on W34. Use the full 64-bit value (W34 doesn't fit in 32 bits).
      uint32_t facMask  = (bits == 26) ? 0xFF : 0xFFFF;
      uint32_t facility = (uint32_t)((data >> 17) & facMask);
      uint32_t card     = (uint32_t)((data >> 1)  & 0xFFFF);
      Serial.printf("  Wiegand-%u  facility=%lu  card=%lu\n",
                    bits, (unsigned long)facility, (unsigned long)card);
      // Full raw — must NOT truncate to 32 bits (W34 has bits 33..32).
      uint32_t khi = (uint32_t)(data >> 32), klo = (uint32_t)data;
      if (khi) Serial.printf("  >> whitelist key = %lX%08lX (full raw)\n",
                             (unsigned long)khi, (unsigned long)klo);
      else     Serial.printf("  >> whitelist key = %lX (full raw)\n", (unsigned long)klo);
    } else if (bits == 4) {
      Serial.printf("  keypad key = %u\n", (unsigned)(data & 0xF));
    } else if (bits == 8) {
      uint8_t key = data & 0xF, comp = (data >> 4) & 0xF;   // key + inverted key
      Serial.printf("  keypad key = %u  (8-bit %s)\n",
                    key, (comp == (uint8_t)(~key & 0xF)) ? "OK" : "check");
    } else {
      Serial.println("  (unexpected length - noise/partial: check dividers & common ground)");
    }
  }
}
