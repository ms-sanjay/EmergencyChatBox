#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <deque>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ---------- Pin Configurations ----------
#define LORA_SS    D8
#define LORA_RST   D0
#define LORA_DIO0  D3
#define BUTTON_PIN 3
#define LED_PIN    LED_BUILTIN

#define LORA_BAND 433E6

std::deque<String> messageQueue;
bool isAcknowledging = false;
String lastAcknowledgedMessage = "";
unsigned long lastBlinkTime = 0;
bool blinkState = false;
int missedPackets = 0;

void setup() {
  Serial.begin(115200);
  delay(100);

  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // OLED Init
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED init failed!");
    while (true);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 10);
  display.println("LoRa Receiver...");
  display.display();

  // LoRa Init
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(LORA_BAND)) {
    Serial.println("LoRa init failed!");
    while (true);
  }
  LoRa.enableCrc();
  Serial.println("✅ LoRa Ready with CRC");
  displayMessage("LoRa Ready with CRC");
}

void loop() {
  // Fast receive check
  receiveLoRaMessage();

  // Blink warning without blocking
  if (!messageQueue.empty() && !isAcknowledging) {
    if (millis() - lastBlinkTime > 400) {
      blinkState = !blinkState;
      display.clearDisplay();
      if (blinkState) drawWarningSymbol();
      display.display();
      lastBlinkTime = millis();
    }
  }

  // Button pressed = process one message
  if (!messageQueue.empty() && digitalRead(BUTTON_PIN) == LOW) {
    processNextMessage();
    delay(300);
  }
}

void receiveLoRaMessage() {
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    String received = "";
    while (LoRa.available()) {
      received += (char)LoRa.read();
    }

    if (received.startsWith("Acknowledged:")) return; // ignore ACKs
    if (received.startsWith("\u1F6A8")) received.remove(0, 4);

    messageQueue.push_back(received);
    Serial.print("📩 Received: "); Serial.println(received);
    Serial.print("🗂 Queue size: "); Serial.println(messageQueue.size());

    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("New Msg Added!");
    display.print("Queue: ");
    display.println(messageQueue.size());
    display.display();
  }
}

void processNextMessage() {
  if (messageQueue.empty()) return;

  isAcknowledging = true;
  String current = messageQueue.front();
  messageQueue.pop_front();

  lastAcknowledgedMessage = current;
  displayMessage(current);

  // Send ACK
  String ackMsg = "Acknowledged: " + current;
  LoRa.beginPacket();
  LoRa.print(ackMsg);
  LoRa.endPacket();
  Serial.println("✅ Sent ACK: " + ackMsg);

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("ACK Sent!");
  display.println(current);
  display.print("Pending: ");
  display.println(messageQueue.size());
  display.display();

  delay(800); // small delay to avoid congestion
  isAcknowledging = false;
}

void displayMessage(String msg) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Message:");
  display.println(msg);
  display.display();
}

void drawWarningSymbol() {
  display.drawTriangle(64, 20, 44, 50, 84, 50, WHITE);
  display.fillRect(62, 30, 4, 12, WHITE);
  display.fillRect(62, 45, 4, 4, WHITE);
}
