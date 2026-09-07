# Smart Door Lock (ZKTeco / ESP32)

Component bring-up firmware for the RFID card-in / button-out access-control
build. Each new component is proven in its own PlatformIO environment before the
components are combined into the main app.

Board: **SSN32 (ESP32-WROOM-32E)** → `board = esp32dev`.

## Pin map (single source of truth: `include/pins.h`)

| GPIO | Signal | Wiring |
|------|--------|--------|
| 4  | Wiegand **D0** | KR602E green, via 10k/15k divider (5 V → 3.0 V) |
| 16 | Wiegand **D1** | KR602E white, via 10k/15k divider |
| 12 | **Unlock trigger** | → transistor → AP108 `PUSH` (active-HIGH pulse). **Strapping pin — keep LOW at boot + external 10k pull-down.** |
| 21 | **Exit / PUSH sense** | AP108 `PUSH` via divider; idle HIGH, press LOW |

Reader power is 12 V from the AP108, **not** the ESP32. All grounds common.

## Environments

| Env | What it tests | Command |
|-----|---------------|---------|
| `wiegand-test` | Card reader — prints bit count + facility/card + parity per tap | `pio run -e wiegand-test -t upload -t monitor` |
| `lock-test` | Drop-bolt unlock — send `u` to fire one PUSH pulse (**mentor present**) | `pio run -e lock-test -t upload -t monitor` |
| `pulse-test` | Exit-button / PUSH sense — counts falling edges | `pio run -e pulse-test -t upload -t monitor` |
| `esp32dev` | Main app (stub) | `pio run -e esp32dev -t upload -t monitor` |

Each test env compiles only its own `test/<name>/main.cpp` via
`build_src_filter = -<*> +<../test/<name>/>`, so `src/main.cpp` is never
double-compiled.

## Safety

- **GPIO 12 is a boot strapping pin.** It must be LOW at reset; every sketch here
  drives it LOW in `setup()`, but fit an external ~10k pull-down as well so it
  cannot float HIGH during boot.
- **Lock (Stage 4) and exit button (Stage 5) are the highest-risk steps** — real
  lock hardware and egress safety. Mentor present, not just for power-up.
- Fail-safe drop bolt: power removed = unlocked. The AP108 relay `NC` contact
  carries lock power; the ESP32 only pulses `PUSH`, never the lock itself.
