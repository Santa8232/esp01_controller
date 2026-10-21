# ESP-01 Controller & Morse Code Pad — Command Reference

A quick reference guide for running, building, testing, flashing, and managing the ESP-01 controller and Windows receiver.

---

## 🖥️ Windows Receiver (.NET 8)

Run these commands inside the `windows/` directory.

### Run Receiver
```powershell
cd windows
dotnet run
```

### Build Receiver
```powershell
cd windows
dotnet build
```

### Build Receiver for Production (Release)
```powershell
cd windows
dotnet publish -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true
```

---

## ⚡ Firmware & Flashing (ESP8266 / Arduino CLI)

Run these commands from the project root directory.

### Compile Firmware
```powershell
arduino-cli compile --fqbn esp8266:esp8266:generic --build-path ./build firmware/ESP01Controller
```

### Wireless Over-The-Air (OTA) Flash
Upload compiled binary directly over Wi-Fi (no USB cables needed):
```powershell
python "C:\Users\Santa\AppData\Local\Arduino15\packages\esp8266\hardware\esp8266\3.1.2\tools\espota.py" -i 192.168.29.123 -p 8266 -a 123456 -f ./build/ESP01Controller.ino.bin
```
*(Replace `192.168.29.123` with your current ESP-01 IP address if it changes).*

### Initial USB-Serial Flashing (First Time)
When flashing over a USB-to-UART adapter (ensure GPIO0 is pulled LOW to GND during power-on):
```powershell
arduino-cli upload -p COM3 --fqbn esp8266:esp8266:generic firmware/ESP01Controller
```
*(Replace `COM3` with your serial port).*

### Serial Monitor
Inspect boot logs and Wi-Fi IP assignment:
```powershell
arduino-cli monitor -p COM3 -c baudrate=115200
```

---

## 🌐 HTTP REST API & Testing (cURL)

The ESP-01 serves a lightweight HTTP server on port 80. You can test or control it with PowerShell `curl` / `Invoke-RestMethod`:

### Check Controller Status & Mode
```powershell
curl http://192.168.29.123/status
```

### Dequeue Next Hardware Event
```powershell
curl http://192.168.29.123/event
```

### Read Wi-Fi RSSI & Configuration
```powershell
curl http://192.168.29.123/config
```

### Send Manual Space Keystroke
```powershell
curl http://192.168.29.123/space
```

### Switch to Morse Code Mode
```powershell
curl "http://192.168.29.123/save?mode=morse&ajax=1"
```

### Switch to Clipboard Controller Mode
```powershell
curl "http://192.168.29.123/save?mode=copy&ajax=1"
```

### Update Morse Speed & Pause Thresholds
```powershell
curl "http://192.168.29.123/save?dash=220&gap=500&ajax=1"
```

### Read Stored Clipboard Text
```powershell
curl http://192.168.29.123/text
```

---

## ⌨️ Hardware Button Gestures

### Clipboard Controller Mode (`mode=copy`)
| Gesture | Action | Description |
| :--- | :--- | :--- |
| **Short Click (<0.5s)** | `COPY` | Dispatches `Ctrl + C` to copy whatever text you highlighted with mouse. |
| **Long Hold (≥0.5s)** | `WIN_V` | Opens Windows Clipboard History overlay. |

### Morse Code Mode (`mode=morse`)
| Gesture | Action | Description |
| :--- | :--- | :--- |
| **Tap (<220ms)** | DOT (`.`) | Added immediately on button release. |
| **Hold (≥220ms)** | DASH (`-`) | Triggered while holding down. |
| **Inactivity Pause (500ms)** | DECODE | Decodes buffered sequence and types the letter. |
| **Hold (≥1.5s)** | CANCEL | Clears mistaken sequence without typing. |
| **Space Sequence** | `..--` or `.-.-` | Decodes as `SPACE` character. |

---

## 🐙 Git Workflow

### Check Status
```powershell
git status
```

### Stage and Commit
```powershell
git add .
git commit -m "Your commit message"
```

### Push to GitHub
```powershell
git push origin main
```

### Pull Latest Changes
```powershell
git pull origin main
```
