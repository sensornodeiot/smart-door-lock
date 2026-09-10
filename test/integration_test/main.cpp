#include <Arduino.h>
#include "pins.h"

const uint16_t WIEGAND_FRAME_GAP_MS = 25;
const uint16_t EXIT_PRESS_DEBOUNCE_MS = 25;
const uint16_t EXIT_RELEASE_DEBOUNCE_MS = 250;
const uint16_t UNLOCK_PULSE_MS = 300;
const uint32_t READER_LED_TEST_MS = 5000;
const uint32_t BEEPER_TEST_MS = 1000;

volatile uint64_t wiegandData = 0;
volatile uint16_t wiegandBits = 0;
volatile uint32_t wiegandLastPulse = 0;

uint32_t readerLedOffAt = 0;
uint32_t beeperOffAt = 0;
uint32_t lastUnlockAt = 0;

void IRAM_ATTR onWiegandD0()
{
    wiegandData <<= 1;
    wiegandBits++;
    wiegandLastPulse = millis();
}

void IRAM_ATTR onWiegandD1()
{
    wiegandData = (wiegandData << 1) | 1;
    wiegandBits++;
    wiegandLastPulse = millis();
}

void printRawKey(uint64_t data)
{
    uint32_t high = (uint32_t)(data >> 32);
    uint32_t low = (uint32_t)data;

    if (high)
        Serial.printf("%lX%08lX", (unsigned long)high, (unsigned long)low);
    else
        Serial.printf("%lX", (unsigned long)low);
}

void startFeedbackTest()
{
    digitalWrite(PIN_READER_LED, HIGH);
    digitalWrite(PIN_BEEPER, HIGH);
    readerLedOffAt = millis() + READER_LED_TEST_MS;
    beeperOffAt = millis() + BEEPER_TEST_MS;
    Serial.println("[integration] LED on for 5 s; beeper on for 1 s");
}

void serviceFeedback()
{
    uint32_t now = millis();

    if (readerLedOffAt != 0 && (int32_t)(now - readerLedOffAt) >= 0)
    {
        digitalWrite(PIN_READER_LED, LOW);
        readerLedOffAt = 0;
        Serial.println("[integration] reader LED off");
    }

    if (beeperOffAt != 0 && (int32_t)(now - beeperOffAt) >= 0)
    {
        digitalWrite(PIN_BEEPER, LOW);
        beeperOffAt = 0;
        Serial.println("[integration] beeper off");
    }
}

void fireUnlock()
{
    Serial.println("[integration] UNLOCK pulse");
    lastUnlockAt = millis();
    digitalWrite(PIN_UNLOCK, HIGH);
    delay(UNLOCK_PULSE_MS);
    digitalWrite(PIN_UNLOCK, LOW);
    Serial.println("[integration] unlock pulse complete");
}

void serviceExitSense()
{
    static int candidate = HIGH;
    static int stable = HIGH;
    static uint32_t changedAt = 0;
    int current = digitalRead(PIN_EXIT_SENSE);

    if (current != candidate)
    {
        candidate = current;
        changedAt = millis();
    }

    uint32_t stableFor = millis() - changedAt;
    if (candidate == LOW && stable == HIGH && stableFor >= EXIT_PRESS_DEBOUNCE_MS)
    {
        stable = LOW;
        if (millis() - lastUnlockAt > 500)
        {
            Serial.println("[integration] EXIT press detected");
            startFeedbackTest();
        }
    }
    else if (candidate == HIGH && stable == LOW && stableFor >= EXIT_RELEASE_DEBOUNCE_MS)
    {
        stable = HIGH;
        Serial.println("[integration] EXIT released");
    }
}

