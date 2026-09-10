#include <Arduino.h>
#include "pins.h"

const uint32_t BEEPER_ON_TIME_MS = 1000;

void setup()
{
    pinMode(PIN_BEEPER, OUTPUT);
    digitalWrite(PIN_BEEPER, LOW);

    Serial.begin(115200);
    delay(200);
    Serial.println("[beeper-test] ready: send G to turn GPIO27 on for 5 seconds");
}

void loop()
{
    if (Serial.available())
    {
        char command = Serial.read();
        if (command == 'G' || command == 'g')
        {
            Serial.println("[beeper-test] GPIO27 ON");
            digitalWrite(PIN_BEEPER, HIGH);
            delay(BEEPER_ON_TIME_MS);
            digitalWrite(PIN_BEEPER, LOW);
            Serial.println("[beeper-test] GPIO27 OFF");
        }
    }
}
