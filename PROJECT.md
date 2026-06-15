# ESPoch — Project Notes

## Hardware

**Device:** Waveshare ESP32-S3-Touch-AMOLED-2.06  
**Purchase:** https://www.waveshare.com/esp32-s3-touch-amoled-2.06.htm?sku=31957  
**Wiki:** https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-2.06  
**Price:** $31.99  
**Status:** In hand — Stage 1 & Stage 2 firmware built, flashed, and running (2026-06-14)

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

### Installed Toolchain (as built)

- **ESP-IDF:** v6.0.1 (installed via the VS Code Espressif extension, Express setup)
- **IDF install path:** `C:\esp\v6.0.1\esp-idf`
- **Tools path (`IDF_TOOLS_PATH`):** `C:\Espressif` (extension default — note this is NOT
  the standard `~/.espressif`, which matters for command-line builds)
- **Watch serial port:** `COM3` (onboard USB-Serial-JTAG)
- **Target:** `esp32s3`, flashed over UART at `0x0` bootloader / `0x8000` partitions / `0x10000` app

---

## Build & Flash

### The easy way (VS Code)

1. Open the `ESPoch` folder in VS Code.
2. Bottom toolbar: target = **esp32s3**, port = **COM3**.
3. Click the 🔥 **Build, Flash and Monitor** flame icon. Choose **UART** if asked.

All required fixes live in the project files, so a normal VS Code build works — no manual steps.

### Command line (advanced / scripted)

The VS Code extension uses a non-standard tool layout, so the usual `export.ps1` does not
work directly. Drive a build from PowerShell like this:

```powershell
$env:IDF_PATH='C:\esp\v6.0.1\esp-idf'
$env:IDF_TOOLS_PATH='C:\Espressif'
$py='C:\Espressif\tools\python\v6.0.1\venv\Scripts\python.exe'
& $py "$env:IDF_PATH\tools\idf_tools.py" export --format key-value 2>&1 | ForEach-Object {
  if ($_ -match '^([^=]+)=(.*)$') { Set-Item "Env:$($matches[1])" ($matches[2] -replace '%PATH%',$env:PATH) }
}
$env:IDF_PYTHON_ENV_PATH='C:\Espressif\tools\python\v6.0.1\venv'
& $py "$env:IDF_PATH\tools\idf.py" -C 'C:\Users\micha\OneDrive\Code\ESPoch' build
& $py "$env:IDF_PATH\tools\idf.py" -C 'C:\Users\micha\OneDrive\Code\ESPoch' -p COM3 flash
```

### Project layout

| Path | Purpose |
| ---- | ------- |
| `main/main.c` | App entry + the LVGL watch-face UI |
| `main/idf_component.yml` | Managed dependencies (LVGL, `espressif/usb`) |
| `components/esp32_s3_touch_amoled_2_06/` | **Vendored, patched** Waveshare BSP (see notes below) |
| `sdkconfig.defaults` | Chip / PSRAM / display / LVGL configuration |
| `dependencies.lock` | Pins exact dependency versions (committed for reproducibility) |
| `managed_components/` | Auto-downloaded deps — **git-ignored**, rebuilt from the lock file |
| `build/` | Build output — git-ignored |

---

## ESP-IDF v6 Compatibility Notes

The Waveshare board support package (BSP) `waveshare/esp32_s3_touch_amoled_2_06` v1.0.7 was
written for ESP-IDF v5 and does **not** build cleanly on the v6.0.1 we use. Rather than pin an
old IDF, the BSP is **vendored** as a local component at
`components/esp32_s3_touch_amoled_2_06/` and patched. The patches are recorded in that folder's
[`PATCHES.md`](components/esp32_s3_touch_amoled_2_06/PATCHES.md). Summary:

1. **`espressif/usb` dependency added** (in `main/idf_component.yml`) — v6 moved the USB host
   component out of the core into the component registry.
