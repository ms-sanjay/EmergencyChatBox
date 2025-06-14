#include <ESP8266WiFi.h>
#include <WebSocketsClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <deque>  // Used for queuing incoming messages

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Wi-Fi credentials
const char* ssid = "Emergency_Network";
const char* websocket_host = "192.168.4.1";  // Sender IP
const uint16_t websocket_port = 81;

// WebSocket client instance
WebSocketsClient webSocket;

// Pin definitions
const int LED_PIN = LED_BUILTIN;
const int BUTTON_PIN = 3;  // Push button for acknowledgment

bool isAcknowledging = false;
String lastAcknowledgedMessage = "";
std::deque<String> messageQueue;  // Queue to store incoming messages

// Function declarations
void webSocketEvent(WStype_t type, uint8_t *payload, size_t length);
void displayMessage(String message);
void drawWarningSymbol();
void drawTick();
void processNextMessage();

void setup() {
  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  digitalWrite(LED_PIN, HIGH);

  // Initialize OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED failed to start");
    while (true);  // Halt here
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 10);
  display.println("Starting...");
  display.display();
  delay(2000);

  // Connect to Wi-Fi
  Serial.print("Connecting to ");
  Serial.print(ssid);
  WiFi.begin(ssid);

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }

  Serial.println("\nConnected");
  Serial.print("Receiver IP: ");
  Serial.println(WiFi.localIP());

  displayMessage("Wi-Fi OK!");

  // Set up WebSocket
  webSocket.begin(websocket_host, websocket_port, "/");
  webSocket.onEvent(webSocketEvent);
}

void loop() {
  webSocket.loop();

  // Blink warning if message is waiting
  if (!messageQueue.empty()) {
    display.clearDisplay();
    drawWarningSymbol();
    display.display();
    delay(500);
    display.clearDisplay();
    display.display();
    delay(500);
  }

  // Acknowledge when button is pressed
  if (!messageQueue.empty() && digitalRead(BUTTON_PIN) == LOW) {
    processNextMessage();
  }
}

// Handle and acknowledge the next message in the queue
void processNextMessage() {
  if (messageQueue.empty()) return;

  isAcknowledging = true;
  String currentMessage = messageQueue.front();
  messageQueue.pop_front();

  lastAcknowledgedMessage = currentMessage;

  display.clearDisplay();
  displayMessage(currentMessage);
  drawTick();
  display.display();

  // Send ACK back to sender
  String ackMessage = "Acknowledged: " + currentMessage;
  webSocket.sendTXT(ackMessage);

  delay(2000);

  // Show next if available, else idle screen
  if (!messageQueue.empty()) {
    processNextMessage();
  } else {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(0, 0);
    display.println("Previous message:");
    display.println(lastAcknowledgedMessage);
    display.println("");
    display.println("Waiting for new");
    display.println("messages...");
    display.display();
    isAcknowledging = false;
  }
}

// Handles incoming WebSocket messages
void webSocketEvent(WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      Serial.println("Connected to sender");
      displayMessage("Connected!");
      break;

    case WStype_DISCONNECTED:
      Serial.println("Disconnected from sender");
      displayMessage("Disconnected!");
      break;

    case WStype_TEXT:
      String receivedMessage = String((char*)payload);
      Serial.print("Received: ");
      Serial.println(receivedMessage);

      if (receivedMessage.startsWith("Acknowledged:")) {
        Serial.println("Skipping acknowledgment message");
        return;
      }

      // Remove warning symbol prefix
      if (receivedMessage.startsWith("\u1F6A8")) {
        receivedMessage.remove(0, 4);
      }

      messageQueue.push_back(receivedMessage);
      break;

    default:
      break;
  }
}

// Utility: Display simple text on OLED
void displayMessage(String message) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println(message);
  display.display();
}

// Utility: Draw blinking warning triangle
void drawWarningSymbol() {
  display.drawTriangle(64, 20, 44, 50, 84, 50, WHITE);
  display.fillRect(62, 30, 4, 12, WHITE);
  display.fillRect(62, 45, 4, 4, WHITE);
}

// Utility: Draw tick mark for acknowledgment
void drawTick() {
  int midX = 64, midY = 32;
  int tickStartX = midX - 20, tickStartY = midY + 10;
  int tickMidX = midX - 5, tickMidY = midY + 20;
  int tickEndX = midX + 20, tickEndY = midY - 15;

  for (int i = 0; i < 3; i++) {
    display.drawLine(tickStartX + i, tickStartY, tickMidX + i, tickMidY, WHITE);
    display.drawLine(tickMidX + i, tickMidY, tickEndX + i, tickEndY, WHITE);
  }
}
