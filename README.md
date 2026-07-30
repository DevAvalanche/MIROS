# MIROS — Multi-tool ESP32 Pocket Device

A single-button, pocket-sized ESP32-C3 gadget with a 72×40 OLED display. MIROS packs a WiFi scanner, a set of everyday utilities, and five playable mini-games into one menu-driven firmware — all controlled from a single button.

Built with PlatformIO and the Arduino framework.

---

## Features

**WiFi Tools**
- Live WiFi network scanning, sorted by signal strength (RSSI)
- Channel and encryption type detection (OPEN, WEP, WPA, WPA2, WPA3)
- Open networks flagged `[!]`, secured networks flagged `[*]`
- Detailed inspection view showing full BSSID (MAC address)

**Utilities**
- Stopwatch
- Countdown timer with presets
- Pomodoro timer (work/break cycles with session count)
- Tally counter
- Dice roller
- Magic 8-Ball
- Coin flipper
- Guided breathing exercise
- Flashlight (solid / strobe / SOS modes via onboard LED)

**Mini-Games**
- Snake
- Flappy Bird
- Pong
- Space Defender
- Dino Runner

**Other**
- Screensaver with power-saving auto-dim after inactivity
- Scrolling instructions/about screens
- Fully single-button navigation (tap / hold / double-tap gestures)

---

## Hardware Requirements

- ESP32-C3 development board (e.g. ESP32-C3-DevKitM-1)
- SSD1306 72×40 I2C OLED display
- Single push button
- Addressable LED (NeoPixel-compatible), for the flashlight modes
- Jumper wires

## Pin Configuration

| Signal | GPIO |
|--------|------|
| SDA    | 5    |
| SCL    | 6    |
| Button | 9    |
| LED    | 8    |

---

## Controls

MIROS is designed to be fully operable from one button:

| Action | Result |
|--------|--------|
| Short tap | Next menu item / jump (in games) |
| Double tap | Context-dependent action (varies per mode) |
| Hold ~0.7–1.5s | Confirm / freeze-unfreeze / mode action |
| Hold ~3–5s | Enter or exit current mode / return to menu |
| Hold 5s in game | Exit game to menu |

Exact timing thresholds are defined at the top of `src/main.cpp` (`HOLD_TIME`, `TIMER_START_HOLD`, `DINO_JUMP_MAX_PRESS`, etc.) if you want to tune the feel.

---

## Software Stack

- [PlatformIO](https://platformio.org/)
- Arduino framework for ESP32
- [U8g2](https://github.com/olikraus/u8g2) — OLED graphics
- Adafruit NeoPixel — LED control
- QRCode — QR generation
- ESP32 Preferences (NVS) — persisted settings
- ESP32 core WiFi library — scanning

---

## Setup

1. Install [VS Code](https://code.visualstudio.com/) and the [PlatformIO IDE extension](https://platformio.org/install/ide?install=vscode).
2. Clone this repo:
   ```bash
   git clone https://github.com/yourusername/MIROS.git
   cd MIROS
   ```
3. Open the folder in VS Code. PlatformIO will detect `platformio.ini` and automatically install the correct toolchain and libraries on first load — no manual library installation needed.
4. Connect your ESP32-C3 board over USB.
5. Build and upload:
   ```bash
   pio run --target upload
   ```
   or use the PlatformIO toolbar buttons (checkmark to build, arrow to upload).
6. Wire up the OLED, button, and LED according to the pin table above.

No WiFi credentials or API keys are required — the WiFi scanner mode only listens for nearby networks, it doesn't connect to any.

---

## License

Add a license of your choice (MIT is a common default for hobbyist hardware projects) — none is currently specified.