2. **LVGL pinned to `>=9.3`** — `esp_lvgl_port` 2.8.0 references a color constant added in 9.3.
3. **BSP `REQUIRES` extended** with `esp_driver_ledc esp_driver_sdmmc esp_driver_i2s` — v6 split
   the monolithic `driver` component into separate pieces.
4. **`bsp_display_lcd_init()` call fixed** — it was called with an argument it doesn't declare
   (a hard error under the GCC 15 in IDF v6).
5. **QSPI pixel clock lowered 40 MHz → 20 MHz** — at 40 MHz the v6 SPI driver aborts the screen
   flush with "DMA TX underflow". 20 MHz is rock-solid and plenty for a watch face.
6. **`sdkconfig.defaults` performance block** — PSRAM XIP + 64-byte cache line + perf
   optimization give the display DMA enough bandwidth (mirrors Waveshare's demo config).
   `CONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM` is intentionally **off** (GCC 15 attribute error).

> If the BSP is ever re-downloaded fresh from the registry, these patches must be re-applied.
> That's exactly why it's vendored locally instead of left as a managed dependency.

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

Note: Skip 30 seconds is a best-effort feature — Apple's AMS (Apple Media Service) does not include a dedicated skip-30 command. Will attempt via Downcast's response to system media controls.

---

## Feature Roadmap

### Stage 1 — Hello Watch ✅ COMPLETE (2026-06-14)

- Flash initial firmware
- Display ESPoch name on screen (serial console — no display driver yet)
- Confirm all hardware working

### Stage 2 — Watch Face ✅ COMPLETE (2026-06-14)

- Digital time display (large, amber) — live ticking `HH:MM:SS`
- Date display (day + full date)
- Battery percentage with color indicator — **layout + color logic done; value is a
  placeholder until the AXP2101 is wired in (planned with Stage 3)**
- Step count display — **placeholder until Stage 4 (QMI8658 IMU)**
- Amber on black, dashboard layout
- Optimized for outdoor readability

> Time is currently seeded to a fixed value at boot, so it ticks but is not the real
> wall-clock time yet — that arrives with NTP in Stage 3. See **Build & Flash** and
> **ESP-IDF v6 Compatibility Notes** below for how the firmware is built.

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
- AMS (Apple Media Service) — media controls (play/pause, next, previous)
- Swipe right gesture for skip 30 seconds (best effort)

Note: ESP32-S3 has Bluetooth Low Energy only — no Bluetooth Classic radio — so AVRCP (a Classic profile) is not available. Apple exposes media control to BLE accessories through AMS instead, and notifications through ANCS. NimBLE (already enabled) is the correct stack for both.

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

Note: iPhone uses BLE + ANCS for notifications and BLE + AMS for media control. AirPods pair directly to iPhone — watch is not in that audio chain.

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
- Media controls go through iPhone system layer via AMS (Apple Media Service) over BLE

---

## Home Server

- **Hardware:** QNAP TS-451+ NAS
- **Docker:** Running, available for containers
- **LLM:** Claude API
- **Role in project:** Stage 7 voice note receiver, transcription, automation
- **Addressing:** mDNS `espoch-server.local` preferred over static IP

---

## Notes for Next Session

1. Watch is in hand; toolchain installed; **Stages 1 & 2 are built, flashed, and running**.
2. The watch face works (live ticking clock on the AMOLED). Time value is seeded, not real yet.
3. **Next: Stage 3** — WiFi + NTP (real time + timezone) + OTA + web config. Also wire the real
   battery reading via the AXP2101 (see Waveshare `01_AXP2101` example, uses XPowersLib).
4. Stage 3 should switch from the single-app partition to a **two-slot OTA partition table**.
5. Owner is not a coder — all code is written and explained in plain English.
6. Build can now be driven from VS Code (🔥 flame icon) or the command line (see **Build & Flash**).
7. The Waveshare BSP is **vendored and patched** for ESP-IDF v6 — see **ESP-IDF v6 Compatibility
   Notes** before touching display code.
