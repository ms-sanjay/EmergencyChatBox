#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WebSocketsServer.h>
#include <DNSServer.h>

// Wi-Fi Access Point SSID
const char* apSSID = "Emergency_Network";

// Set up servers
ESP8266WebServer httpServer(80);     // For hosting the captive portal page
WebSocketsServer webSocket(81);      // For real-time communication
DNSServer dnsServer;                 // DNS server to handle captive portal redirection

// HTML page served as captive portal
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Emergency Chat</title>
  <style>
    /* Basic styling for chat UI */
    * { margin: 0; padding: 0; box-sizing: border-box; font-family: Arial, sans-serif; }
    body { display: flex; justify-content: center; align-items: center; height: 100vh; background: #f4f4f4; }
    .chat-container { width: 350px; background: #fff; border-radius: 10px; box-shadow: 0 4px 8px rgba(0,0,0,0.2); overflow: hidden; }
    .chat-header { background: #007bff; color: white; padding: 15px; text-align: center; font-size: 1.2em; font-weight: bold; }
    #messages { height: 350px; overflow-y: auto; padding: 10px; background: #f9f9f9; display: flex; flex-direction: column; }
    .message { max-width: 80%; padding: 10px; margin: 5px; border-radius: 10px; font-size: 0.9em; }
    .sent { background: #007bff; color: white; align-self: flex-end; }
    .received { background: #ddd; color: black; align-self: flex-start; }
    .chat-footer { display: flex; padding: 10px; background: white; border-top: 1px solid #ccc; }
    input { flex: 1; padding: 10px; border: 1px solid #ccc; border-radius: 5px; }
    button { padding: 10px; border: none; border-radius: 5px; background: #007bff; color: white; cursor: pointer; margin-left: 5px; }
    .sos-button { background: #ff0000; }
  </style>
</head>
<body>
  <div class="chat-container">
    <div class="chat-header">&#x1F6A8; Emergency Chat</div>
    <div id="messages"></div>
    <div class="chat-footer">
      <input id="msg" type="text" placeholder="Type a message..." autofocus>
      <button onclick="sendMessage()">Send</button>
      <button class="sos-button" onclick="sendEmergencyMessage()">SOS</button>
    </div>
  </div>

  <script>
    // Connect to WebSocket server hosted on the same ESP
    var ws = new WebSocket('ws://' + window.location.hostname + ':81/');

    // When a message is received from receiver ESP
    ws.onmessage = function(event) {
      addMessageToChat(event.data, "received");
    };

    // Send user-typed message
    function sendMessage() {
      var input = document.getElementById("msg");
      var message = input.value.trim();
      if (message !== "") {
        ws.send(message);
        addMessageToChat(message, "sent");
        input.value = "";
      }
    }

    // Send a pre-defined emergency message
    function sendEmergencyMessage() {
      var emergencyMsg = "\u{1F6A8} SOS! Need help!";
      ws.send(emergencyMsg);
      addMessageToChat(emergencyMsg, "sent");
    }

    // Display message in chat window
    function addMessageToChat(msg, type) {
      var messages = document.getElementById("messages");
      var msgDiv = document.createElement("div");
      msgDiv.classList.add("message", type);
      msgDiv.innerHTML = msg;
      messages.appendChild(msgDiv);
      messages.scrollTop = messages.scrollHeight;
    }
  </script>
</body>
</html>
)rawliteral";

// Redirects all unknown requests to main page (captive portal behavior)
void captivePortal() {
  httpServer.sendHeader("Location", "http://192.168.4.1/", true);
  httpServer.send(302, "text/html", "<html><body><h1>Redirecting...</h1></body></html>");
}

// Serve main chat page
void handleRoot() {
  httpServer.send(200, "text/html", index_html);
}

// Catch-all fallback handler
void handleNotFound() {
  httpServer.send(200, "text/html", "<html><body><h1>Emergency Portal Active</h1></body></html>");
}

// Handle WebSocket events
void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      Serial.printf("Client [%u] connected\n", num);
      break;
    case WStype_DISCONNECTED:
      Serial.printf("Client [%u] disconnected\n", num);
      break;
    case WStype_TEXT:
      Serial.printf("Message: %s\n", payload);
      // Broadcast incoming message to all connected clients (receiver)
      webSocket.broadcastTXT((char*)payload);
      break;
    default:
      break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);

  // Start Access Point
  WiFi.softAP(apSSID);
  IPAddress apIP = WiFi.softAPIP();
  Serial.print("AP IP: ");
  Serial.println(apIP);

  // Start DNS server to redirect all URLs to this ESP
  dnsServer.start(53, "*", apIP);

  // HTTP routes
  httpServer.on("/", handleRoot);
  httpServer.on("/generate_204", captivePortal);        // Android
  httpServer.on("/hotspot-detect.html", captivePortal); // iOS
  httpServer.on("/fwlink", captivePortal);              // Windows
  httpServer.on("/ncsi.txt", captivePortal);            // Windows
  httpServer.onNotFound(handleNotFound);

  // Start web server and WebSocket
  httpServer.begin();
  webSocket.begin();
  webSocket.onEvent(onWebSocketEvent);

  Serial.println("HTTP and WebSocket servers started.");
}

void loop() {
  dnsServer.processNextRequest();   // Handle DNS redirection
  httpServer.handleClient();        // Handle captive portal requests
  webSocket.loop();                 // Handle WebSocket messages
}