void reportWiegandFrame()
{
    noInterrupts();
    uint64_t data = wiegandData;
    uint16_t bits = wiegandBits;
    wiegandData = 0;
    wiegandBits = 0;
    interrupts();

    Serial.printf("[integration] frame bits=%u raw=0x", bits);
    printRawKey(data);
    Serial.println();

    if (bits == 26 || bits == 34)
    {
        uint32_t facilityMask = bits == 26 ? 0xFF : 0xFFFF;
        uint32_t facility = (uint32_t)((data >> 17) & facilityMask);
        uint32_t card = (uint32_t)((data >> 1) & 0xFFFF);
        uint8_t parityGroupWidth = bits == 26 ? 13 : 17;
        uint64_t firstParityGroup =
            (data >> (bits - parityGroupWidth)) & ((1ULL << parityGroupWidth) - 1);
        uint64_t lastParityGroup = data & ((1ULL << (parityGroupWidth - 1)) - 1);
        bool firstParityEven = (__builtin_popcountll(firstParityGroup) % 2) == 0;
        bool lastParityOdd = (__builtin_popcountll(lastParityGroup) % 2) == 1;

        Serial.printf("[integration] Wiegand-%u facility=%lu card=%lu parity=%s\n",
                      bits, (unsigned long)facility, (unsigned long)card,
                      firstParityEven && lastParityOdd ? "OK" : "CHECK");
    }
    else if (bits == 4)
    {
        Serial.printf("[integration] keypad key=%u\n", (unsigned)(data & 0xF));
    }
    else if (bits == 8)
    {
        uint8_t key = data & 0xF;
        uint8_t complement = (data >> 4) & 0xF;
        bool valid = complement == (uint8_t)(~key & 0xF);
        Serial.printf("[integration] keypad key=%u 8-bit=%s\n",
                      key, valid ? "OK" : "CHECK");
    }
    else
    {
        Serial.println("[integration] unexpected Wiegand length");
    }
}

void serviceWiegand()
{
    if (wiegandBits > 0 && millis() - wiegandLastPulse > WIEGAND_FRAME_GAP_MS)
        reportWiegandFrame();
}

void printHelp()
{
    Serial.println();
    Serial.println("[integration] commands:");
    Serial.println("  status  - print pin assignments and idle input state");
    Serial.println("  feedback - test reader LED and beeper");
    Serial.println("  UNLOCK  - pulse the lock trigger (explicit, high risk)");
    Serial.println("  help    - print this list");
    Serial.println("[integration] also tap a card/keypad and press the exit button");
}

void printStatus()
{
    Serial.printf("[integration] D0=%d D1=%d unlock=%d status=%d LED=%d beeper=%d exit=%d\n",
                  PIN_WIEGAND_D0, PIN_WIEGAND_D1, PIN_UNLOCK, PIN_STATUS_LED,
                  PIN_READER_LED, PIN_BEEPER, PIN_EXIT_SENSE);
    Serial.printf("[integration] exit input=%s\n",
                  digitalRead(PIN_EXIT_SENSE) == HIGH ? "HIGH/idle" : "LOW/pressed");
}

void handleCommand(String command)
{
    command.trim();

    if (command == "help")
        printHelp();
    else if (command == "status")
        printStatus();
    else if (command == "feedback")
        startFeedbackTest();
    else if (command == "UNLOCK")
        fireUnlock();
    else if (command.length())
        Serial.println("[integration] unknown command; type help");
}

void setup()
{
    pinMode(PIN_UNLOCK, OUTPUT);
    digitalWrite(PIN_UNLOCK, LOW);
    pinMode(PIN_STATUS_LED, OUTPUT);
    digitalWrite(PIN_STATUS_LED, LOW);
    pinMode(PIN_READER_LED, OUTPUT);
    digitalWrite(PIN_READER_LED, LOW);
    pinMode(PIN_BEEPER, OUTPUT);
    digitalWrite(PIN_BEEPER, LOW);
    pinMode(PIN_EXIT_SENSE, INPUT);
    pinMode(PIN_WIEGAND_D0, INPUT);
    pinMode(PIN_WIEGAND_D1, INPUT);

    Serial.begin(115200);
    delay(200);
    attachInterrupt(digitalPinToInterrupt(PIN_WIEGAND_D0), onWiegandD0, FALLING);
    attachInterrupt(digitalPinToInterrupt(PIN_WIEGAND_D1), onWiegandD1, FALLING);

    Serial.println("\n[integration] smart-door-lock hardware-in-the-loop test");
    Serial.println("[integration] GPIO12 is held LOW at boot; unlock requires exact 'UNLOCK'");
    printHelp();
}

void loop()
{
    serviceFeedback();
    serviceExitSense();
    serviceWiegand();

    static String command;
    while (Serial.available())
    {
        char character = Serial.read();
        if (character == '\n' || character == '\r')
        {
            if (command.length())
            {
                handleCommand(command);
                command = "";
            }
        }
        else if (command.length() < 32)
        {
            command += character;
        }
    }
}
