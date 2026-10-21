#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>
#include <EEPROM.h>

#define BUTTON_PIN 0

// Wi-Fi credentials
const char* ssid = "Santa";
const char* password = "30August1991";

// Change this OTA password before using the device on a shared network.
const char* otaHostname = "esp01-controller";
const char* otaPassword = "123456";

ESP8266WebServer server(80);

String mode = "copy";
String clipboardText = "Hello from ESP-01";
char morse[8] = {0};
uint8_t morseLen = 0;
String lastSequence = "";
String lastCharacter = "";

const unsigned long DEBOUNCE_MS = 25;
const unsigned long COPY_LONG_PRESS_MS = 500;
unsigned long morseDashMs = 220;     // Click threshold: < 220ms = dot, >= 220ms = dash
unsigned long morseGapMs = 500;      // Inactivity pause to decode letter: 500ms (was 900ms)
unsigned long wordGapMs = 1200;      // Inactivity pause to emit SPACE: 1200ms (was 2200ms)
const unsigned long MORSE_SPACE_HOLD_MS = 800;  // Hold 800ms = emit SPACE gesture!
const unsigned long MORSE_CANCEL_MS = 1800;     // Hold 1.8s = cancel/clear mistake sequence

bool rawLast = HIGH;
bool stableButton = HIGH;
bool longPressSent = false;
bool spaceSent = false;
bool clearSent = false;
unsigned long debounceAt = 0;
unsigned long pressAt = 0;
unsigned long lastMorseInput = 0;

// Small FIFO event queue with fixed-length slots to prevent heap fragmentation.
const uint8_t QUEUE_SIZE = 12;
const uint8_t MAX_EVENT_LEN = 16;
char events[QUEUE_SIZE][MAX_EVENT_LEN];
uint8_t queueHead = 0;
uint8_t queueTail = 0;

void enqueueEvent(const char* value) {
  uint8_t next = (queueTail + 1) % QUEUE_SIZE;
  if (next == queueHead) {
    Serial.println(F("Event queue full; event dropped"));
    return;
  }
  strncpy(events[queueTail], value, MAX_EVENT_LEN - 1);
  events[queueTail][MAX_EVENT_LEN - 1] = '\0';
  queueTail = next;
}

void enqueueEvent(const String& value) {
  enqueueEvent(value.c_str());
}

String dequeueEvent() {
  if (queueHead == queueTail) return "";
  String value = events[queueHead];
  queueHead = (queueHead + 1) % QUEUE_SIZE;
  return value;
}

void saveSettings() {
  EEPROM.begin(128);
  EEPROM.write(0, mode == "morse" ? 1 : 0);
  for (int i = 0; i < 100; i++) {
    EEPROM.write(1 + i, i < (int)clipboardText.length()
      ? clipboardText[i] : 0);
  }
  EEPROM.write(101, (uint8_t)(morseDashMs / 10));
  EEPROM.write(102, (uint8_t)(morseGapMs / 10));
  EEPROM.write(103, (uint8_t)(wordGapMs / 20));
  EEPROM.commit();
  EEPROM.end();
}

void loadSettings() {
  EEPROM.begin(128);
  mode = (EEPROM.read(0) == 1) ? "morse" : "copy";

  char buffer[101] = {0};
  for (int i = 0; i < 100; i++) buffer[i] = (char)EEPROM.read(1 + i);
  buffer[100] = '\0';

  bool valid = true;
  for (int i = 0; i < 100 && buffer[i] != '\0'; i++) {
    unsigned char c = (unsigned char)buffer[i];
    if (c < 32 && c != '\t') {
      valid = false;
      break;
    }
  }
  if (valid && strlen(buffer) > 0) clipboardText = String(buffer);

  uint8_t d = EEPROM.read(101);
  if (d >= 10 && d <= 80) morseDashMs = (unsigned long)d * 10;
  uint8_t g = EEPROM.read(102);
  if (g >= 20 && g <= 150) morseGapMs = (unsigned long)g * 10;
  uint8_t w = EEPROM.read(103);
  if (w >= 30 && w <= 150) wordGapMs = (unsigned long)w * 20;

  EEPROM.end();
}

char decodeMorse(const char* code) {
  static const char* const letters[] = {
    ".-", "-...", "-.-.", "-..", ".", "..-.", "--.",
    "....", "..", ".---", "-.-", ".-..", "--", "-.",
    "---", ".--.", "--.-", ".-.", "...", "-", "..-",
    "...-", ".--", "-..-", "-.--", "--.."
  };
  for (int i = 0; i < 26; i++) {
    if (strcmp(code, letters[i]) == 0) return (char)('A' + i);
  }

  static const char* const digits[] = {
    "-----", ".----", "..---", "...--", "....-",
    ".....", "-....", "--...", "---..", "----."
  };
  for (int i = 0; i < 10; i++) {
    if (strcmp(code, digits[i]) == 0) return (char)('0' + i);
  }

  if (strcmp(code, ".-.-.-") == 0) return '.';
  if (strcmp(code, "--..--") == 0) return ',';
  if (strcmp(code, "..--..") == 0) return '?';
  if (strcmp(code, "-.-.--") == 0) return '!';
  if (strcmp(code, "-....-") == 0) return '-';
  if (strcmp(code, ".-..-.") == 0) return '"';
  if (strcmp(code, ".--.-.") == 0) return '@';
  if (strcmp(code, "-..-.") == 0) return '/';
  if (strcmp(code, "..--") == 0 || strcmp(code, ".-.-") == 0) return ' '; // explicit Morse SPACE codes
  return '\0';
}

