// ============================================================================
// Smart Door Lock — main application
//
// STAGE 6 + KEYPAD: persistent NVS whitelist, serial admin console, plus the
// reader keypad as (a) a PIN unlock and (b) a master-code enrollment path.
//
//   Card tap            -> enrolled card GRANTS -> unlock (Stage 4)
//   Keypad <code>#      -> valid door PIN GRANTS -> unlock ; * clears
//   Keypad <master>#    -> arm enroll -> next card tapped is added (no unlock)
//
//   Serial console (115200), one command per line:
//     list | enroll [name] | cancel | del <hexkey> | clear
//     pinlist | pinadd <code> [name] | pindel <code>
//     master <code>            (default "1234" — CHANGE IT)
//     help
//
// SAFETY: same fail-safe lock wiring as Stage 4 (bolt through AP108 NC).
// ============================================================================
#include <Arduino.h>
#include <Preferences.h>
#include "pins.h"

// --- Persistent stores (NVS) -----------------------------------------------
struct Card { uint64_t key;   char name[16]; };
struct Pin  { char code[12];  char name[16]; };
const int MAX_CARDS = 32, MAX_PINS = 16;

Card cards[MAX_CARDS]; int numCards = 0;
Pin  pins[MAX_PINS];   int numPins  = 0;
char masterCode[12] = "1234";
Preferences prefs;

const Card SEED[] = {                    // seeded on first empty boot
  { 0x595F9C9EULL,  "Card A" },
  { 0x25956282DULL, "Card B" },
  { 0x3C1906912ULL, "Card C" },
};

// --- Wiegand capture --------------------------------------------------------
volatile uint64_t wgData = 0;
volatile uint16_t wgBits = 0;
volatile uint32_t wgLast = 0;

void IRAM_ATTR onD0() { wgData <<= 1;               wgBits++; wgLast = millis(); }
void IRAM_ATTR onD1() { wgData = (wgData << 1) | 1;  wgBits++; wgLast = millis(); }

// --- Small helpers ----------------------------------------------------------
void printKey(uint64_t k) {
  uint32_t hi = (uint32_t)(k >> 32), lo = (uint32_t)k;
  if (hi) Serial.printf("%lX%08lX", (unsigned long)hi, (unsigned long)lo);
  else    Serial.printf("%lX", (unsigned long)lo);
}

// --- Card store -------------------------------------------------------------
void saveCards() { prefs.putBytes("cards", cards, numCards * sizeof(Card)); }

void loadCards() {
  size_t len = prefs.getBytesLength("cards");
  if (len == 0 || len % sizeof(Card) != 0 || len > sizeof(cards)) {
    numCards = sizeof(SEED) / sizeof(SEED[0]);
    memcpy(cards, SEED, sizeof(SEED));
    saveCards();
    Serial.println("[nvs] seeded 3 default cards");
    return;
  }
  numCards = len / sizeof(Card);
  prefs.getBytes("cards", cards, len);
}

const char* cardLookup(uint64_t key) {
  for (int i = 0; i < numCards; i++) if (cards[i].key == key) return cards[i].name;
  return nullptr;
}

bool cardAdd(uint64_t key, const char* name) {
  if (cardLookup(key))       { Serial.println("  already enrolled"); return false; }
  if (numCards >= MAX_CARDS) { Serial.println("  card list full");   return false; }
  cards[numCards].key = key;
  strncpy(cards[numCards].name, name, sizeof(cards[numCards].name) - 1);
  cards[numCards].name[sizeof(cards[numCards].name) - 1] = '\0';
  numCards++; saveCards(); return true;
}

bool cardDel(uint64_t key) {
  for (int i = 0; i < numCards; i++) if (cards[i].key == key) {
    for (int j = i; j < numCards - 1; j++) cards[j] = cards[j + 1];
    numCards--; saveCards(); return true;
  }
  return false;
}

void listCards() {
  Serial.printf("[cards] %d:\n", numCards);
  for (int i = 0; i < numCards; i++) {
    Serial.printf("  %2d  %-14s key=", i + 1, cards[i].name);
    printKey(cards[i].key); Serial.println();
  }
}

// --- PIN store --------------------------------------------------------------
void savePins() { prefs.putBytes("pins", pins, numPins * sizeof(Pin)); }

void loadPins() {
  size_t len = prefs.getBytesLength("pins");
  if (len == 0 || len % sizeof(Pin) != 0 || len > sizeof(pins)) { numPins = 0; return; }
  numPins = len / sizeof(Pin);
  prefs.getBytes("pins", pins, len);
}

