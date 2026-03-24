/*
 *  WIRING
 * ─────────────────────────────────────────────────────────────
 *  ESP8266 NodeMCU v2  │  Peripheral   │  Notes
 * ─────────────────────┼───────────────┼────────────────────
 *  3.3V / GND          │  LoRa VCC/GND │
 *  D8  (GPIO15)        │  LoRa NSS     │  SPI Chip-Select
 *  D5  (GPIO14)        │  LoRa SCK     │
 *  D6  (GPIO12)        │  LoRa MISO    │
 *  D7  (GPIO13)        │  LoRa MOSI    │
 *  D0  (GPIO16)        │  LoRa RESET   │
 *  D2  (GPIO4)         │  LoRa DIO0    │  IRQ
 * ─────────────────────┼───────────────┼────────────────────
 *  3.3V / GND          │  NEO-6M VCC   │
 *  D1  (GPIO5)         │  NEO-6M TX →  │  → ESP RX
 *  D3  (GPIO0)         │  NEO-6M RX ←  │  ← ESP TX
 * ─────────────────────┴───────────────┴────────────────────
 *
 */

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncWebServer.h>
#include <WebSocketsServer.h>
#include <DNSServer.h>
#include <SPI.h>
#include <LoRa.h>
#include <SoftwareSerial.h>
#include <TinyGPSPlus.h>

// ── Pins ──────────────────────────────────────────────────────
#define LORA_SS    15
#define LORA_RST   16
#define LORA_DIO0   4
#define GPS_RX_PIN  5
#define GPS_TX_PIN  0
#define GPS_BAUD 9600
#define LORA_FREQ  433E6

// ── Globals ───────────────────────────────────────────────────
const char* AP_SSID = "Emergency_Network";

AsyncWebServer   httpServer(80);
WebSocketsServer webSocket(81);
DNSServer        dnsServer;
SoftwareSerial   gpsSerial(GPS_RX_PIN, GPS_TX_PIN);
TinyGPSPlus      gps;

struct GpsData {
  bool   valid   = false;
  double lat     = 0.0;
  double lng     = 0.0;
  int    sats    = 0;
  int    hdop    = 9999;
  String timeUTC = "--:--:--";
} gpsData;

bool          loraReady        = false;
unsigned long loraTxCount      = 0;
unsigned long loraTxFailCount  = 0;
unsigned long loraRxCount      = 0;
unsigned long lastGpsBroadcast = 0;
const unsigned long GPS_BROADCAST_MS = 3000;