void handleButton() {
  bool raw = digitalRead(BUTTON_PIN);

  if (raw != rawLast) {
    rawLast = raw;
    debounceAt = millis();
  }
  if (millis() - debounceAt < DEBOUNCE_MS) return;

  if (raw != stableButton) {
    stableButton = raw;

    if (stableButton == LOW) {
      pressAt = millis();
      longPressSent = false;
      spaceSent = false;
      clearSent = false;
    } else {
      // Key Released (Key Up)
      unsigned long duration = millis() - pressAt;

      if (mode == "copy") {
        if (!longPressSent) {
          enqueueEvent("COPY");
        }
      } else {
        // Morse mode: if not already triggered as dash, space, or cleared, record DOT
        if (!longPressSent && !clearSent && !spaceSent) {
          if (morseLen < sizeof(morse) - 1) {
            morse[morseLen++] = '.';
            morse[morseLen] = '\0';
          }
        }
        // Inactivity gap timer starts on key release for all dots and dashes!
        if (!clearSent && !spaceSent) {
          lastMorseInput = millis();
          lastSequence = morse;
        }
      }
    }
  }

  // While button is held down:
  if (stableButton == LOW) {
    unsigned long heldDuration = millis() - pressAt;

    if (mode == "copy") {
      if (!longPressSent && heldDuration >= COPY_LONG_PRESS_MS) {
        longPressSent = true;
        enqueueEvent("WIN_V");
      }
    } else {
      // Morse mode:
      if (!longPressSent && heldDuration >= morseDashMs) {
        // 1. Reached dash threshold (220ms): tentatively record '-'
        longPressSent = true;
        if (morseLen < sizeof(morse) - 1) {
          morse[morseLen++] = '-';
          morse[morseLen] = '\0';
        }
        lastSequence = morse;
      } else if (!spaceSent && !clearSent && heldDuration >= MORSE_SPACE_HOLD_MS && heldDuration < MORSE_CANCEL_MS) {
        // 2. Continuous hold (800ms) from idle: emit SPACE directly!
        if (morseLen == 1 && morse[0] == '-') {
          spaceSent = true;
          morseLen = 0;
          morse[0] = '\0';
          enqueueEvent("SPACE");
          lastSequence = "[SPACE]";
          lastCharacter = "[SPACE]";
        }
      } else if (!clearSent && heldDuration >= MORSE_CANCEL_MS) {
        // 3. Super long hold (1.8s): clear accidental mistake sequence
        clearSent = true;
        morseLen = 0;
        morse[0] = '\0';
        lastSequence = "[CLEARED]";
        lastCharacter = "[CLEARED]";
      }
    }
  }

  // Decode character after short inactivity pause (morseGapMs)
  // No auto-spacing: spaces are never automatically inserted
  if (mode == "morse" && morseLen > 0 &&
      stableButton == HIGH &&
      millis() - lastMorseInput >= morseGapMs) {
    char decoded = decodeMorse(morse);
    lastSequence = morse;

    if (decoded != '\0') {
      if (decoded == ' ') {
        enqueueEvent("SPACE");
        lastCharacter = "[SPACE]";
      } else {
        char buf[8];
        snprintf(buf, sizeof(buf), "TYPE:%c", decoded);
        enqueueEvent(buf);
        lastCharacter = String(decoded);
      }
    } else {
      lastCharacter = "?";
    }
    morseLen = 0;
    morse[0] = '\0';
  }
}

String jsonEscape(const String& input) {
  String out;
  out.reserve(input.length() + 4);
  for (size_t i = 0; i < input.length(); i++) {
    char c = input[i];
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (c == '\n') {
      out += "\\n";
    } else if (c == '\r') {
      out += "\\r";
    } else if ((uint8_t)c >= 32) {
      out += c;
    }
  }
  return out;
}