const char* pinLookup(const char* code) {
  for (int i = 0; i < numPins; i++) if (strcmp(pins[i].code, code) == 0) return pins[i].name;
  return nullptr;
}

bool pinAdd(const char* code, const char* name) {
  if (pinLookup(code))     { Serial.println("  PIN exists");   return false; }
  if (numPins >= MAX_PINS) { Serial.println("  PIN list full"); return false; }
  strncpy(pins[numPins].code, code, sizeof(pins[numPins].code) - 1);
  pins[numPins].code[sizeof(pins[numPins].code) - 1] = '\0';
  strncpy(pins[numPins].name, name, sizeof(pins[numPins].name) - 1);
  pins[numPins].name[sizeof(pins[numPins].name) - 1] = '\0';
  numPins++; savePins(); return true;
}

bool pinDel(const char* code) {
  for (int i = 0; i < numPins; i++) if (strcmp(pins[i].code, code) == 0) {
    for (int j = i; j < numPins - 1; j++) pins[j] = pins[j + 1];
    numPins--; savePins(); return true;
  }
  return false;
}

void listPins() {
  Serial.printf("[pins] %d:\n", numPins);
  for (int i = 0; i < numPins; i++)
    Serial.printf("  %2d  %-14s code=%s\n", i + 1, pins[i].name, pins[i].code);
}

// --- Lock actuation (Stage 4) ----------------------------------------------
volatile uint32_t lastUnlockCmd = 0;   // Stage 7: exit sense shares PUSH

void fireUnlock() {
  lastUnlockCmd = millis();
  digitalWrite(PIN_UNLOCK, HIGH);
  delay(300);
  digitalWrite(PIN_UNLOCK, LOW);
}

void onGranted(const char* name) {
  Serial.printf("GRANTED (%s) - unlocking\n", name);
  digitalWrite(PIN_STATUS_LED, HIGH);
  fireUnlock();
  digitalWrite(PIN_STATUS_LED, LOW);
}

// --- Enroll state -----------------------------------------------------------
bool     enrolling    = false;
uint32_t enrollArmed  = 0;
char     enrollName[16];

void armEnroll(const char* name) {
  strncpy(enrollName, name, sizeof(enrollName) - 1);
  enrollName[sizeof(enrollName) - 1] = '\0';
  enrolling = true; enrollArmed = millis();
  Serial.printf("[enroll] tap a card to add as \"%s\" (* or 'cancel' to abort)\n", enrollName);
}

// --- Keypad -----------------------------------------------------------------
char     keybuf[12];
int      keyLen = 0;
uint32_t lastKey = 0;

void processCode(const char* code) {
  if (strlen(code) == 0) return;
  if (strcmp(code, masterCode) == 0) {                 // master -> enroll
    char n[16]; snprintf(n, sizeof(n), "Card%d", numCards + 1);
    Serial.println("[keypad] master code OK");
    armEnroll(n);
  } else if (const char* name = pinLookup(code)) {     // valid door PIN
    onGranted(name);
  } else {
    Serial.println("DENIED  (bad PIN)");
  }
}

void handleKey(uint8_t key) {
  lastKey = millis();
  if (key <= 9) {
    if (keyLen < (int)sizeof(keybuf) - 1) { keybuf[keyLen++] = '0' + key; keybuf[keyLen] = '\0'; }
    Serial.printf("key: %u\n", key);          // DEBUG — silence for production
  } else if (key == 0xA) {                     // '*' = clear
    keyLen = 0; keybuf[0] = '\0'; Serial.println("key: * (clear)");
  } else if (key == 0xB) {                     // '#' = enter
    Serial.println("key: # (enter)");
    processCode(keybuf); keyLen = 0; keybuf[0] = '\0';
  } else {
    Serial.printf("key: unknown 0x%X\n", key); // report if * / # differ from A/B
  }
}

// --- Serial admin console ---------------------------------------------------
void printHelp() {
  Serial.println("cmds: list | enroll [name] | cancel | del <hexkey> | clear |");
  Serial.println("      pinlist | pinadd <code> [name] | pindel <code> | master <code> | help");
}

static String field(String& s, char sep) {   // pop first token
  int i = s.indexOf(sep);
  String t = (i < 0) ? s : s.substring(0, i);
  s = (i < 0) ? "" : s.substring(i + 1);
  s.trim(); t.trim(); return t;
}

