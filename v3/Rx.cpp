/*
 *
 *  WIRING
 * ─────────────────────────────────────────────────────────────
 *  NodeMCU  │  Device        │  GPIO
 * ──────────┼────────────────┼──────────────────────────────
 *  D8       │  LoRa NSS      │  GPIO15
 *  D5       │  LoRa SCK      │  GPIO14
 *  D6       │  LoRa MISO     │  GPIO12
 *  D7       │  LoRa MOSI     │  GPIO13
 *  D0       │  LoRa RESET    │  GPIO16
 *  D3       │  LoRa DIO0     │  GPIO0   INPUT_PULLUP
 *  D2       │  OLED SDA      │  GPIO4
 *  D1       │  OLED SCL      │  GPIO5
 *  RX/GPIO3 │  Button → GND  │  GPIO3   INPUT_PULLUP
 *  D4       │  LED (onboard) │  GPIO2
 * ─────────────────────────────────────────────────────────────
 */

#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <deque>

// ── Pins ──────────────────────────────────────────────────────
#define LORA_SS    D8   // GPIO15
#define LORA_RST   D0   // GPIO16
#define LORA_DIO0  D3   // GPIO0  — INPUT_PULLUP at boot
#define OLED_SDA   D2   // GPIO4
#define OLED_SCL   D1   // GPIO5
#define BUTTON_PIN 3    // GPIO3 / RX0  — HW-BUG-1 fix
#define LED_PIN    2    // GPIO2 / D4   — separated from button
#define LORA_FREQ  433E6  // must match TX node

// ── OLED ──────────────────────────────────────────────────────
#define SCREEN_W      128
#define SCREEN_H       64
#define OLED_RESET     -1
#define OLED_ADDR    0x3C  // try 0x3D if blank

Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, OLED_RESET);

// ── Message entry ─────────────────────────────────────────────
struct MsgEntry {
  String msgId;
  String text;
  bool   isSOS;
  bool   hasGps;
  float  gpsLat;
  float  gpsLng;
  int    rssi;
};

// ── State ─────────────────────────────────────────────────────
std::deque<MsgEntry> msgQueue;
std::deque<String>   seenIds;

const uint8_t MAX_QUEUE    = 20;
const uint8_t MAX_SEEN_IDS = 40;

uint16_t      ackCount    = 0;
bool          oledOk      = false;
bool          loraOk      = false;
unsigned long lastBlinkMs = 0;
bool          blinkState  = false;

#define BLINK_NORMAL_MS  400
#define BLINK_SOS_MS     180
#define READ_TIME_MS    1800

// ── Forward declarations ──────────────────────────────────────
void     oledIdle();
void     oledPopup();
void     oledShowMessage(const MsgEntry &m);
void     oledAckSent(uint8_t remaining);
void     sendAck(const String &msgId);
void     ledFlash(uint8_t times = 1);
MsgEntry parsePacket(const String &raw, int rssi);
bool     isDuplicate(const String &id);

