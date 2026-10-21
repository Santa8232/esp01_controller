# ESP-01 Tactical Controller & Morse Code Pad

A versatile single-button Wi-Fi controller powered by an **ESP-01 (ESP8266)** communicating with a lightweight background **Windows .NET 8 receiver** and featuring an embedded **Cybernetic Morse Code Pad** web dashboard.

---

## ⚡ Key Features

- **Clipboard / Mouse-Selection Controller**:
  - **Short Click (<0.5s)**: Injects `Ctrl + C` into the active window to immediately copy any highlighted text into your clipboard.
  - **Long Hold (≥0.5s)**: Triggers Windows `Win + V` clipboard history.
- **Optimized High-Speed Morse Code Keyer**:
  - **Dot / Dash Timing**: Fast, responsive thresholds (`< 220ms` = Dot, `≥ 220ms` = Dash).
  - **Letter Pause**: `500ms` inactivity pause to decode letter and type into the focused app.
  - **Mistake Cancel**: Hold button for `1.5s` to clear the current unfinished sequence.
  - **No Auto-Spacing**: Pauses between letters will never inject unwanted spaces. To type an intentional space, tap `..--` or `.-.-`.
- **Embedded Tactical Web UI (`http://<ESP-IP>/`)**:
  - Dichotomic Morse Code tree topology matching tactile cyber-deck hardware.
  - Real-time white track illumination, neon green dots, neon orange dashes, and concentric reticle around the active letter.
  - Live decoded text stream with 700Hz telegraph audio sidetone.
  - Interactive "PRESS & HOLD THUMB KEY PAD" button.
- **Over-The-Air (OTA) Firmware Flashing**: Update firmware wirelessly over local Wi-Fi without needing USB serial adapters.

---

## 🛠️ Hardware Setup

- **MCU**: ESP-01 / ESP8266 module
- **Power**: 3.3V regulated power supply (GPIO0 must be high during boot)
- **Button**: Momentary tactile push button between **GPIO0** and **GND**

---

## 💻 Windows Receiver Setup

1. Make sure [.NET 8 SDK](https://dotnet.microsoft.com/) is installed.
2. In `windows/Program.cs`, verify `EspUrl` matches your ESP-01 IP (e.g. `http://192.168.29.123`).
3. Run the receiver:
   ```powershell
   cd windows
   dotnet run
   ```
4. Keep the receiver running in the background.

---

## 📡 Flashing Firmware

1. Open `firmware/ESP01Controller/ESP01Controller.ino` in Arduino IDE or compile via `arduino-cli`.
2. Configure Wi-Fi SSID and password.
3. Flash initial build via USB-UART adapter (ensure GPIO0 is pulled to GND during boot for flashing mode).
4. Subsequent updates can be flashed over Wi-Fi via Arduino OTA or `espota.py`.
