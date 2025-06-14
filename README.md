# Emergency Chat Box using ESP8266

A Wi-Fi-based offline communication system using two ESP8266 modules. Enables emergency message transmission and acknowledgment without internet — ideal for disaster zones and remote areas.

---

## Components
- ESP8266 (NodeMCU) — Sender & Receiver
- OLED Display (SSD1306, 128x64)
- Push Button — For acknowledgment

---

##  Working

### Sender ESP8266
- Creates a local Access Point (`Emergency_Network`)
- Hosts a captive portal (auto-opens on connection)
- Accepts messages via Web UI
- Sends messages via WebSocket
- Waits for acknowledgment and updates UI

### Receiver ESP8266
- Connects to the Sender’s AP
- Receives messages and queues them
- Blinks a warning symbol until acknowledged
- Displays the message and sends back acknowledgment
- Shows messages one by one

---

##  Features
-  Works without internet (Airplane Mode supported)
-  OLED visual alert + push button acknowledgment
-  Reliable message queue system
-  Real-time two-way WebSocket communication

---