// ══════════════════════════════════════════════════════════════
//  SETUP
// ══════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println(F("\n=== Rescue Node Minimal Boot ==="));

  // ── Pin setup ─────────────────────────────────────────────
  // HW-BUG-2: pull GPIO0 HIGH before LoRa init
  pinMode(LORA_DIO0, INPUT_PULLUP);
  // HW-BUG-1: button on GPIO3, LED on GPIO2
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);  // HIGH = off on NodeMCU

  // ── I2C + OLED ────────────────────────────────────────────
  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.setClock(400000);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println(F("[OLED] FAILED — check wiring / address"));
    oledOk = false;
  } else {
    oledOk = true;
    display.setTextColor(WHITE);
    display.cp437(true);
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("RESCUE NODE");
    display.println("Starting...");
    display.display();
    Serial.println(F("[OLED] OK"));
  }

  // ── LoRa ──────────────────────────────────────────────────
  pinMode(LORA_RST, OUTPUT);
  digitalWrite(LORA_RST, LOW);  delay(10);
  digitalWrite(LORA_RST, HIGH); delay(10);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  loraOk = false;

  for (int i = 1; i <= 3; i++) {
    Serial.printf("[LoRa] Init attempt %d/3...\n", i);
    if (LoRa.begin(LORA_FREQ)) { loraOk = true; break; }
    delay(500);
  }

  if (!loraOk) {
    Serial.println(F("[LoRa] FAILED — check wiring"));
    if (oledOk) {
      display.clearDisplay();
      display.setCursor(0, 0);
      display.println("LoRa FAILED");
      display.println("Check wiring");
      display.display();
    }
    while (true) { ledFlash(3); delay(2000); }
  }

  // Must match TX settings exactly
  LoRa.setSpreadingFactor(9);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.setTxPower(17);
  LoRa.enableCrc();
  Serial.println(F("[LoRa] OK — SF9 BW125 CR4/5"));

  ledFlash(2);

  Serial.println(F("=== Ready — listening ===\n"));
  if (oledOk) oledIdle();
}

// ══════════════════════════════════════════════════════════════
//  LOOP
// ══════════════════════════════════════════════════════════════
void loop() {
  unsigned long ms = millis();

  // ── 1. Receive LoRa ───────────────────────────────────────
  int pktSize = LoRa.parsePacket();
  if (pktSize > 0) {
    String raw = "";
    while (LoRa.available()) raw += (char)LoRa.read();
    int rssi = LoRa.packetRssi();

    Serial.println("[RX] " + raw + "  RSSI:" + String(rssi));

    // Ignore our own ACK echoes
    if (raw.startsWith("__ACK__:")) goto skip_rx;

    // Ignore location-only packets (no display needed on RX)
    if (raw.indexOf("__LOC__:") != -1 && raw.indexOf("__SOS__:") == -1)
      goto skip_rx;

    {
      MsgEntry entry = parsePacket(raw, rssi);

      // Deduplicate
      if (entry.msgId.length() && isDuplicate(entry.msgId)) {
        Serial.println("[RX] Duplicate — dropped");
        goto skip_rx;
      }
      if (entry.msgId.length()) {
        seenIds.push_back(entry.msgId);
        if (seenIds.size() > MAX_SEEN_IDS) seenIds.pop_front();
      }

      // SOS goes to front of queue
      if (entry.isSOS) {
        size_t pos = 0;
        while (pos < msgQueue.size() && msgQueue[pos].isSOS) pos++;
        msgQueue.insert(msgQueue.begin() + pos, entry);
      } else {
        msgQueue.push_back(entry);
      }
      if (msgQueue.size() > MAX_QUEUE) msgQueue.pop_back();

      Serial.printf("[RX] Queued — size:%u  SOS:%s\n",
                    (unsigned)msgQueue.size(),
                    entry.isSOS ? "YES" : "no");

      if (oledOk) oledPopup();
      ledFlash(entry.isSOS ? 3 : 1);
    }
  }
  skip_rx:;

  // ── 2. Button press → read + ACK ──────────────────────────
  if (!msgQueue.empty() && digitalRead(BUTTON_PIN) == LOW) {
    delay(40);  // debounce
    if (digitalRead(BUTTON_PIN) == LOW) {

      MsgEntry current = msgQueue.front();
      msgQueue.pop_front();

      if (oledOk) oledShowMessage(current);
      delay(READ_TIME_MS);

      sendAck(current.msgId);

      if (oledOk) oledAckSent((uint8_t)msgQueue.size());
      delay(1200);

      // Wait for release (max 5s safety)
      unsigned long t = millis();
      while (digitalRead(BUTTON_PIN) == LOW && millis() - t < 5000) delay(10);

      if (!msgQueue.empty()) { if (oledOk) oledPopup(); }
      else                   { if (oledOk) oledIdle();  }
    }
  }

  // ── 3. Blink OLED while messages pending ──────────────────
  if (!msgQueue.empty() && oledOk) {
    unsigned long interval = msgQueue.front().isSOS ? BLINK_SOS_MS : BLINK_NORMAL_MS;
    if (ms - lastBlinkMs >= interval) {
      lastBlinkMs = ms;
      blinkState  = !blinkState;
      if (blinkState) oledPopup();
      else { display.clearDisplay(); display.display(); }
    }
  }
}

