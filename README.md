# ESP-01 Tactical Controller & Morse Code Pad

A versatile single-button Wi-Fi controller powered by an **ESP-01 (ESP8266)** communicating with a background **Windows .NET 8 receiver** and featuring an embedded **Cybernetic Morse Code Pad** web dashboard.

---

## ⚡ Key Features

### 1. Dual Operational Modes
* **📋 Clipboard Controller Mode (`mode=copy`)**:
  * **Short Click (<0.5s)**: Injects `Ctrl + C` into the active window to copy highlighted text to the clipboard and syncs it with the ESP-01 web interface.
  * **Long Hold (≥0.5s)**: Triggers Windows `Win + V` clipboard history overlay.
* **⚡ Tactical Morse Code Keyer (`mode=morse`)**:
  * **Dot / Dash Timing**: Fast, responsive thresholds (`< 200ms` = Dot `·`, `≥ 200ms` = Dash `—`).
  * **Comfortable Letter Pause (750ms)**: Generous inactivity window allowing comfortable keying of multi-element Morse sequences without premature letter decoding.
  * **Direct Space Gesture (≥750ms hold)**: Press and hold the button from idle for ~0.75s to immediately emit a space keystroke without needing multi-element sequences.
  * **Multi-Element Space Codes**: Supports `..--` (matches Pad tree node next to `F`), `.-.-`, and `....-`.
  * **Universal Windows Spacebar (`VK_SPACE`)**: Dispatches native Windows virtual key code `0x20`, guaranteeing real spacebar strokes in VS Code, browsers, Notepad, and terminals.
  * **Mistake Cancel (≥1.8s hold)**: Clears any mistaken partial sequence without emitting unwanted characters.
  * **No Auto-Spacing**: Pauses between letters will never inject unwanted spaces.

---

### 2. Embedded Cybernetic Web Dashboard (`http://<ESP-IP>/`)
* **Vector Dichotomic Morse Tree**: Full interactive SVG matrix matching tactical cyber-deck hardware.
* **Visible `[—] SPACE` Node**: Dedicated space node located in Row 3 under `F` with active track lighting (`e_F_SP`).
* **Active Reticle & Sidetone**: Real-time neon edge illumination, targeting reticle on active letters, and 700 Hz telegraph audio feedback.
* **Virtual Thumb Key Pad & Quick Action**: On-screen "PRESS & HOLD THUMB KEY PAD" and dedicated `␣ SPACE` button.
* **Speed & Threshold Presets**: Switch between **🐢 Relaxed** (240ms/850ms), **⚡ Normal** (200ms/750ms), and **🚀 Fast** (160ms/500ms) with direct EEPROM persistence.
* **Live Text Stream Terminal**: Real-time decoded stream with one-click clipboard copy and clear.

---

### 3. Wireless Over-The-Air (OTA) Updates
* Flash updated firmware wirelessly over local Wi-Fi using Arduino OTA or `espota.py` without requiring physical USB-UART reconnection.

---

## 🛠️ Hardware Setup & Wiring

| ESP-01 Pin | Connection | Note |
| :--- | :--- | :--- |
| **3V3** | +3.3V DC | Regulated 3.3V power supply (min 250mA peak) |
| **GND** | Ground | Common ground |
| **EN / CH_PD** | Pull-up to 3.3V | Enable chip (via 10kΩ resistor or direct) |
| **GPIO0** | Momentary Push Button to **GND** | Internal pull-up enabled. *(Must be HIGH during boot)* |
| **GPIO2** | Pull-up to 3.3V | Keeps bootloader stable (recommended 10kΩ) |
| **RX / TX** | Optional UART | 115200 baud for debugging serial monitor |

> [!WARNING]
> Because the control button is connected to **GPIO0**, ensure the button is **NOT** held down during power-on or reset; holding GPIO0 LOW during boot puts the ESP8266 into UART flash mode.

---

## ⌨️ Morse Code Quick Reference

### Timing Matrix
| Gesture | Duration | Action |
| :--- | :--- | :--- |
| **Short Tap** | `< 200 ms` | Dit / Dot (`·`) |
| **Hold** | `200 ms – 749 ms` | Dah / Dash (`—`) |
| **Direct Hold (Idle)** | `≥ 750 ms` | Direct `SPACE` Keystroke |
| **Letter Pause** | `750 ms` pause | Decodes current sequence into active window |
| **Cancel Hold** | `≥ 1.8 s` | Clears mistake sequence (`[CLEARED]`) |

### Space Input Options
1. **Direct Button Hold**: Hold button down for ~0.75s from idle.
2. **Morse Code Sequence**: Tap `..--` (Dot Dot Dash Dash) or `.-.-`.
3. **Web Dashboard**: Click the `␣ SPACE` button or `[—] SPACE` node on the tree.

---

## 💻 Windows Receiver Setup (.NET 8)

The Windows receiver runs unobtrusively in the background, polling hardware events from the ESP-01 HTTP server and dispatching native keyboard/clipboard inputs.

### 1. Requirements
* [.NET 8.0 SDK](https://dotnet.microsoft.com/download/dotnet/8.0)

### 2. Run
```powershell
cd windows
dotnet run
```

### 3. Build Release Executable
```powershell
cd windows
dotnet publish -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true
```

---

## ⚡ Firmware Flashing (Arduino CLI)

### 1. Compile Firmware
```powershell
arduino-cli compile --fqbn esp8266:esp8266:generic --build-path ./build firmware/ESP01Controller
```

### 2. Over-The-Air (OTA) Flash
Upload wirelessly over Wi-Fi:
```powershell
python "C:\Users\Santa\AppData\Local\Arduino15\packages\esp8266\hardware\esp8266\3.1.2\tools\espota.py" -i 192.168.29.123 -p 8266 -a 123456 -f ./build/ESP01Controller.ino.bin
```
*(Replace `192.168.29.123` with your device's current local IP address).*

---

## 📖 Command Reference

For a complete cheat sheet covering all Arduino CLI commands, OTA uploads, .NET publish profiles, and HTTP REST endpoints, see:
👉 **[command.md](command.md)**

---

## 🐙 Git Repository

* **Repository:** [https://github.com/Santa8232/esp01_controller.git](https://github.com/Santa8232/esp01_controller.git)
* **Branch:** `main`
