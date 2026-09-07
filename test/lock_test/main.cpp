// ============================================================================
// lock-test  —  drop-bolt unlock trigger (Stage 4, HIGHEST RISK, MENTOR PRESENT)
//
// Pulses GPIO12 HIGH briefly to switch the transistor that shorts the AP108
// `PUSH` terminal to GND. The AP108 relay + DELAY then cuts power to the
// fail-safe drop bolt for its hold time (~5 s) and re-locks. The ESP32 never
// carries lock power and only needs a short pulse.
//
//   pio run -e lock-test -t upload -t monitor
//   ...then send 'u' over the serial monitor to fire one unlock.
//
// GPIO12 is a strapping pin: this sketch drives it LOW immediately in setup()
// so the board boots normally. Keep the external ~10k pull-down fitted too.
// ============================================================================
#include <Arduino.h>
#include "pins.h"

const uint16_t UNLOCK_PULSE_MS = 300;   // brief PUSH tap; AP108 DELAY holds the unlock

void fireUnlock() {
  Serial.println("[lock-test] UNLOCK -> pulsing PUSH");
  digitalWrite(PIN_UNLOCK, HIGH);
  delay(UNLOCK_PULSE_MS);
  digitalWrite(PIN_UNLOCK, LOW);
  Serial.println("[lock-test] pulse done (AP108 holds unlock for its DELAY, then re-locks)");
}

void setup() {
  pinMode(PIN_UNLOCK, OUTPUT);
  digitalWrite(PIN_UNLOCK, LOW);          // MUST be LOW at boot (GPIO12 strapping pin)
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[lock-test] send 'u' to fire one unlock pulse. Mentor present.");
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'u' || c == 'U') fireUnlock();
  }
}
