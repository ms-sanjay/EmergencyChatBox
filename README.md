# Emergency Chat Box using ESP8266

This project is an emergency communication system that works without internet. It uses two ESP8266 modules — one as a sender (host) and another as a receiver — to send and display messages in real time using WebSockets. It is useful in situations like natural disasters, remote areas, or airplane mode, where regular communication is not possible.

## Components Used
- ESP8266 (NodeMCU) – Used for both Sender & Receiver
- OLED Display (SSD1306, 128x64) – For displaying messages
- Push Button – To acknowledge the messages
- VS code(Platform IO) – For programming
- HTML + JavaScript – For web interface
- WebSockets – For real-time message exchange

## How it Works
- The Sender ESP8266 creates a Wi-Fi hotspot named `Emergency_Network`.
- Users can connect to this network (even in airplane mode).
- A captive portal opens where users can type and send a message.
- The message is transmitted to the Receiver via WebSocket.
- The Receiver shows an alert on the OLED screen for each message.
- When the push button is pressed, the message is acknowledged and displayed.

Queued messages are handled in order, so each message is read and confirmed without overwriting.

## Project Documentation

For a complete explanation of the working, components, and screenshots:

[project_report(PDF)](project_report.pdf)

## Files Included
- `senderESP` – Code for Sender ESP8266 (AP + Web Server)
- `receiverESP` – Code for Receiver ESP8266 (OLED + Button)
- `Chat_interface.html` – Web interface served by the sender
- `project_report.pdf` – Full documentation with images
- `working result` – Working result