// ══════════════════════════════════════════════════════════════
//  PACKET PARSER
//  TX minimal format: "MSGID:XXXXXXXX <text> [GPS:lat,lng]"
// ══════════════════════════════════════════════════════════════
MsgEntry parsePacket(const String &raw, int rssi) {
  MsgEntry m;
  m.rssi   = rssi;
  m.isSOS  = false;
  m.hasGps = false;
  m.gpsLat = 0;
  m.gpsLng = 0;

  String work = raw;

  // Extract MSGID
  if (work.startsWith("MSGID:") && work.length() >= 15) {
    m.msgId = work.substring(6, 14);
    work    = work.substring(15);
  }

  // Extract GPS tag: "[GPS:lat,lng]"
  int gStart = work.indexOf("[GPS:");
  if (gStart != -1) {
    int gEnd = work.indexOf(']', gStart);
    if (gEnd != -1) {
      String tag   = work.substring(gStart + 5, gEnd);
      int    comma = tag.indexOf(',');
      if (comma != -1 && tag != "NO_FIX") {
        m.gpsLat = tag.substring(0, comma).toFloat();
        m.gpsLng = tag.substring(comma + 1).toFloat();
        m.hasGps = true;
      }
      work = work.substring(0, gStart) + work.substring(gEnd + 1);
      work.trim();
    }
  }

  // Classify
  if (work.startsWith("__SOS__:")) {
    m.isSOS = true;
    m.text  = work.substring(8);
  } else if (work.startsWith("__LOC__:")) {
    m.text = "(Location shared)";
  } else {
    m.text = work;
  }
  m.text.trim();

  return m;
}

// ══════════════════════════════════════════════════════════════
//  DEDUPLICATION
// ══════════════════════════════════════════════════════════════
bool isDuplicate(const String &id) {
  for (size_t i = 0; i < seenIds.size(); i++)
    if (seenIds[i] == id) return true;
  return false;
}

// ══════════════════════════════════════════════════════════════
//  SEND ACK
// ══════════════════════════════════════════════════════════════
void sendAck(const String &msgId) {
  if (!msgId.length()) { Serial.println(F("[ACK] No MSGID — skipped")); return; }
  String ack = "__ACK__:" + msgId;
  Serial.println("[ACK] Sending: " + ack);
  delay(30);
  if (!LoRa.beginPacket()) { Serial.println(F("[ACK] beginPacket failed")); return; }
  LoRa.print(ack);
  if (LoRa.endPacket() == 1) {
    ackCount++;
    Serial.printf("[ACK] OK — total: %u\n", ackCount);
    ledFlash(2);
  } else {
    Serial.println(F("[ACK] FAILED"));
    ledFlash(5);
  }
}

// ══════════════════════════════════════════════════════════════
//  LED
// ══════════════════════════════════════════════════════════════
void ledFlash(uint8_t times) {
  for (uint8_t i = 0; i < times; i++) {
    digitalWrite(LED_PIN, LOW);   // LOW = on for NodeMCU
    delay(80);
    digitalWrite(LED_PIN, HIGH);
    if (i < times - 1) delay(80);
  }
}

// ══════════════════════════════════════════════════════════════
//  OLED SCREENS
// ══════════════════════════════════════════════════════════════

