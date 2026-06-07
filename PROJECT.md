# ESPoch — Project Notes

## Hardware

**Device:** Waveshare ESP32-S3-Touch-AMOLED-2.06  
**Purchase:** https://www.waveshare.com/esp32-s3-touch-amoled-2.06.htm?sku=31957  
**Wiki:** https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-2.06  
**Price:** $31.99  
**Status:** Ordered, not yet arrived

### Key Specs

- ESP32-S3R8 dual-core LX7 processor, up to 240MHz
- 2.06 inch AMOLED touchscreen, 410×502 pixels
- 8MB PSRAM, 32MB Flash
- WiFi 802.11 b/g/n + Bluetooth 5 BLE
- QMI8658 6-axis IMU (accelerometer + gyroscope)
- PCF85063 RTC chip
- AXP2101 power management
- ES8311 audio codec + dual digital MEMS microphones
- TF card slot
- 3.7V MX1.25 lithium battery connector
- Two physical buttons: PWR and BOOT
- Type-C USB

---

## Project Name

**ESPoch**

- ESP = the ESP32-S3 chip
- Epoch = Unix time reference point (t=0), the basis of how computers measure time
- Every ESP32 deals with epoch time when doing NTP sync

---

## Development Environment

- **Framework:** ESP-IDF (not Arduino)
- **Editor:** VS Code + Espressif IDF Extension
- **Language:** C
- **Platform:** Windows 11
- **Project folder:** `C:\Users\micha\OneDrive\Code\ESPoch`

### Software to Install (when watch arrives)

1. VS Code — https://code.visualstudio.com/download
2. Git — https://git-scm.com/download/win (64-bit, all defaults)
3. USB Driver — https://www.wch-ic.com/downloads/CH343SER_EXE.html
4. Espressif IDF Extension — installs inside VS Code last

### Flashing Method

- USB-C cable to PC
- Onboard auto-download circuit — no manual bootloader mode needed
- One-click flash from VS Code
- OTA updates via WiFi once Stage 3 is complete

---

## Watch Face Design

**Style:** Dashboard — all info on one screen  
**Optimized for:** Outdoor readability, high contrast

### Layout

```
┌─────────────────────┐
│                     │
│       10:42         │  ← Large amber, bold
│                     │
│    Monday           │  ← Medium amber
│    June 6, 2026     │  ← Medium amber
│                     │
│   STEPS  2,847      │  ← Small amber label, medium value
│                     │
│   BATT   73%        │  ← Small amber label, color value
│                     │
└─────────────────────┘
```

### Color Palette

| Element        | Color        | Hex     |
| -------------- | ------------ | ------- |
| Background     | Pure black   | #000000 |
| Time           | Bright amber | #FFB300 |
| Date / Labels  | Dimmer amber | #CC8800 |
| Battery > 50%  | Green        | #00FF00 |
| Battery 20-50% | Yellow       | #FFFF00 |
| Battery < 20%  | Red          | #FF0000 |

Note: AMOLED black = pixels off = best battery life and outdoor contrast

---

## Button Mapping

| Button | Short Press  | Long Press           | Double Press |
| ------ | ------------ | -------------------- | ------------ |
| PWR    | Wake / Sleep | Power options        | —            |
| BOOT   | Play / Pause | Start/Stop recording | Next track   |

---

## Gesture Controls

| Gesture     | Action                     |
| ----------- | -------------------------- |
| Swipe right | Skip 30 seconds (Downcast) |
| Raise wrist | Wake screen (Stage 8)      |

Note: Skip 30 seconds is a best-effort feature — standard AVRCP does not include this command. Will attempt via Downcast's response to system media controls.

---

## Feature Roadmap

### Stage 1 — Hello Watch

- Flash initial firmware
- Display ESPoch name on screen
- Confirm all hardware working

### Stage 2 — Watch Face

- Digital time display (large, amber)
- Date display
- Battery percentage with color indicator
- Step count display
- Amber on black, dashboard layout
- Optimized for outdoor readability

### Stage 3 — NTP + OTA + Web Config

- WiFi connection with saved credentials
- NTP time sync on boot and periodically
- Keeps RTC accurate
- OTA firmware updates over WiFi
- Web configuration portal (WiFi credentials, timezone, settings)
- Build OTA early — makes all future stages easier to deploy

### Stage 4 — Step Counter

- QMI8658 IMU step counting
- Daily step count on watch face
- Resets at midnight

### Stage 5 — Bluetooth (iPhone)

- Pair with iPhone (one-time setup)
- ANCS — receive and display notifications
- Show app name, title, body text
- Dismiss notifications from watch
- AVRCP — media controls (play/pause, next, previous)
- Swipe right gesture for skip 30 seconds (best effort)

#### Bluetooth Confidence Levels

| Feature               | Confidence        |
| --------------------- | ----------------- |
| Display notifications | High              |
| Dismiss notifications | High              |
| Play / Pause          | Medium-High       |
| Next track / episode  | Medium            |
| Previous track        | Medium            |
| Skip 30 seconds       | Low — best effort |
| Now playing info      | Medium            |

Note: iPhone uses BLE + ANCS for notifications. Media control via AVRCP may be inconsistent with iPhone. AirPods pair directly to iPhone — watch is not in that audio chain.

### Stage 6 — Google Calendar Sync

- Google Calendar API (free tier)
- Read-only calendar access
- Display next 2-3 appointments on swipe screen
- Alert X minutes before event
- Syncs when on WiFi
- Requires one-time Google API setup

### Stage 7 — Voice Notes

- Long press BOOT to record
- Visual recording indicator on screen
- Audio saved as .WAV to TF card
- Auto-upload queue when WiFi available
- Upload to home server via HTTP POST
- File naming: `espoch_YYYYMMDD_HHMM_001.wav`
- Upload progress and success/fail confirmation on screen

#### Server Side (QNAP TS-451+ NAS)

- Docker container receives uploads
- Local Whisper transcription (free, private)
- Transcript saved alongside audio: `espoch_YYYYMMDD_HHMM_001.txt`
- Claude API available for further processing
- Server addressed via mDNS: `espoch-server.local`
- Custom automation hooks for transcripts

### Stage 8 — Polish

- Raise wrist to wake screen (IMU gesture)
- Notification history — scroll back through missed ones
- WiFi credentials setup without reflashing
- Battery low warning on screen

---

## Phone + Audio Setup

- **Phone:** iPhone (current iOS)
- **Music:** Spotify
- **Podcasts:** Downcast
- **Audio output:** Apple AirPods
- AirPods pair directly to iPhone, watch is not in audio chain
- Media controls go through iPhone system layer via AVRCP

---

## Home Server

- **Hardware:** QNAP TS-451+ NAS
- **Docker:** Running, available for containers
- **LLM:** Claude API
- **Role in project:** Stage 7 voice note receiver, transcription, automation
- **Addressing:** mDNS `espoch-server.local` preferred over static IP

---

## Notes for Next Session

1. Watch has not yet arrived
2. No software installed yet
3. First session: install tools → flash Stage 1 → build Stage 2
4. Owner is not a coder — all code will be written and explained in plain English
5. Start installation instructions at VS Code download
