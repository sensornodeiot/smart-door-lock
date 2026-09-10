#include <Arduino.h>
#include "pins.h"

const uint32_t LED_ON_TIME_MS = 5000;

void setup()
{
    pinMode(PIN_READER_LED, OUTPUT);
    digitalWrite(PIN_READER_LED, LOW);

    Serial.begin(115200);
    delay(200);
    Serial.println("[led-test] ready: send G to turn GPIO14 on for 5 seconds");
}

void loop()
{
    if (Serial.available())
    {
        char command = Serial.read();
        if (command == 'G' || command == 'g')
        {
            Serial.println("[led-test] GPIO14 ON");
            digitalWrite(PIN_READER_LED, HIGH);
            delay(LED_ON_TIME_MS);
            digitalWrite(PIN_READER_LED, LOW);
            Serial.println("[led-test] GPIO14 OFF");
        }
    }
}