void handleCommand(String line) {
  line.trim();
  if (line.length() == 0) return;

  if (line == "help")        printHelp();
  else if (line == "list")   listCards();
  else if (line == "pinlist") listPins();
  else if (line == "cancel") { enrolling = false; Serial.println("[enroll] cancelled"); }
  else if (line == "clear")  { numCards = 0; saveCards(); Serial.println("[cards] all removed"); }
  else if (line.startsWith("enroll")) {
    String name = line.substring(6); name.trim();
    if (name.length() == 0) name = "Card" + String(numCards + 1);
    char n[16]; name.toCharArray(n, sizeof(n)); armEnroll(n);
  }
  else if (line.startsWith("del ")) {
    uint64_t k = strtoull(line.substring(4).c_str(), nullptr, 16);
    Serial.print("[cards] "); if (cardDel(k)) { Serial.print("removed "); printKey(k); Serial.println(); }
    else Serial.println("key not found");
  }
  else if (line.startsWith("pinadd ")) {
    String rest = line.substring(7); rest.trim();
    String code = field(rest, ' ');
    String name = rest.length() ? rest : String("PIN");
    if (code.length()) { if (pinAdd(code.c_str(), name.c_str())) Serial.printf("[pins] added %s\n", code.c_str()); }
    else Serial.println("[pins] usage: pinadd <code> [name]");
  }
  else if (line.startsWith("pindel ")) {
    String code = line.substring(7); code.trim();
    Serial.print("[pins] "); Serial.println(pinDel(code.c_str()) ? "removed" : "not found");
  }
  else if (line.startsWith("master ")) {
    String code = line.substring(7); code.trim();
    if (code.length() && code.length() < sizeof(masterCode)) {
      code.toCharArray(masterCode, sizeof(masterCode));
      prefs.putString("master", masterCode);
      Serial.printf("[master] set to %s\n", masterCode);
    } else Serial.println("[master] bad length");
  }
  else Serial.println("unknown command - type help");
}

// --- Setup / loop -----------------------------------------------------------
void setup() {
  pinMode(PIN_UNLOCK, OUTPUT);     digitalWrite(PIN_UNLOCK, LOW);   // GPIO12 strap — LOW at boot
  pinMode(PIN_STATUS_LED, OUTPUT); digitalWrite(PIN_STATUS_LED, LOW);

  Serial.begin(115200);
  delay(200);
  prefs.begin("doorlock", false);
  loadCards();
  loadPins();
  prefs.getString("master", "1234").toCharArray(masterCode, sizeof(masterCode));

  Serial.printf("\n[smart-door-lock] keypad build - %d card(s), %d PIN(s).\n", numCards, numPins);
  if (strcmp(masterCode, "1234") == 0) Serial.println("[master] WARNING: default 1234 - change with 'master <code>'");
  printHelp();

  pinMode(PIN_WIEGAND_D0, INPUT);
  pinMode(PIN_WIEGAND_D1, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_WIEGAND_D0), onD0, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_WIEGAND_D1), onD1, FALLING);
}

void loop() {
  // serial commands
  static String cmd;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') { if (cmd.length()) { handleCommand(cmd); cmd = ""; } }
    else if (cmd.length() < 48) cmd += c;
  }

  // keypad entry timeout (partial code abandoned)
  if (keyLen > 0 && millis() - lastKey > 5000) { keyLen = 0; keybuf[0] = '\0'; Serial.println("[keypad] timeout"); }
  // enroll arm timeout
  if (enrolling && millis() - enrollArmed > 15000) { enrolling = false; Serial.println("[enroll] timed out"); }

  // Wiegand frame
  if (wgBits > 0 && (millis() - wgLast) > 25) {
    noInterrupts();
    uint64_t data = wgData; uint16_t bits = wgBits;
    wgData = 0; wgBits = 0;
    interrupts();

    if (bits == 26 || bits == 34) {              // card
      if (enrolling) {
        if (cardAdd(data, enrollName)) { Serial.print("[enroll] added "); printKey(data); Serial.printf(" as \"%s\"\n", enrollName); }
        enrolling = false;                        // enrolling never unlocks
      } else {
        const char* name = cardLookup(data);
        if (name) onGranted(name);
        else { Serial.print("DENIED  key="); printKey(data); Serial.println(); }
      }
    } else if (bits == 8 || bits == 4) {           // keypad key
      // THIS reader encodes the key in the HIGH nibble; low nibble = ~key.
      uint8_t hi = (uint8_t)((data >> 4) & 0xF), lo = data & 0xF;
      bool valid8 = (bits == 8) && (lo == ((~hi) & 0xF));
      uint8_t key = (bits == 8) ? hi : lo;         // 4-bit mode: the 4 bits are the key
      if (bits == 4 || valid8) handleKey(key);
      else Serial.printf("KEYPAD bad frame byte=0x%02X (ignored)\n", (unsigned)(data & 0xFF));
    }
  }
}