const char PAGE[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP-01 Morse Code Pad</title>
<style>
:root{
  --pad-bg:#0d141f;--pad-surface:#121a28;--pad-bezel:#2b394e;
  --line-dim:#364860;--line-active:#ffffff;
  --dot-green:#00e676;--dot-glow:rgba(0,230,118,0.45);
  --dash-amber:#ff5722;--dash-glow:rgba(255,87,34,0.45);
  --text:#e2e8f0;--text-dim:#7e92ab;--radius:24px;
}
*{box-sizing:border-box;margin:0;padding:0}
body{background:#070b12;color:var(--text);font:14px -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;line-height:1.4;-webkit-font-smoothing:antialiased;padding:16px 12px 40px}
main{max-width:520px;margin:0 auto}

/* Top App Bar */
.top-bar{display:flex;align-items:center;justify-content:space-between;padding:10px 14px;background:#0d1420;border:1px solid #1f2c3f;border-radius:14px;margin-bottom:16px}
.brand{display:flex;align-items:center;gap:8px;font-size:12px;font-weight:700;letter-spacing:1px;color:var(--text)}
.pulse-led{width:8px;height:8px;border-radius:50%;background:var(--dot-green);box-shadow:0 0 8px var(--dot-green);animation:pulse 2s infinite}
@keyframes pulse{0%,100%{opacity:1;transform:scale(1)}50%{opacity:0.3;transform:scale(0.8)}}
.top-actions{display:flex;align-items:center;gap:6px}
.btn-mini{background:#162234;border:1px solid #293a52;color:var(--text);padding:4px 10px;border-radius:6px;font-size:11px;font-weight:600;cursor:pointer;transition:all 0.15s}
.btn-mini:hover{background:#22334c}
.btn-mini.active{border-color:var(--dot-green);color:var(--dot-green)}

/* Tabs */
.mode-tabs{display:grid;grid-template-columns:1fr 1fr;gap:6px;background:#0d1420;padding:4px;border-radius:12px;border:1px solid #1f2c3f;margin-bottom:16px}
.mode-tab{padding:9px;border:none;border-radius:8px;font-size:12px;font-weight:700;color:var(--text-dim);background:transparent;cursor:pointer;transition:all 0.2s}
.mode-tab.active{background:#1a2538;color:#fff;border:1px solid #364860;box-shadow:0 0 12px rgba(0,0,0,0.5)}

/* --- MORSE CODE PAD DEVICE --- */
.morse-pad-device{
  background:radial-gradient(circle at 50% 20%,#131d2c 0%,#0a0f17 100%);
  border:3px solid #2d3d52;
  border-radius:28px;
  padding:16px 14px;
  box-shadow:0 12px 40px rgba(0,0,0,0.8),inset 0 1px 1px rgba(255,255,255,0.1),inset 0 0 20px rgba(0,0,0,0.6);
  position:relative;
  margin-bottom:18px;
}
.pad-top-header{
  display:flex;
  align-items:center;
  justify-content:space-between;
  padding:4px 10px 10px;
  border-bottom:1px solid rgba(255,255,255,0.04);
  position:relative;
}
.pad-headline{
  font-family:"Courier New",monospace;
  font-size:16px;
  font-weight:800;
  letter-spacing:4px;
  color:#cbd5e1;
}
.pad-antenna{
  display:flex;
  flex-direction:column;
  align-items:center;
  cursor:pointer;
}
.pad-antenna svg{
  width:26px;
  height:24px;
  stroke:#e2e8f0;
  stroke-width:2;
  fill:none;
}
.pad-corner-ring{
  width:14px;
  height:14px;
  border:2px solid #4a5c74;
  border-radius:50%;
  position:relative;
}
.pad-corner-ring::after{
  content:"";
  position:absolute;
  inset:3px;
  background:#4a5c74;
  border-radius:50%;
}

/* SVG Chart Layout */
#padSvg{
  width:100%;
  height:auto;
  display:block;
  margin:10px 0;
  touch-action:none;
}
.pad-edge{
  stroke:var(--line-dim);
  stroke-width:2.2;
  fill:none;
  stroke-linecap:round;
  stroke-linejoin:round;
  transition:all 0.15s ease;
}
.pad-edge.active{
  stroke:var(--line-active);
  stroke-width:3.5;
  filter:drop-shadow(0 0 6px var(--dot-green));
}
.pad-node-dot{
  fill:#0f1724;
  stroke:#4f637b;
  stroke-width:2.2;
  cursor:pointer;
  transition:all 0.15s ease;
}
.pad-node-dot.active{
  fill:var(--dot-green);
  stroke:#86efac;
  stroke-width:2.5;
  filter:drop-shadow(0 0 8px var(--dot-green));
}
.pad-node-dash{
  fill:#0f1724;
  stroke:#4f637b;
  stroke-width:2.2;
  cursor:pointer;
  transition:all 0.15s ease;
}
.pad-node-dash.active{
  fill:var(--dash-amber);
  stroke:#fdba74;
  stroke-width:2.5;
  filter:drop-shadow(0 0 8px var(--dash-amber));
}
.pad-reticle{
  fill:none;
  stroke:var(--dot-green);
  stroke-width:2.4;
  filter:drop-shadow(0 0 9px var(--dot-green));
  transition:all 0.2s cubic-bezier(0.34,1.56,0.64,1);
}
.pad-svg-title{
  font-family:"Courier New",monospace;
  font-size:18px;
  font-weight:800;
  letter-spacing:5px;
  fill:#cbd5e1;
  text-anchor:middle;
  user-select:none;
}
.pad-label{
  font-family:"Courier New",monospace;
  font-size:13px;
  font-weight:800;
  fill:#94a3b8;
  cursor:pointer;
  user-select:none;
  transition:fill 0.15s ease;
}
.pad-label.active-green{
  fill:var(--dot-green);
  font-size:14px;
  filter:drop-shadow(0 0 6px var(--dot-green));
}
.pad-label.active-amber{
  fill:var(--dash-amber);
  font-size:14px;
  filter:drop-shadow(0 0 6px var(--dash-amber));
}

/* Pad Control Box / Thumb Key */
.pad-control-box{
  background:#090f18;
  border:1.5px solid #23344a;
  border-radius:20px;
  padding:16px 14px 16px;
  margin-top:6px;
  box-shadow:inset 0 1px 1px rgba(255,255,255,0.06),0 4px 16px rgba(0,0,0,0.5);
  user-select:none;
}
.pad-legend-row{
  display:flex;
  align-items:center;
  justify-content:center;
  gap:22px;
  margin-bottom:12px;
}
.legend-pill{
  display:inline-flex;
  align-items:center;
  gap:7px;
  font-family:"Courier New",monospace;
  font-size:11.5px;
  font-weight:700;
  letter-spacing:1px;
  color:#94a3b8;
}
.pill-dot{width:10px;height:10px;border-radius:50%;background:var(--dot-green);box-shadow:0 0 9px var(--dot-green)}
.pill-dash{width:18px;height:7px;border-radius:3px;background:var(--dash-amber);box-shadow:0 0 9px var(--dash-amber)}
.thumb-pad-btn{
  width:100%;
  padding:8px 6px;
  background:transparent;
  border:none;
  color:#cbd5e1;
  font-family:"Courier New",monospace;
  font-size:13.5px;
  font-weight:800;
  letter-spacing:2px;
  cursor:pointer;
  text-align:center;
  user-select:none;
  touch-action:none;
  transition:all 0.1s ease;
}
.thumb-pad-btn:active,.thumb-pad-btn.pressed{
  transform:scale(0.98);
  color:var(--dot-green);
  text-shadow:0 0 12px var(--dot-green);
}

/* Decoded Output Stream */
.stream-card{background:#0d1420;border:1px solid #1f2c3f;border-radius:16px;padding:14px;margin-bottom:16px}
.stream-header{display:flex;align-items:center;justify-content:space-between;margin-bottom:8px}
.stream-title{font-size:11px;font-weight:700;letter-spacing:1px;color:var(--text-dim);text-transform:uppercase}
.stream-text{
  font-family:"Courier New",monospace;
  font-size:16px;
  color:#f1f5f9;
  background:#070b12;
  border:1px solid #1a2538;
  border-radius:8px;
  padding:12px;
  min-height:54px;
  max-height:90px;
  overflow-y:auto;
  word-break:break-all;
}
.stream-btns{display:flex;justify-content:flex-end;gap:8px;margin-top:8px}

/* Settings Form */
.settings-card{background:#0d1420;border:1px solid #1f2c3f;border-radius:16px;padding:16px;margin-bottom:16px}
.form-label{display:block;font-size:11px;font-weight:700;letter-spacing:1px;color:var(--text-dim);text-transform:uppercase;margin-bottom:6px}
.form-input{width:100%;background:#070b12;border:1px solid #1f2c3f;border-radius:8px;padding:10px 12px;color:var(--text);font-size:13px}
.form-input:focus{outline:none;border-color:var(--dot-green)}
.form-grid{display:grid;grid-template-columns:1fr 1fr 1fr;gap:8px}
.presets-row{display:flex;gap:6px;flex-wrap:wrap;margin:10px 0}
.chip{background:#141d2c;border:1px solid #233348;border-radius:6px;padding:5px 10px;font-size:11px;font-weight:600;color:var(--text-dim);cursor:pointer}
.chip:hover{color:#fff;border-color:#38bdf8}
.btn-save{width:100%;padding:12px;background:linear-gradient(135deg,#047857,#10b981);border:none;border-radius:8px;color:#fff;font-size:13px;font-weight:700;letter-spacing:1px;cursor:pointer;margin-top:12px}

/* Toast */
#toast{position:fixed;bottom:20px;left:50%;transform:translateX(-50%) translateY(80px);background:var(--dot-green);color:#000;font-weight:700;font-size:12px;padding:10px 20px;border-radius:20px;box-shadow:0 6px 20px rgba(0,0,0,0.6);opacity:0;transition:all 0.25s ease;z-index:999}
#toast.show{transform:translateX(-50%) translateY(0);opacity:1}
[hidden]{display:none!important}
</style>
</head>
<body>
<main>

<!-- Top Bar -->
<div class="top-bar">
  <div class="brand">
    <span class="pulse-led"></span>
    <span>ESP-01 CONTROLLER</span>
  </div>
  <div class="top-actions">
    <button class="btn-mini" id="rssiBadge" type="button">-- dBm</button>
    <button class="btn-mini active" id="audioToggle" type="button" onclick="toggleSound()">🔊 SOUND</button>
  </div>
</div>

<!-- Mode Switcher -->
<div class="mode-tabs">
  <button class="mode-tab active" id="tabMorse" type="button" onclick="setMode('morse')">⚡ MORSE CODE</button>
  <button class="mode-tab" id="tabCopy" type="button" onclick="setMode('copy')">📋 CLIPBOARD</button>
</div>

<!-- ================= MORSE PAD VIEW ================= -->
<div id="morseView">
  <!-- Cybernetic Morse Code Pad Device -->
  <div class="morse-pad-device">
    <!-- Dichotomic Morse Map SVG -->
    <svg id="padSvg" viewBox="0 0 460 520" role="img" aria-label="Morse Code Matrix">
      <defs>
        <filter id="glowGreen" x="-20%" y="-20%" width="140%" height="140%">
          <feDropShadow dx="0" dy="0" stdDeviation="4" flood-color="#00e676"/>
        </filter>
        <filter id="glowAmber" x="-20%" y="-20%" width="140%" height="140%">
          <feDropShadow dx="0" dy="0" stdDeviation="4" flood-color="#ff5722"/>
        </filter>
      </defs>

      <!-- TOP HEADER (Unified vector layout matching reference image) -->
      <text x="105" y="38" class="pad-svg-title">MORSE</text>
      <!-- Antenna Inverted Triangle Symbol directly connected to mast -->
      <polygon id="antenna_icon" points="216,18 244,18 230,36" fill="none" stroke="#cbd5e1" stroke-width="2.4" stroke-linejoin="round" style="cursor:pointer" onclick="resetPad()"/>
      <line id="e_root" class="pad-edge" x1="230" y1="36" x2="230" y2="78"/>
      <text x="355" y="38" class="pad-svg-title">CODE</text>
      <!-- Circular Indicator Badge in Top Right -->
      <circle cx="430" cy="32" r="11" fill="none" stroke="#475569" stroke-width="2.4"/>
      <circle cx="430" cy="32" r="5" fill="#475569"/>

      <!-- Connection Lines / Tracks -->
      <!-- Top Horizontal Rail: T M O (left) -->
      <line id="e_ant_T" class="pad-edge" x1="230" y1="78" x2="190" y2="78"/>
      <line id="e_T_M" class="pad-edge" x1="190" y1="78" x2="130" y2="78"/>
      <line id="e_M_O" class="pad-edge" x1="130" y1="78" x2="65" y2="78"/>
      <!-- Top Horizontal Rail: E I S H (right) -->
      <line id="e_ant_E" class="pad-edge" x1="230" y1="78" x2="275" y2="78"/>
      <line id="e_E_I" class="pad-edge" x1="275" y1="78" x2="330" y2="78"/>
      <line id="e_I_S" class="pad-edge" x1="330" y1="78" x2="385" y2="78"/>
      <line id="e_S_H" class="pad-edge" x1="385" y1="78" x2="430" y2="78"/>

      <!-- LEFT SIDE BRANCHES (under T and M) -->
      <!-- Drop from T to N and D -->
      <line id="e_T_N" class="pad-edge" x1="190" y1="78" x2="190" y2="260"/>
      <line id="e_N_K" class="pad-edge" x1="190" y1="260" x2="130" y2="260"/>
      <line id="e_K_Y" class="pad-edge" x1="130" y1="260" x2="65" y2="260"/>
      <line id="e_K_C" class="pad-edge" x1="130" y1="260" x2="130" y2="340"/>
      <line id="e_N_D" class="pad-edge" x1="190" y1="260" x2="190" y2="400"/>
      <line id="e_D_X" class="pad-edge" x1="190" y1="400" x2="125" y2="400"/>
      <line id="e_D_B" class="pad-edge" x1="190" y1="400" x2="190" y2="465"/>

      <!-- Drop from M to G, Z, Q -->
      <line id="e_M_G" class="pad-edge" x1="130" y1="78" x2="130" y2="150"/>
      <line id="e_G_Q" class="pad-edge" x1="130" y1="150" x2="65" y2="150"/>
      <line id="e_G_Z" class="pad-edge" x1="130" y1="150" x2="130" y2="220"/>

      <!-- RIGHT SIDE BRANCHES (under E, I, S) -->
      <!-- Drop from E to A and W -->
      <line id="e_E_A" class="pad-edge" x1="275" y1="78" x2="275" y2="260"/>
      <line id="e_A_R" class="pad-edge" x1="275" y1="260" x2="330" y2="260"/>
      <line id="e_R_L" class="pad-edge" x1="330" y1="260" x2="385" y2="260"/>
      <line id="e_A_W" class="pad-edge" x1="275" y1="260" x2="275" y2="400"/>
      <line id="e_W_P" class="pad-edge" x1="275" y1="400" x2="330" y2="400"/>
      <line id="e_W_J" class="pad-edge" x1="275" y1="400" x2="275" y2="465"/>

      <!-- Drop from I to U, F, SPACE -->
      <line id="e_I_U" class="pad-edge" x1="330" y1="78" x2="330" y2="150"/>
      <line id="e_U_F" class="pad-edge" x1="330" y1="150" x2="330" y2="220"/>
      <line id="e_F_SP" class="pad-edge" x1="330" y1="220" x2="373" y2="220"/>

      <!-- Drop from S to V -->
      <line id="e_S_V" class="pad-edge" x1="385" y1="78" x2="385" y2="150"/>

      <!-- Drop from H -->
      <line id="e_H_drop" class="pad-edge" x1="430" y1="78" x2="430" y2="150"/>

      <!-- NODES (Pills = Dash, Circles = Dot) -->
      <!-- TOP ROW: O, M, T (dashes) | E, I, S, H (dots) -->
      <rect id="n_O" class="pad-node-dash" x="53" y="72" width="24" height="12" rx="4"/>
      <text id="l_O" class="pad-label" x="65" y="60" text-anchor="middle">O</text>

      <rect id="n_M" class="pad-node-dash" x="118" y="72" width="24" height="12" rx="4"/>
      <text id="l_M" class="pad-label" x="130" y="60" text-anchor="middle">M</text>

      <rect id="n_T" class="pad-node-dash" x="178" y="72" width="24" height="12" rx="4"/>
      <text id="l_T" class="pad-label" x="190" y="60" text-anchor="middle">T</text>

      <circle id="n_E" class="pad-node-dot" cx="275" cy="78" r="8"/>
      <text id="l_E" class="pad-label" x="275" y="60" text-anchor="middle">E</text>

      <circle id="n_I" class="pad-node-dot" cx="330" cy="78" r="8"/>
      <text id="l_I" class="pad-label" x="330" y="60" text-anchor="middle">I</text>

      <circle id="n_S" class="pad-node-dot" cx="385" cy="78" r="8"/>
      <text id="l_S" class="pad-label" x="385" y="60" text-anchor="middle">S</text>

      <circle id="n_H" class="pad-node-dot" cx="430" cy="78" r="8"/>
      <text id="l_H" class="pad-label" x="430" y="60" text-anchor="middle">H</text>

      <!-- ROW 2: Q, G (left) | U, V (right) -->
      <rect id="n_Q" class="pad-node-dash" x="53" y="144" width="24" height="12" rx="4"/>
      <text id="l_Q" class="pad-label" x="65" y="174" text-anchor="middle">Q</text>

      <circle id="n_G" class="pad-node-dot" cx="130" cy="150" r="8"/>
      <text id="l_G" class="pad-label" x="146" y="154">G</text>

      <rect id="n_U" class="pad-node-dash" x="324" y="138" width="12" height="24" rx="4"/>
      <text id="l_U" class="pad-label" x="306" y="154">U</text>

      <rect id="n_V" class="pad-node-dash" x="379" y="138" width="12" height="24" rx="4"/>
      <text id="l_V" class="pad-label" x="403" y="154">V</text>

      <!-- ROW 3: Z (left) | F, SPACE (right) -->
      <circle id="n_Z" class="pad-node-dot" cx="130" cy="220" r="8"/>
      <text id="l_Z" class="pad-label" x="146" y="224">Z</text>

      <circle id="n_F" class="pad-node-dot" cx="330" cy="220" r="8"/>
      <text id="l_F" class="pad-label" x="346" y="224">F</text>

      <!-- SPACE Node (..--) -->
      <rect id="n_SP" class="pad-node-dash" x="373" y="214" width="24" height="12" rx="4"/>
      <text id="l_SP" class="pad-label" x="404" y="224">SPACE</text>

      <!-- ROW 4: Y, K, N (left) | A, R, L (right) -->
      <rect id="n_Y" class="pad-node-dash" x="53" y="254" width="24" height="12" rx="4"/>
      <text id="l_Y" class="pad-label" x="65" y="244" text-anchor="middle">Y</text>

      <rect id="n_K" class="pad-node-dash" x="118" y="254" width="24" height="12" rx="4"/>
      <text id="l_K" class="pad-label" x="130" y="244" text-anchor="middle">K</text>

      <circle id="n_N" class="pad-node-dot" cx="190" cy="260" r="8"/>
      <text id="l_N" class="pad-label" x="206" y="264">N</text>

      <rect id="n_A" class="pad-node-dash" x="269" y="248" width="12" height="24" rx="4"/>
      <text id="l_A" class="pad-label" x="251" y="264">A</text>

      <circle id="n_R" class="pad-node-dot" cx="330" cy="260" r="8"/>
      <text id="l_R" class="pad-label" x="330" y="284" text-anchor="middle">R</text>

      <circle id="n_L" class="pad-node-dot" cx="385" cy="260" r="8"/>
      <text id="l_L" class="pad-label" x="406" y="264">L</text>

      <!-- ROW 5: C (left) -->
      <circle id="n_C" class="pad-node-dot" cx="130" cy="340" r="8"/>
      <text id="l_C" class="pad-label" x="146" y="344">C</text>

      <!-- ROW 6: X, D, B (left) | W, P, J (right) -->
      <rect id="n_X" class="pad-node-dash" x="113" y="394" width="24" height="12" rx="4"/>
      <text id="l_X" class="pad-label" x="100" y="404">X</text>

      <circle id="n_D" class="pad-node-dot" cx="190" cy="400" r="8"/>
      <text id="l_D" class="pad-label" x="206" y="404">D</text>

      <circle id="n_B" class="pad-node-dot" cx="190" cy="465" r="8"/>
      <text id="l_B" class="pad-label" x="206" y="469">B</text>

      <rect id="n_W" class="pad-node-dash" x="269" y="388" width="12" height="24" rx="4"/>
      <text id="l_W" class="pad-label" x="251" y="404">W</text>

      <circle id="n_P" class="pad-node-dot" cx="330" cy="400" r="8"/>
      <text id="l_P" class="pad-label" x="346" y="404">P</text>

      <rect id="n_J" class="pad-node-dash" x="269" y="453" width="12" height="24" rx="4"/>
      <text id="l_J" class="pad-label" x="251" y="469">J</text>

      <!-- Target Reticle Ring (Moves to selected node) -->
      <circle id="reticle" class="pad-reticle" cx="-50" cy="-50" r="16" style="display:none"/>
    </svg>

    <!-- Pad Control / Key Box -->
    <div class="pad-control-box">
      <div class="pad-legend-row">
        <span class="legend-pill"><span class="pill-dot"></span> TAP = DOT (·)</span>
        <span class="legend-pill"><span class="pill-dash"></span> HOLD = DASH (—)</span>
        <span class="legend-pill" style="cursor:pointer" onclick="sendSpace()" title="Click to send SPACE"><span class="pill-dash" style="background:#38bdf8;box-shadow:0 0 9px #38bdf8"></span> ␣ SPACE (..-- )</span>
      </div>
      <div style="display:flex;gap:8px">
        <button class="thumb-pad-btn" id="thumbKeyPad" type="button" style="flex:1">
          PRESS &amp; HOLD THUMB KEY PAD
        </button>
        <button class="btn-mini" type="button" style="padding:10px 16px;border-radius:10px;font-size:12px;font-weight:800;letter-spacing:1px;border-color:#38bdf8;color:#38bdf8" onclick="sendSpace()">
          ␣ SPACE
        </button>
      </div>
    </div>
  </div>

  <!-- Live Decoded Stream Terminal -->
  <div class="stream-card">
    <div class="stream-header">
      <span class="stream-title">Live Decoded Text Stream</span>
      <span style="font-family:monospace;font-size:12px;color:var(--dot-green)" id="liveLetterBadge">Active: [·]</span>
    </div>
    <div class="stream-text" id="streamBox">Waiting for input...</div>
    <div class="stream-btns">
      <button class="btn-mini" type="button" onclick="copyStream()">📋 Copy Stream</button>
      <button class="btn-mini" type="button" onclick="clearStream()">🗑 Clear</button>
    </div>
  </div>

  <!-- Morse Timing Settings -->
  <div class="settings-card">
    <div class="stream-header">
      <span class="stream-title">Morse Speed &amp; Thresholds</span>
    </div>
    <div class="presets-row">
      <button class="chip" type="button" onclick="setWpm(260,600)">🐢 Relaxed</button>
      <button class="chip" type="button" onclick="setWpm(220,500)">⚡ Normal</button>
      <button class="chip" type="button" onclick="setWpm(160,380)">🚀 Fast</button>
    </div>
    <div class="form-grid" style="grid-template-columns:1fr 1fr">
      <div>
        <label class="form-label">Dash Threshold (ms)</label>
        <input class="form-input" type="number" id="dashInput" min="100" max="800" value="220">
      </div>
      <div>
        <label class="form-label">Letter Pause (ms)</label>
        <input class="form-input" type="number" id="gapInput" min="200" max="1500" value="500">
      </div>
    </div>
    <button class="btn-save" type="button" onclick="saveSettings()">Save Configuration</button>
  </div>
</div>

<!-- ================= CLIPBOARD CONTROLLER VIEW ================= -->
<div id="copyView" hidden>
  <div class="settings-card">
    <div class="stream-header">
      <span class="stream-title">Clipboard Controller</span>
    </div>
    <label class="form-label">Stored Clipboard Text</label>
    <input class="form-input" id="clipboardTextInput" maxlength="100" placeholder="Text to copy...">
    <div class="presets-row" style="margin-top:10px">
      <button class="chip" type="button" onclick="setText('Hello from ESP-01')">Hello from ESP-01</button>
      <button class="chip" type="button" onclick="setText('user@example.com')">📧 Email</button>
      <button class="chip" type="button" onclick="setText('Password123!')">🔑 Password</button>
      <button class="chip" type="button" onclick="setText('git pull origin main')">💻 Git Pull</button>
    </div>
    <button class="btn-save" type="button" onclick="saveSettings()">Save Clipboard Text</button>
  </div>

  <div class="stream-card">
    <div class="stream-header">
      <span class="stream-title">Hardware Shortcut Guide</span>
    </div>
    <div style="font-size:12px;color:var(--text-dim);line-height:1.6">
      • <b style="color:#fff">Short Click (&lt;0.5s):</b> Simulates <b style="color:var(--dot-green)">Ctrl + C</b> to copy whatever text you highlighted with your mouse.<br>
      • <b style="color:#fff">Long Hold (&ge;0.5s):</b> Simulates <b style="color:var(--dash-amber)">Win + V</b> to open Windows Clipboard History.
    </div>
  </div>
</div>

<div id="toast">✓ Saved to EEPROM</div>
</main>

<script>
let currentMode = 'morse';
let audioEnabled = true;
let audioCtx = null;
let streamText = '';
let lastChar = '';

function toggleSound(){
  audioEnabled = !audioEnabled;
  const b = document.querySelector('#audioToggle');
  b.textContent = audioEnabled ? '🔊 SOUND' : '🔇 MUTED';
  b.classList.toggle('active', audioEnabled);
}

function playBeep(freq, dur){
  if (!audioEnabled) return;
  try {
    if (!audioCtx) audioCtx = new (window.AudioContext || window.webkitAudioContext)();
    if (audioCtx.state === 'suspended') audioCtx.resume();
    const osc = audioCtx.createOscillator();
    const gain = audioCtx.createGain();
    osc.type = 'sine';
    osc.frequency.setValueAtTime(freq || 700, audioCtx.currentTime);
    gain.gain.setValueAtTime(0.001, audioCtx.currentTime);
    gain.gain.exponentialRampToValueAtTime(0.18, audioCtx.currentTime + 0.008);
    gain.gain.exponentialRampToValueAtTime(0.001, audioCtx.currentTime + dur - 0.008);
    osc.connect(gain);
    gain.connect(audioCtx.destination);
    osc.start();
    osc.stop(audioCtx.currentTime + dur);
  } catch(e){}
}

function setMode(m){
  currentMode = m;
  document.querySelector('#tabMorse').classList.toggle('active', m === 'morse');
  document.querySelector('#tabCopy').classList.toggle('active', m === 'copy');
  document.querySelector('#morseView').hidden = (m !== 'morse');
  document.querySelector('#copyView').hidden = (m !== 'copy');
}

function showToast(msg){
  const t = document.querySelector('#toast');
  t.textContent = msg;
  t.classList.add('show');
  setTimeout(() => t.classList.remove('show'), 2200);
}

function setText(v){
  document.querySelector('#clipboardTextInput').value = v;
}

function setWpm(dash, gap){
  document.querySelector('#dashInput').value = dash;
  document.querySelector('#gapInput').value = gap;
}

async function loadConfig(){
  try{
    const r = await fetch('/config');
    if (r.ok){
      const d = await r.json();
      if (d.mode) setMode(d.mode);
      if (d.text !== undefined) document.querySelector('#clipboardTextInput').value = d.text;
      if (d.dash) document.querySelector('#dashInput').value = d.dash;
      if (d.gap) document.querySelector('#gapInput').value = d.gap;
      if (d.rssi !== undefined) document.querySelector('#rssiBadge').textContent = `${d.rssi} dBm`;
    }
  }catch(e){}
}
loadConfig();

async function saveSettings(){
  const p = new URLSearchParams();
  p.set('mode', currentMode);
  p.set('text', document.querySelector('#clipboardTextInput').value || '');
  p.set('dash', document.querySelector('#dashInput').value || '220');
  p.set('gap', document.querySelector('#gapInput').value || '500');
  p.set('ajax', '1');
  try{
    const r = await fetch('/save?' + p.toString());
    if (r.ok) showToast('✓ Saved to ESP-01 EEPROM');
  }catch(e){
    showToast('Failed to save');
  }
}

/* ================= MORSE PAD MATRIX ENGINE ================= */
// Exact branching graph paths matching the tactical reference layout:
const morseAlphabet = {
  'E': { code: '.', type: 'dot', node: 'n_E', label: 'l_E', x: 275, y: 78, edges: ['e_root', 'e_ant_E'] },
  'I': { code: '..', type: 'dot', node: 'n_I', label: 'l_I', x: 330, y: 78, edges: ['e_root', 'e_ant_E', 'e_E_I'] },
  'S': { code: '...', type: 'dot', node: 'n_S', label: 'l_S', x: 385, y: 78, edges: ['e_root', 'e_ant_E', 'e_E_I', 'e_I_S'] },
  'H': { code: '....', type: 'dot', node: 'n_H', label: 'l_H', x: 430, y: 78, edges: ['e_root', 'e_ant_E', 'e_E_I', 'e_I_S', 'e_S_H'] },
  'V': { code: '...-', type: 'dash', node: 'n_V', label: 'l_V', x: 385, y: 150, edges: ['e_root', 'e_ant_E', 'e_E_I', 'e_I_S', 'e_S_V'] },
  'U': { code: '..-', type: 'dash', node: 'n_U', label: 'l_U', x: 330, y: 150, edges: ['e_root', 'e_ant_E', 'e_E_I', 'e_I_U'] },
  'F': { code: '..-.', type: 'dot', node: 'n_F', label: 'l_F', x: 330, y: 220, edges: ['e_root', 'e_ant_E', 'e_E_I', 'e_I_U', 'e_U_F'] },
  'A': { code: '.-', type: 'dash', node: 'n_A', label: 'l_A', x: 275, y: 260, edges: ['e_root', 'e_ant_E', 'e_E_A'] },
  'R': { code: '.-.', type: 'dot', node: 'n_R', label: 'l_R', x: 330, y: 260, edges: ['e_root', 'e_ant_E', 'e_E_A', 'e_A_R'] },
  'L': { code: '.-..', type: 'dot', node: 'n_L', label: 'l_L', x: 385, y: 260, edges: ['e_root', 'e_ant_E', 'e_E_A', 'e_A_R', 'e_R_L'] },
  'W': { code: '.--', type: 'dash', node: 'n_W', label: 'l_W', x: 275, y: 400, edges: ['e_root', 'e_ant_E', 'e_E_A', 'e_A_W'] },
  'P': { code: '.--.', type: 'dot', node: 'n_P', label: 'l_P', x: 330, y: 400, edges: ['e_root', 'e_ant_E', 'e_E_A', 'e_A_W', 'e_W_P'] },
  'J': { code: '.---', type: 'dash', node: 'n_J', label: 'l_J', x: 275, y: 465, edges: ['e_root', 'e_ant_E', 'e_E_A', 'e_A_W', 'e_W_J'] },
  'T': { code: '-', type: 'dash', node: 'n_T', label: 'l_T', x: 190, y: 78, edges: ['e_root', 'e_ant_T'] },
  'M': { code: '--', type: 'dash', node: 'n_M', label: 'l_M', x: 130, y: 78, edges: ['e_root', 'e_ant_T', 'e_T_M'] },
  'O': { code: '---', type: 'dash', node: 'n_O', label: 'l_O', x: 65, y: 78, edges: ['e_root', 'e_ant_T', 'e_T_M', 'e_M_O'] },
  'G': { code: '--.', type: 'dot', node: 'n_G', label: 'l_G', x: 130, y: 150, edges: ['e_root', 'e_ant_T', 'e_T_M', 'e_M_G'] },
  'Z': { code: '--..', type: 'dot', node: 'n_Z', label: 'l_Z', x: 130, y: 220, edges: ['e_root', 'e_ant_T', 'e_T_M', 'e_M_G', 'e_G_Z'] },
  'Q': { code: '--.-', type: 'dash', node: 'n_Q', label: 'l_Q', x: 65, y: 150, edges: ['e_root', 'e_ant_T', 'e_T_M', 'e_M_G', 'e_G_Q'] },
  'N': { code: '-.', type: 'dot', node: 'n_N', label: 'l_N', x: 190, y: 260, edges: ['e_root', 'e_ant_T', 'e_T_N'] },
  'K': { code: '-.-', type: 'dash', node: 'n_K', label: 'l_K', x: 130, y: 260, edges: ['e_root', 'e_ant_T', 'e_T_N', 'e_N_K'] },
  'Y': { code: '-.--', type: 'dash', node: 'n_Y', label: 'l_Y', x: 65, y: 260, edges: ['e_root', 'e_ant_T', 'e_T_N', 'e_N_K', 'e_K_Y'] },
  'C': { code: '-.-.', type: 'dot', node: 'n_C', label: 'l_C', x: 130, y: 340, edges: ['e_root', 'e_ant_T', 'e_T_N', 'e_N_K', 'e_K_C'] },
  'D': { code: '-..', type: 'dot', node: 'n_D', label: 'l_D', x: 190, y: 400, edges: ['e_root', 'e_ant_T', 'e_T_N', 'e_N_D'] },
  'X': { code: '-..-', type: 'dash', node: 'n_X', label: 'l_X', x: 125, y: 400, edges: ['e_root', 'e_ant_T', 'e_T_N', 'e_N_D', 'e_D_X'] },
  'B': { code: '-...', type: 'dot', node: 'n_B', label: 'l_B', x: 190, y: 465, edges: ['e_root', 'e_ant_T', 'e_T_N', 'e_N_D', 'e_D_B'] },
  'SP': { code: '..--', type: 'dash', node: 'n_SP', label: 'l_SP', x: 385, y: 220, edges: ['e_root', 'e_ant_E', 'e_E_I', 'e_I_U', 'e_U_F', 'e_F_SP'] }
};

// Map code to letter
const codeMap = {};
Object.entries(morseAlphabet).forEach(([letter, data]) => {
  codeMap[data.code] = letter;
});
codeMap['.-.-'] = 'SP';

function resetPad(){
  document.querySelectorAll('.pad-edge').forEach(e => e.classList.remove('active'));
  document.querySelectorAll('.pad-node-dot, .pad-node-dash').forEach(n => n.classList.remove('active'));
  document.querySelectorAll('.pad-label').forEach(l => l.classList.remove('active-green', 'active-amber'));
  const r = document.querySelector('#reticle');
  if (r) r.style.display = 'none';
  document.querySelector('#liveLetterBadge').textContent = 'Active: [·]';
}

function highlightPath(code){
  resetPad();
  if (!code) return;

  // Track partial codes along the sequence to light up intermediate nodes
  let partial = '';
  for (let i = 0; i < code.length; i++){
    partial += code[i];
    const letter = codeMap[partial];
    if (letter){
      const info = morseAlphabet[letter];
      // Light up edges
      info.edges.forEach(edgeId => {
        const el = document.getElementById(edgeId);
        if (el) el.classList.add('active');
      });
      // Light up node
      const n = document.getElementById(info.node);
      if (n) n.classList.add('active');
      // Light up label
      const l = document.getElementById(info.label);
      if (l) l.classList.add(info.type === 'dot' ? 'active-green' : 'active-amber');
    }
  }

  // Put targeting reticle on final letter if matched
  const finalLetter = codeMap[code];
  if (finalLetter){
    const target = morseAlphabet[finalLetter];
    const ret = document.querySelector('#reticle');
    if (ret){
      ret.setAttribute('cx', target.x);
      ret.setAttribute('cy', target.y);
      ret.style.display = 'block';
    }
    const displayName = finalLetter === 'SP' ? 'SPACE' : finalLetter;
    document.querySelector('#liveLetterBadge').textContent = `Active: ${displayName} [${code}]`;
  } else {
    document.querySelector('#liveLetterBadge').textContent = `Sequence: [${code}]`;
  }
}

async function sendSpace(){
  highlightPath('..--');
  playMorseAudio('..--');
  try{
    await fetch('/space');
  }catch(e){}
  streamText += ' ';
  const box = document.querySelector('#streamBox');
  if (box){
    box.textContent = streamText || 'Waiting for input...';
    box.scrollTop = box.scrollHeight;
  }
}

// Click on any node or label in SVG to test and hear sound
Object.entries(morseAlphabet).forEach(([ch, item]) => {
  const nodeEl = document.getElementById(item.node);
  const labelEl = document.getElementById(item.label);
  function handleSelect(){
    highlightPath(item.code);
    playMorseAudio(item.code);
    if (ch === 'SP'){
      sendSpace();
    }
  }
  if (nodeEl) nodeEl.addEventListener('click', handleSelect);
  if (labelEl) labelEl.addEventListener('click', handleSelect);
});

function playMorseAudio(code){
  let t = 0;
  for (const s of code){
    setTimeout(() => {
      playBeep(700, s === '.' ? 0.08 : 0.22);
    }, t * 1000);
    t += (s === '.' ? 0.16 : 0.30);
  }
}

/* Polling practice events from ESP-01 */
async function pollPractice(){
  try{
    const r = await fetch('/practice', { cache: 'no-store' });
    if (r.ok){
      const d = await r.json();
      if (d.sequence !== undefined && d.sequence){
        highlightPath(d.sequence);
      }
      if (d.character !== undefined && d.character && d.character !== lastChar){
        lastChar = d.character;
        const box = document.querySelector('#streamBox');
        if (d.character === '[SPACE]'){
          streamText += ' ';
          highlightPath('..--');
          playBeep(500, 0.06);
        } else if (d.character === '[CLEARED]'){
          resetPad();
          playBeep(350, 0.12);
        } else if (d.character !== '?'){
          streamText += d.character;
          playBeep(850, 0.08);
        }
        box.textContent = streamText || 'Waiting for input...';
        box.scrollTop = box.scrollHeight;
      }
    }
  }catch(e){}
  setTimeout(pollPractice, 180);
}
pollPractice();

function copyStream(){
  if (streamText){
    navigator.clipboard.writeText(streamText);
    showToast('✓ Stream copied to clipboard');
  }
}

function clearStream(){
  streamText = '';
  document.querySelector('#streamBox').textContent = 'Waiting for input...';
}

/* Virtual Thumb Key Pad */
let padDown = 0;
const thumbBtn = document.querySelector('#thumbKeyPad');
function startKey(e){
  e.preventDefault();
  padDown = Date.now();
  thumbBtn.classList.add('pressed');
  playBeep(700, 0.5);
}
function stopKey(e){
  e.preventDefault();
  thumbBtn.classList.remove('pressed');
  if (!padDown) return;
  const dur = Date.now() - padDown;
  const isDash = dur >= 220;
  playBeep(700, isDash ? 0.22 : 0.08);
}
thumbBtn.addEventListener('mousedown', startKey);
thumbBtn.addEventListener('mouseup', stopKey);
thumbBtn.addEventListener('touchstart', startKey);
thumbBtn.addEventListener('touchend', stopKey);

// Highlight default node 'L' initially to showcase the layout
highlightPath('.-..');
</script>
</body>
</html>
)HTML";

void setupOTA() {
  ArduinoOTA.setHostname(otaHostname);
  ArduinoOTA.setPassword(otaPassword);
  ArduinoOTA.onStart([]() { Serial.println(F("OTA update started")); });
  ArduinoOTA.onEnd([]() { Serial.println(F("OTA update complete")); });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf_P(PSTR("OTA error: %u\n"), error);
  });
  ArduinoOTA.begin();
}

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  loadSettings();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print(F("Connecting to Wi-Fi"));
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print('.');
  }

  Serial.println();
  Serial.print(F("Web interface: http://"));
  Serial.println(WiFi.localIP());

  setupOTA();

  server.on("/", HTTP_GET, []() {
    server.send_P(200, "text/html", PAGE);
  });

  server.on("/config", HTTP_GET, []() {
    String json;
    json.reserve(160);
    json += "{\"mode\":\"" + mode + "\",";
    json += "\"text\":\"" + jsonEscape(clipboardText) + "\",";
    json += "\"dash\":" + String(morseDashMs) + ",";
    json += "\"gap\":" + String(morseGapMs) + ",";
    json += "\"word\":" + String(wordGapMs) + ",";
    json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
    json += "\"ip\":\"" + WiFi.localIP().toString() + "\"}";
    server.send(200, "application/json", json);
  });

  server.on("/save", HTTP_GET, []() {
    if (server.hasArg("mode")) {
      String requested = server.arg("mode");
      if (requested == "copy" || requested == "morse") mode = requested;
    }
    if (server.hasArg("text")) {
      clipboardText = server.arg("text");
      if (clipboardText.length() > 100) clipboardText = clipboardText.substring(0, 100);
    }
    if (server.hasArg("dash")) {
      unsigned long val = server.arg("dash").toInt();
      if (val >= 100 && val <= 800) morseDashMs = val;
    }
    if (server.hasArg("gap")) {
      unsigned long val = server.arg("gap").toInt();
      if (val >= 200 && val <= 1500) morseGapMs = val;
    }
    if (server.hasArg("word")) {
      unsigned long val = server.arg("word").toInt();
      if (val >= 600 && val <= 3000) wordGapMs = val;
    }
    saveSettings();
    if (server.hasArg("ajax")) {
      server.send(200, "application/json", "{\"status\":\"ok\"}");
    } else {
      server.sendHeader("Location", "/");
      server.send(303, "text/plain", "");
    }
  });

  server.on("/event", HTTP_GET, []() {
    server.send(200, "text/plain", dequeueEvent());
  });

  server.on("/text", HTTP_GET, []() {
    server.send(200, "text/plain", clipboardText);
  });

  server.on("/status", HTTP_GET, []() {
    server.send(200, "text/plain", mode);
  });

  server.on("/practice", HTTP_GET, []() {
    String json;
    json.reserve(64);
    json += "{\"sequence\":\"";
    json += jsonEscape(lastSequence);
    json += "\",\"character\":\"";
    json += jsonEscape(lastCharacter);
    json += "\"}";
    server.send(200, "application/json", json);
  });

  server.on("/space", HTTP_GET, []() {
    enqueueEvent("SPACE");
    lastSequence = "[SPACE]";
    lastCharacter = "[SPACE]";
    server.send(200, "application/json", "{\"status\":\"ok\"}");
  });

  server.begin();
  Serial.println(F("Web server started; OTA enabled"));
}

void loop() {
  ArduinoOTA.handle();
  server.handleClient();
  handleButton();
}