// ── HTML ──────────────────────────────────────────────────────
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width,initial-scale=1"/>
<title>Emergency Chat Box</title>
<style>
  @import url('https://fonts.googleapis.com/css2?family=Share+Tech+Mono&family=Rajdhani:wght@400;600;700&display=swap');

  :root {
    --bg:      #0a0e12;
    --surface: #111720;
    --border:  #1e3040;
    --accent:  #00e5ff;
    --warn:    #ff3b3b;
    --ok:      #00e676;
    --dim:     #2d4558;
    --text:    #cce8f4;
    --sub:     #4a7080;
    --mono:    'Share Tech Mono', monospace;
    --sans:    'Rajdhani', sans-serif;
    --r:       5px;
  }

  *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }

  html, body {
    height: 100%;
    background: var(--bg);
    color: var(--text);
    font-family: var(--mono);
    font-size: 13px;
    overflow: hidden;
  }

  .app {
    display: flex;
    flex-direction: column;
    height: 100vh;
    max-width: 600px;
    margin: 0 auto;
    border-left: 1px solid var(--border);
    border-right: 1px solid var(--border);
  }

  /* ── Header ── */
  .header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 10px 16px;
    background: var(--surface);
    border-bottom: 1px solid var(--accent);
    flex-shrink: 0;
  }
  .header-title {
    font-family: var(--sans);
    font-size: 1.1rem;
    font-weight: 700;
    letter-spacing: .2em;
    color: var(--accent);
    text-transform: uppercase;
  }
  .header-title span { color: var(--warn); margin-right: 6px; animation: blink 2s ease-in-out infinite; }
  @keyframes blink { 0%,100%{opacity:1} 50%{opacity:.3} }

  .ws-status { display: flex; align-items: center; gap: 6px; font-size: .65rem; color: var(--sub); }
  .ws-dot { width: 7px; height: 7px; border-radius: 50%; background: var(--dim); transition: all .4s; }
  .ws-dot.live { background: var(--ok); box-shadow: 0 0 6px var(--ok); }

  /* ── GPS Bar ── */
  .gps-bar {
    display: flex;
    align-items: center;
    gap: 10px;
    padding: 7px 16px;
    background: var(--surface);
    border-bottom: 1px solid var(--border);
    flex-shrink: 0;
    flex-wrap: wrap;
  }
  .gps-pill {
    display: inline-flex;
    align-items: center;
    gap: 4px;
    padding: 2px 9px;
    border-radius: 20px;
    font-size: .63rem;
    border: 1px solid var(--border);
    color: var(--sub);
    transition: all .4s;
    font-family: var(--mono);
  }
  .gps-pill.ok  { border-color: var(--ok);  color: var(--ok); }
  .gps-pill.bad { border-color: var(--warn); color: var(--warn); }
  .gps-dot { width: 6px; height: 6px; border-radius: 50%; background: currentColor; }
  .gps-field { font-size: .65rem; color: var(--sub); }
  .gps-field b { color: var(--text); font-weight: normal; }
  .gps-sep { color: var(--dim); }

  /* ── Messages ── */
  .messages {
    flex: 1;
    overflow-y: auto;
    padding: 14px 14px 8px;
    display: flex;
    flex-direction: column;
    gap: 7px;
    scroll-behavior: smooth;
  }
  .messages::-webkit-scrollbar { width: 3px; }
  .messages::-webkit-scrollbar-thumb { background: var(--border); }

  /* ── Bubbles ── */
  .msg {
    max-width: 80%;
    padding: 8px 12px;
    border-radius: var(--r);
    font-size: .8rem;
    line-height: 1.5;
    word-break: break-word;
  }
  .msg .ts { display: block; font-size: .58rem; color: var(--sub); margin-bottom: 3px; }
  .sent  { align-self: flex-end;   background: #002233; border: 1px solid rgba(0,229,255,.25); color: #d0f0ff; }
  .recv  { align-self: flex-start; background: var(--surface); border: 1px solid var(--border); }
  .sys   {
    align-self: center; background: transparent; border: 1px solid var(--border);
    color: var(--sub); font-size: .62rem; padding: 3px 12px; border-radius: 20px;
    max-width: 100%; text-align: center;
  }
  .sos-bubble {
    align-self: flex-start;
    background: #1a0000; border: 1px solid var(--warn); color: #ffcccc;
    animation: sosPulse 1s ease-in-out infinite alternate;
  }
  .sos-bubble.sent { align-self: flex-end; }
  @keyframes sosPulse { from{box-shadow:0 0 2px var(--warn)} to{box-shadow:0 0 14px var(--warn)} }
  .ack-tick { font-size: .6rem; color: var(--ok); margin-left: 6px; }

  /* ── Input Bar ── */
  .input-bar {
    display: flex; align-items: center; gap: 7px;
    padding: 8px 12px; background: var(--surface);
    border-top: 1px solid var(--border); flex-shrink: 0;
  }
  .msg-input {
    flex: 1; background: var(--bg); border: 1px solid var(--border);
    color: var(--text); padding: 9px 12px; border-radius: var(--r);
    font-family: var(--mono); font-size: .8rem; outline: none; transition: border-color .2s;
  }
  .msg-input:focus { border-color: var(--accent); }
  .msg-input::placeholder { color: var(--dim); }
  .btn {
    padding: 9px 13px; border: none; border-radius: var(--r);
    font-family: var(--mono); font-size: .68rem; letter-spacing: .07em;
    text-transform: uppercase; cursor: pointer; transition: all .15s; white-space: nowrap;
  }
  .btn-send { background: var(--accent); color: #000; font-weight: 700; }
  .btn-send:hover { filter: brightness(1.15); }
  .btn-sos { background: #400000; color: #fff; border: 1px solid var(--warn); animation: sosBlink 2.5s ease-in-out infinite; }
  @keyframes sosBlink { 0%,100%{box-shadow:none} 50%{box-shadow:0 0 10px var(--warn)} }
  .btn-sos:hover { background: #700000; }

  /* ── Location bubble ── */
  .loc-bubble { align-self: flex-start; background: #001a0a; border: 1px solid rgba(0,230,118,.3); color: #b2ffd6; max-width: 90%; }
  .loc-bubble.sent { align-self: flex-end; }
  .loc-coords { font-size: .75rem; color: var(--ok); margin-top: 4px; }
  .copy-btn {
    margin-top: 6px; padding: 3px 10px;
    background: rgba(0,230,118,.08); border: 1px solid rgba(0,230,118,.25);
    color: var(--ok); font-size: .62rem; font-family: var(--mono);
    border-radius: 3px; cursor: pointer; letter-spacing: .06em;
  }
  .copy-btn:hover { background: rgba(0,230,118,.18); }

  /* ── Share button ── */
  .share-btn {
    width: calc(100% - 24px); margin: 0 12px 10px; padding: 8px;
    background: #001a0a; border: 1px solid rgba(0,230,118,.3); color: var(--ok);
    font-family: var(--mono); font-size: .68rem; letter-spacing: .1em;
    text-transform: uppercase; border-radius: var(--r); cursor: pointer;
    transition: all .2s; flex-shrink: 0;
  }
  .share-btn:hover { background: #002a14; }
  .share-btn:disabled { opacity: .35; cursor: not-allowed; }
</style>
</head>
<body>
<div class="app">

  <header class="header">
    <div class="header-title"><span>&#9888;</span>EMERGENCY CHAT BOX</div>
    <div class="ws-status">
      <div class="ws-dot" id="wsDot"></div>
      <span id="wsLabel">connecting</span>
    </div>
  </header>

  <div class="gps-bar">
    <span class="gps-pill bad" id="gpsPill">
      <span class="gps-dot"></span>
      <span id="gpsStatus">NO FIX</span>
    </span>
    <span class="gps-field" id="gpsCoords" style="display:none">
      <b id="gLat">—</b> <span class="gps-sep">/</span> <b id="gLng">—</b>
    </span>
    <span class="gps-field" id="gpsTime" style="display:none">
      &#128336; <b id="gTime">—</b>
    </span>
    <span class="gps-field" id="gpsSatsEl" style="display:none">
      <b id="gSats">0</b> sats
    </span>
  </div>

  <div class="messages" id="messages"></div>

  <button class="share-btn" id="shareBtn" onclick="shareLocation()" disabled>
    &#128205; SHARE MY LOCATION
  </button>

  <div class="input-bar">
    <input class="msg-input" id="msgIn" type="text" maxlength="180"
           placeholder="Type a message…" autocomplete="off"/>
    <button class="btn btn-send" onclick="sendMsg()">SEND</button>
    <button class="btn btn-sos"  onclick="sendSOS()">SOS</button>
  </div>

</div>
<script>
var myGps = { valid:false, lat:0, lng:0, sats:0, hdop:9999, time:'--:--:--' };
var pendingMsgs = {};

var ws, reconnTimer;
function connect() {
  ws = new WebSocket('ws://' + window.location.hostname + ':81/');
  ws.onopen    = wsOpen;
  ws.onclose   = wsClose;
  ws.onerror   = function() { ws.close(); };
  ws.onmessage = wsMsg;
}
function wsOpen() {
  clearTimeout(reconnTimer);
  document.getElementById('wsDot').classList.add('live');
  document.getElementById('wsLabel').textContent = 'connected';
  sys('Connected to mesh node.');
}
function wsClose() {
  document.getElementById('wsDot').classList.remove('live');
  document.getElementById('wsLabel').textContent = 'reconnecting';
  reconnTimer = setTimeout(connect, 3000);
}
function wsMsg(e) {
  var d = e.data;
  if      (d.startsWith('__GPS__:'))      onGps(d.slice(8));
  else if (d.startsWith('__LOC__:'))      onLoc(d.slice(8), 'recv');
  else if (d.startsWith('__SOS__:'))      onSos(d.slice(8), 'recv');
  else if (d.startsWith('__ACK__:'))      onAck(d.slice(8));
  else if (d.startsWith('__TX_MSGID__:')) onTxMsgId(d.slice(13));
  else addBubbleEl(d, 'recv');
}

function onGps(json) {
  try {
    var g = JSON.parse(json);
    myGps = g;
    var pill   = document.getElementById('gpsPill');
    var coords = document.getElementById('gpsCoords');
    var timeEl = document.getElementById('gpsTime');
    var satsEl = document.getElementById('gpsSatsEl');
    if (g.valid) {
      pill.className = 'gps-pill ok';
      document.getElementById('gpsStatus').textContent = 'GPS OK';
      document.getElementById('gLat').textContent  = g.lat.toFixed(5) + '°';
      document.getElementById('gLng').textContent  = g.lng.toFixed(5) + '°';
      document.getElementById('gTime').textContent = g.time;
      document.getElementById('gSats').textContent = g.sats;
      coords.style.display = '';
      timeEl.style.display = '';
      satsEl.style.display = '';
      document.getElementById('shareBtn').disabled = false;
    } else {
      pill.className = 'gps-pill bad';
      document.getElementById('gpsStatus').textContent = 'NO FIX';
      coords.style.display = 'none';
      timeEl.style.display = 'none';
      satsEl.style.display = 'none';
      document.getElementById('shareBtn').disabled = true;
    }
  } catch(e) {}
}

function onLoc(json, side) {
  try {
    var g = JSON.parse(json);
    var coordStr = g.lat.toFixed(6) + ', ' + g.lng.toFixed(6);
    var box = document.getElementById('messages');
    var d   = document.createElement('div');
    d.className = 'msg loc-bubble' + (side === 'sent' ? ' sent' : '');
    d.innerHTML =
      '<span class="ts">' + now() + (side === 'recv' ? ' · REMOTE' : '') + '</span>' +
      '<strong>&#128205; Location</strong>' +
      '<div class="loc-coords">' + coordStr + '</div>' +
      '<button class="copy-btn" onclick="copyCoords(this,\'' + coordStr + '\')">&#128203; COPY</button>';
    box.appendChild(d);
    box.scrollTop = box.scrollHeight;
  } catch(e) { addBubbleEl(json, side); }
}

// FIX: returns DOM element so sendSOS() can mark it pending
function onSos(text, side) {
  var box = document.getElementById('messages');
  var d   = document.createElement('div');
  d.className = 'msg sos-bubble' + (side === 'sent' ? ' sent' : '');
  d.innerHTML = '<span class="ts">' + now() + (side === 'recv' ? ' · REMOTE' : '') + '</span>' + esc(text);
  box.appendChild(d);
  box.scrollTop = box.scrollHeight;
  return d;
}

function onTxMsgId(msgId) {
  var bubbles = document.querySelectorAll('#messages .sent[data-pending="true"]');
  if (bubbles.length) {
    var last = bubbles[bubbles.length - 1];
    last.setAttribute('data-msgid', msgId);
    last.removeAttribute('data-pending');
    pendingMsgs[msgId] = last;
  }
}
function onAck(msgId) {
  var el = pendingMsgs[msgId];
  if (el) {
    var tick = document.createElement('span');
    tick.className   = 'ack-tick';
    tick.textContent = ' ✓';
    el.appendChild(tick);
    delete pendingMsgs[msgId];
  } else {
    sys('✓ ACK: ' + msgId);
  }
}

function sendMsg() {
  var inp = document.getElementById('msgIn');
  var m   = inp.value.trim();
  if (!m || !ws || ws.readyState !== 1) return;
  ws.send(m);
  var el = addBubbleEl(m, 'sent');
  el.setAttribute('data-pending', 'true');
  inp.value = '';
}
function sendSOS() {
  if (!ws || ws.readyState !== 1) return;
  var m = '🚨 SOS! NEED IMMEDIATE HELP!';
  ws.send('__SOS__:' + m);
  // FIX: capture bubble and mark pending — same as sendMsg()
  // so onTxMsgId() links it to its MSGID and onAck() adds ✓
  var el = onSos(m, 'sent');
  el.setAttribute('data-pending', 'true');
}
function shareLocation() {
  if (!myGps.valid) return;
  var payload = JSON.stringify({ lat: myGps.lat, lng: myGps.lng });
  ws.send('__LOC__:' + payload);
  onLoc(payload, 'sent');
}

function addBubbleEl(text, cls) {
  var box = document.getElementById('messages');
  var d   = document.createElement('div');
  d.className = 'msg ' + cls;
  d.innerHTML = '<span class="ts">' + now() + (cls === 'recv' ? ' · REMOTE' : '') + '</span>' + esc(text);
  box.appendChild(d);
  box.scrollTop = box.scrollHeight;
  return d;
}
function sys(txt) {
  var box = document.getElementById('messages');
  var d   = document.createElement('div');
  d.className = 'msg sys';
  d.textContent = txt;
  box.appendChild(d);
  box.scrollTop = box.scrollHeight;
}
function copyCoords(btn, coordStr) {
  function done() { btn.textContent = '✓ COPIED'; }
  if (navigator.clipboard) {
    navigator.clipboard.writeText(coordStr).then(done).catch(function() { fallbackCopy(coordStr); done(); });
  } else { fallbackCopy(coordStr); done(); }
}
function fallbackCopy(text) {
  var el = document.createElement('textarea');
  el.value = text; el.style.position = 'fixed'; el.style.opacity = '0';
  document.body.appendChild(el); el.select();
  try { document.execCommand('copy'); } catch(e) {}
  document.body.removeChild(el);
}
function now() {
  var d = new Date();
  return ('0'+d.getHours()).slice(-2)+':'+('0'+d.getMinutes()).slice(-2)+':'+('0'+d.getSeconds()).slice(-2);
}
function esc(s) {
  return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');
}

document.addEventListener('DOMContentLoaded', function() {
  document.getElementById('msgIn').addEventListener('keydown', function(e) {
    if (e.key === 'Enter') sendMsg();
  });
  connect();
});
</script>
</body>
</html>
)rawliteral";

// ══════════════════════════════════════════════════════════════
//  HELPERS
// ══════════════════════════════════════════════════════════════
String buildGpsJson() {
  String j = "{";
  j += "\"valid\":"  + String(gpsData.valid ? "true" : "false") + ",";
  j += "\"lat\":"    + String(gpsData.lat, 6) + ",";
  j += "\"lng\":"    + String(gpsData.lng, 6) + ",";
  j += "\"sats\":"   + String(gpsData.sats) + ",";
  j += "\"hdop\":"   + String(gpsData.hdop) + ",";
  j += "\"time\":\"" + gpsData.timeUTC + "\"";
  j += "}";
  return j;
}

String appendGpsTag(const String &msg) {
  if (gpsData.valid)
    return msg + " [GPS:" + String(gpsData.lat, 5) + "," + String(gpsData.lng, 5) + "]";
  return msg + " [GPS:NO_FIX]";
}

String generateMsgId() {
  char buf[9];
  snprintf(buf, sizeof(buf), "%08lX", millis());
  return String(buf);
}

// named String variable — not a temporary rvalue
void sendLoRa(const String &msg) {
  if (!loraReady) { loraTxFailCount++; return; }
  String msgId  = generateMsgId();
  String tagged = "MSGID:" + msgId + " " + appendGpsTag(msg);
  if (!LoRa.beginPacket()) { loraTxFailCount++; return; }
  LoRa.print(tagged);
  if (LoRa.endPacket() == 1) {
    loraTxCount++;
    String notif = "__TX_MSGID__:" + msgId;  // FIX-1
    webSocket.broadcastTXT(notif);
    Serial.println("[TX] OK MSGID:" + msgId);
  } else {
    loraTxFailCount++;
    Serial.println("[TX] FAIL");
  }
}

// const String& cannot bind to String& — copy to local var
void broadcastExcept(uint8_t skip, const String &msg) {
  String m = msg;  // FIX-2
  for (uint8_t i = 0; i < WEBSOCKETS_SERVER_CLIENT_MAX; i++)
    if (i != skip && webSocket.clientIsConnected(i))
      webSocket.sendTXT(i, m);
}

// ══════════════════════════════════════════════════════════════
//  WEBSOCKET
// ══════════════════════════════════════════════════════════════
void onWsEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED: {
      String gpsInit = "__GPS__:" + buildGpsJson();  // FIX-3
      webSocket.sendTXT(num, gpsInit);
      break;
    }
    case WStype_TEXT: {
      String msg = String((char*)payload).substring(0, length);
      broadcastExcept(num, msg);
      sendLoRa(msg);
      break;
    }
    default: break;
  }
}

// ══════════════════════════════════════════════════════════════
//  HTTP
// ══════════════════════════════════════════════════════════════
void handleRoot(AsyncWebServerRequest *req)    { req->send_P(200, "text/html", INDEX_HTML); }
void handleCaptive(AsyncWebServerRequest *req) { req->redirect("http://192.168.4.1/"); }

// ══════════════════════════════════════════════════════════════
//  SETUP
// ══════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println(F("\n=== Emergency Chat Box ==="));

  // ── LoRa ──────────────────────────────────────────────────
  pinMode(LORA_RST, OUTPUT);
  digitalWrite(LORA_RST, LOW);  delay(10);
  digitalWrite(LORA_RST, HIGH); delay(10);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  loraReady = false;
  for (int i = 1; i <= 3; i++) {
    if (LoRa.begin(LORA_FREQ)) {
      LoRa.setSpreadingFactor(9);
      LoRa.setSignalBandwidth(125E3);
      LoRa.setCodingRate4(5);
      LoRa.setTxPower(17);
      loraReady = true;
      Serial.println(F("[LoRa] OK"));
      break;
    }
    Serial.printf("[LoRa] Attempt %d failed\n", i);
    delay(500);
  }
  if (!loraReady) Serial.println(F("[LoRa] FAILED — check wiring"));

  // ── GPS ───────────────────────────────────────────────────
  gpsSerial.begin(GPS_BAUD);
  Serial.println(F("[GPS] Started — waiting for fix..."));

  // ── WiFi AP ───────────────────────────────────────────────
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID);
  Serial.printf("[WiFi] AP: %s  IP: %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());

  // ── DNS captive portal ────────────────────────────────────
  dnsServer.start(53, "*", WiFi.softAPIP());

  // ── HTTP ──────────────────────────────────────────────────
  httpServer.on("/",                    HTTP_GET, handleRoot);
  httpServer.on("/generate_204",        HTTP_GET, handleCaptive);
  httpServer.on("/hotspot-detect.html", HTTP_GET, handleCaptive);
  httpServer.on("/fwlink",              HTTP_GET, handleCaptive);
  httpServer.on("/ncsi.txt",            HTTP_GET, handleCaptive);
  httpServer.onNotFound([](AsyncWebServerRequest *r) {
    r->send(200, "text/html", "<h1>Emergency Portal</h1>");
  });
  httpServer.begin();

  // ── WebSocket ─────────────────────────────────────────────
  webSocket.begin();
  webSocket.onEvent(onWsEvent);
  Serial.println(F("=== Boot complete ==="));
}

// ══════════════════════════════════════════════════════════════
//  LOOP
// ══════════════════════════════════════════════════════════════
void loop() {

  // ── GPS feed ──────────────────────────────────────────────
  while (gpsSerial.available()) {
    char c = gpsSerial.read();
    if (gps.encode(c)) {
      gpsData.valid = gps.location.isValid();
      if (gpsData.valid) {
        gpsData.lat = gps.location.lat();
        gpsData.lng = gps.location.lng();
      }
      if (gps.satellites.isValid()) gpsData.sats = (int)gps.satellites.value();
      if (gps.hdop.isValid())       gpsData.hdop = (int)gps.hdop.value();
      if (gps.time.isValid()) {
        char buf[9];
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
                 gps.time.hour(), gps.time.minute(), gps.time.second());
        gpsData.timeUTC = buf;
      }
    }
  }

  unsigned long ms = millis();

  // ── Periodic GPS push ─────────────────────────────────────
  if (ms - lastGpsBroadcast >= GPS_BROADCAST_MS) {
    lastGpsBroadcast = ms;
    String gpsPkt = "__GPS__:" + buildGpsJson();  // FIX-4
    webSocket.broadcastTXT(gpsPkt);
  }

  // ── Incoming LoRa ─────────────────────────────────────────
  int pktSize = LoRa.parsePacket();
  if (pktSize > 0) {
    String in = "";
    while (LoRa.available()) in += (char)LoRa.read();
    int rssi = LoRa.packetRssi();
    loraRxCount++;
    Serial.println("[RX] " + in + "  RSSI:" + String(rssi));

    if (in.startsWith("__ACK__:")) {
      if (in.length() >= 16) {
        String ackedId = in.substring(8, 16);
        String ackFwd  = "__ACK__:" + ackedId;  // FIX-5
        webSocket.broadcastTXT(ackFwd);
      }
      goto loop_end;
    }

    {
      String fwd = in + "  (RSSI:" + String(rssi) + "dBm)";  // FIX-6
      webSocket.broadcastTXT(fwd);
    }
  }

  loop_end:
  dnsServer.processNextRequest();
  webSocket.loop();
}
