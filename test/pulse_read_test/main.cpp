// ============================================================================
// pulse-test  —  exit-button / PUSH pulse sense (Stage 5)  [DEBOUNCED]
//
// GPIO21 reads the AP108 `PUSH` sense (via a 4.7k/10k divider): idle HIGH,
// LOW while the exit button is held. A mechanical button bounces, so one press
// shows up as several HIGH/LOW flips — this sketch debounces that into ONE
// "EXIT press" event.
//
//   pio run -e pulse-test -t upload -t monitor
//   press the exit button -> a single "EXIT press #N" per press
//
// Integration note (Stage 4+): the ESP32's own unlock also pulls PUSH LOW
// (sense + trigger share the terminal), so the real app must NOT log an exit
// when it just commanded an unlock.
// ============================================================================
#include <Arduino.h>
#include "pins.h"

const uint16_t DEBOUNCE_MS = 250;   // one press can't retrigger within this window

void setup() {
  Serial.begin(115200);
  delay(200);
  pinMode(PIN_EXIT_SENSE, INPUT);   // divider on PUSH — plain INPUT
  Serial.println("\n[pulse-test] press the exit button (debounced). idle=HIGH, press=LOW.");
}

void loop() {
  static int      last     = HIGH;
  static uint32_t lastEdge = 0;
  static uint32_t count    = 0;
  int now = digitalRead(PIN_EXIT_SENSE);

  // Register a HIGH->LOW press, then ignore bounce for DEBOUNCE_MS.
  if (last == HIGH && now == LOW && (millis() - lastEdge) > DEBOUNCE_MS) {
    count++;
    lastEdge = millis();
    Serial.printf("EXIT press #%u\n", count);
  }
  last = now;
}