// Idle — nothing in queue
void oledIdle() {
  display.clearDisplay();
  display.setTextSize(1);
  // Header bar
  display.fillRect(0, 0, 128, 11, WHITE);
  display.setTextColor(BLACK);
  display.setCursor(2, 2);
  display.print("RESCUE NODE  READY");
  display.setTextColor(WHITE);
  // Body
  display.setCursor(0, 16);
  display.println("Listening...");
  display.setCursor(0, 28);
  display.print("ACK sent: "); display.print(ackCount);
  display.setCursor(0, 54);
  display.print("BTN = read & ACK");
  display.display();
}

// Popup — new message(s) waiting
void oledPopup() {
  if (msgQueue.empty()) return;

  bool   hasSOS = msgQueue.front().isSOS;
  size_t count  = msgQueue.size();

  display.clearDisplay();
  display.setTextSize(1);

  if (hasSOS) {
    // Bold SOS alert — invert header
    display.fillRect(0, 0, 128, 11, WHITE);
    display.setTextColor(BLACK);
    display.setCursor(2, 2);
    display.print("!!! SOS RECEIVED !!!");
    display.setTextColor(WHITE);
    display.setCursor(0, 16);
    display.print("Queue: "); display.print(count);
    display.setCursor(0, 36);
    String preview = msgQueue.front().text;
    if (preview.length() > 20) preview = preview.substring(0, 19) + "~";
    display.print(preview);
    display.setCursor(0, 54);
    display.print("Press BTN to ACK");
  } else {
    display.fillRect(0, 0, 128, 11, WHITE);
    display.setTextColor(BLACK);
    display.setCursor(2, 2);
    display.print("NEW MESSAGE");
    display.setTextColor(WHITE);
    display.setCursor(0, 16);
    display.print("Queue: "); display.print(count);
    display.setCursor(0, 28);
    String preview = msgQueue.front().text;
    if (preview.length() > 20) preview = preview.substring(0, 19) + "~";
    display.print(preview);
    display.setCursor(0, 40);
    display.print("RSSI: "); display.print(msgQueue.front().rssi); display.print(" dBm");
    display.setCursor(0, 54);
    display.print("Press BTN to ACK");
  }
  display.display();
}

// Full message view — shown while user reads
void oledShowMessage(const MsgEntry &m) {
  display.clearDisplay();
  display.setTextSize(1);

  // Header
  display.fillRect(0, 0, 128, 11, WHITE);
  display.setTextColor(BLACK);
  display.setCursor(2, 2);
  if (m.isSOS) display.print("!!! SOS !!!");
  else         display.print("MESSAGE");
  display.print("  "); display.print(m.rssi); display.print("dBm");
  display.setTextColor(WHITE);

  // Message body — up to 3 lines of 21 chars
  String body = m.text;
  for (int line = 0; line < 3 && body.length(); line++) {
    int    take  = min((int)body.length(), 21);
    String chunk = body.substring(0, take);
    display.setCursor(0, 14 + line * 10);
    display.print(chunk);
    body = body.substring(take);
    body.trim();
  }

  // GPS coords if available
  if (m.hasGps) {
    display.setCursor(0, 46);
    display.print("LAT "); display.print(m.gpsLat, 5);
    display.setCursor(0, 56);
    display.print("LNG "); display.print(m.gpsLng, 5);
  }

  display.display();
}

// Confirmation after ACK sent
void oledAckSent(uint8_t remaining) {
  display.clearDisplay();
  display.setTextSize(1);

  // Simple checkmark (3 thick lines)
  for (int t = -1; t <= 1; t++) {
    display.drawLine(8,  38+t, 22, 52+t, WHITE);
    display.drawLine(22, 52+t, 50, 18+t, WHITE);
  }

  display.setCursor(58, 16); display.println("ACK SENT");
  display.setCursor(58, 30); display.print("Left: "); display.print(remaining);
  if (remaining > 0) { display.setCursor(0, 56); display.print("Press BTN for next"); }

  display.display();
}
