#include <WiFi.h>
#include <WebServer.h>
#include <esp_now.h>
#include <BleGamepad.h>

// ============ CONFIGURATION ============
#define UPDATE_RATE_HZ 100  // 100Hz (10ms interval)
// #define UPDATE_RATE_HZ 50   // 50Hz (20ms interval)
#define UPDATE_INTERVAL (1000 / UPDATE_RATE_HZ)

// PPM Input Pins
#define PPM_PIN_CH1 32  // Roll
#define PPM_PIN_CH2 33  // Pitch
#define PPM_PIN_CH3 25  // Throttle
#define PPM_PIN_CH4 26  // Yaw
#define PPM_PIN_CH5 27  // Knob 1
#define PPM_PIN_CH6 14  // Knob 2

// WiFi AP Configuration
const char* ssid = "RC_Config";
const char* password = "12345678";

// ESP-NOW Peer MAC
uint8_t receiverMAC[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ============ GLOBAL OBJECTS ============
WebServer server(80);
BleGamepad bleGamepad("RC_Controller", "ESP32", 100);

// ============ CHANNEL DATA ============
volatile uint16_t channels[16] = {1500}; 
uint16_t virtualChannels[10] = {1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000};

// PPM timing
volatile unsigned long ppmRisingTime[6] = {0};
volatile uint16_t ppmPulseWidth[6] = {1500};

// Calibration
uint16_t calibratedCenters[4] = {1500, 1500, 1500, 1500};
#define DEADZONE 25

// ============ ESP-NOW DATA STRUCTURE ============
typedef struct {
  uint16_t ch[16];
  uint8_t checksum;
} __attribute__((packed)) CompressedRCData;

// ============ PPM INTERRUPT HANDLERS ============
void IRAM_ATTR handlePPM(uint8_t ch, uint8_t pin) {
  if (digitalRead(pin) == HIGH) {
    ppmRisingTime[ch] = micros();
  } else {
    unsigned long w = micros() - ppmRisingTime[ch];
    if (w >= 900 && w <= 2100) ppmPulseWidth[ch] = w;
  }
}

void IRAM_ATTR handlePPM_CH1() { handlePPM(0, PPM_PIN_CH1); }
void IRAM_ATTR handlePPM_CH2() { handlePPM(1, PPM_PIN_CH2); }
void IRAM_ATTR handlePPM_CH3() { handlePPM(2, PPM_PIN_CH3); }
void IRAM_ATTR handlePPM_CH4() { handlePPM(3, PPM_PIN_CH4); }
void IRAM_ATTR handlePPM_CH5() { handlePPM(4, PPM_PIN_CH5); }
void IRAM_ATTR handlePPM_CH6() { handlePPM(5, PPM_PIN_CH6); }

void setupPPM() {
  const uint8_t pins[] = {PPM_PIN_CH1, PPM_PIN_CH2, PPM_PIN_CH3, PPM_PIN_CH4, PPM_PIN_CH5, PPM_PIN_CH6};
  void (*handlers[])() = {handlePPM_CH1, handlePPM_CH2, handlePPM_CH3, handlePPM_CH4, handlePPM_CH5, handlePPM_CH6};
  
  for(int i = 0; i < 6; i++) {
    pinMode(pins[i], INPUT);
    attachInterrupt(digitalPinToInterrupt(pins[i]), handlers[i], CHANGE);
  }
}

void readPPMChannels() {
  noInterrupts();
  for (int i = 0; i < 6; i++) channels[i] = ppmPulseWidth[i];
  for (int i = 0; i < 10; i++) channels[i + 6] = virtualChannels[i];
  interrupts();
}

// ============ CALIBRATION ============
void calibrateJoysticks() {
  Serial.println("Calibrating... Keep sticks centered!");
  delay(500);
  
  long sum[4] = {0};
  for(int i = 0; i < 50; i++) {
    noInterrupts();
    for(int j = 0; j < 4; j++) sum[j] += ppmPulseWidth[j];
    interrupts();
    delay(10);
  }
  
  for(int i = 0; i < 4; i++) {
    calibratedCenters[i] = sum[i] / 50;
    Serial.printf("CH%d center: %d\n", i+1, calibratedCenters[i]);
  }
  Serial.println("Calibration complete!");
}

// ============ ESP-NOW FUNCTIONS ============
void onDataSent(const uint8_t *mac, esp_now_send_status_t status) {}
void onDataSentNew(const wifi_tx_info_t *info, esp_now_send_status_t status) {}

void sendESPNOW() {
  CompressedRCData data;
  for (int i = 0; i < 16; i++) data.ch[i] = channels[i];
  
  data.checksum = 0;
  for (int i = 0; i < 16; i++) {
    data.checksum ^= (data.ch[i] & 0xFF);
    data.checksum ^= (data.ch[i] >> 8);
  }
  
  esp_now_send(receiverMAC, (uint8_t*)&data, sizeof(data));
}

// ============ BLE GAMEPAD OUTPUT ============
void sendBLEGamepad() {
  if (!bleGamepad.isConnected()) return;
  
  // Apply deadzone to analog channels
  uint16_t ch[4];
  for(int i = 0; i < 4; i++) {
    if (abs((int)channels[i] - (int)calibratedCenters[i]) < DEADZONE) {
      ch[i] = calibratedCenters[i];
    } else {
      ch[i] = channels[i];
    }
  }
  
  // Map to gamepad range (0-32767)
  auto mapCh = [](uint16_t val, uint16_t minV, uint16_t maxV) {
    val = constrain(val, minV, maxV);
    return (val - minV) * 32767 / (maxV - minV);
  };
  
  bleGamepad.setAxes(
    mapCh(ch[0], 1000, 2000),  // Roll (X)
    mapCh(ch[1], 1000, 2000),  // Pitch (Y)
    mapCh(ch[2], 1000, 2000),  // Throttle (Z)
    mapCh(ch[3], 1000, 2000),  // Yaw (RZ)
    mapCh(channels[4], 1000, 2000),  // Knob 1 (RX)
    mapCh(channels[5], 1000, 2000)   // Knob 2 (RY)
  );
  
  // Virtual switches as buttons (1-10)
  for(int i = 0; i < 10; i++) {
    if (virtualChannels[i] >= 1750) {
      bleGamepad.press(i + 1);  // HIGH
    } else if (virtualChannels[i] >= 1250) {
      bleGamepad.release(i + 1); // MID (optional press)
    } else {
      bleGamepad.release(i + 1); // LOW
    }
  }
  
  bleGamepad.sendReport();
}

// ============ WEB SERVER ============
const char html[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>RC Config</title><style>
body{font-family:Arial;margin:20px;background:#1a1a1a;color:#fff}
h2{color:#4CAF50}.info{padding:15px;background:#2a2a2a;border-radius:8px;margin:15px 0;border-left:4px solid #4CAF50}
.ch{margin:20px 0;padding:15px;background:#2a2a2a;border-radius:8px}
.sw{display:flex;gap:10px;margin-top:10px}
.btn{flex:1;padding:15px;border:none;border-radius:5px;font-size:16px;cursor:pointer;transition:0.3s}
.btn.on{background:#4CAF50;color:#fff}.btn:not(.on){background:#444;color:#aaa}
.st{padding:10px;background:#333;border-radius:5px;margin:10px 0}
.ind{display:inline-block;width:12px;height:12px;border-radius:50%;margin-right:8px}
.on{background:#4CAF50;animation:pulse 1.5s infinite}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:0.5}}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(80px,1fr));gap:10px}
.chd{background:#333;padding:10px;border-radius:5px;text-align:center;font-size:12px}
.chv{font-size:18px;font-weight:bold;color:#4CAF50}
</style></head><body>
<h2>🎮 RC Config</h2>
<div class="info"><b>📡 Dual Output</b><br>
<span class="ind on"></span>ESP-NOW (Drone)<br><span class="ind on"></span>BLE Gamepad (Simulator)</div>
<div class="st"><b>Rate:</b> UPDATE_HZ Hz | <b>BLE:</b> <span id="ble">Checking...</span></div>
<div class="st"><b>PPM Channels:</b><div class="grid" id="ch"></div></div>
<h3>Virtual Switches</h3><div id="vs"></div>
<script>
let sw=Array(10).fill(1000);
for(let i=0;i<10;i++){
document.getElementById('vs').innerHTML+='<div class="ch"><b>CH'+(i+7)+'</b><div class="sw">'+
'<button class="btn" onclick="set('+i+',1000)">LOW<br>1000</button>'+
'<button class="btn" onclick="set('+i+',1500)">MID<br>1500</button>'+
'<button class="btn" onclick="set('+i+',2000)">HIGH<br>2000</button></div></div>';}
function set(i,v){sw[i]=v;fetch('/s?c='+i+'&v='+v).then(upd);}
function upd(){
for(let i=0;i<10;i++){
let b=document.querySelectorAll('.ch:nth-child('+(i+3)+') .btn');
[1000,1500,2000].forEach(function(v,j){b[j].classList.toggle('on',sw[i]==v);});}}
function ref(){fetch('/d').then(function(r){return r.json();}).then(function(d){
let s='';for(let i=0;i<6;i++)s+='<div class="chd">CH'+(i+1)+'<br><span class="chv">'+d.c[i]+'</span></div>';
document.getElementById('ch').innerHTML=s;
document.getElementById('ble').textContent=d.ble?'Connected':'Disconnected';
for(let i=0;i<10;i++)sw[i]=d.v[i];upd();});}
upd();setInterval(ref,500);ref();
</script></body></html>
)rawliteral";

void handleRoot() {
  String p = FPSTR(html);
  p.replace("UPDATE_HZ", String(UPDATE_RATE_HZ));
  server.send(200, "text/html", p);
}

void handleSet() {
  if (server.hasArg("c") && server.hasArg("v")) {
    int c = server.arg("c").toInt();
    int v = server.arg("v").toInt();
    if (c >= 0 && c < 10 && (v == 1000 || v == 1500 || v == 2000)) {
      virtualChannels[c] = v;
      server.send(200, "text/plain", "OK");
      return;
    }
  }
  server.send(400, "text/plain", "Bad");
}

void handleData() {
  String j = "{\"c\":[";
  for (int i = 0; i < 6; i++) {
    j += channels[i];
    if (i < 5) j += ",";
  }
  j += "],\"v\":[";
  for (int i = 0; i < 10; i++) {
    j += virtualChannels[i];
    if (i < 9) j += ",";
  }
  j += "],\"ble\":";
  j += bleGamepad.isConnected() ? "true" : "false";
  j += "}";
  server.send(200, "application/json", j);
}

// ============ SETUP ============
void setup() {
  Serial.begin(115200);
  Serial.println("\nRC TX Ready");
  Serial.printf("Rate: %dHz\n", UPDATE_RATE_HZ);
  
  setupPPM();
  delay(100);
  calibrateJoysticks();
  
  // BLE Gamepad Setup
  BleGamepadConfiguration cfg;
  cfg.setAutoReport(false);
  cfg.setControllerType(CONTROLLER_TYPE_JOYSTICK);
  cfg.setButtonCount(10);
  cfg.setHatSwitchCount(0);
  bleGamepad.begin(&cfg);
  Serial.println("BLE Gamepad: RC_Controller");
  
  // WiFi AP
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(ssid, password);
  Serial.printf("AP: %s\nIP: ", ssid);
  Serial.println(WiFi.softAPIP());
  
  // Web Server
  server.on("/", handleRoot);
  server.on("/s", handleSet);
  server.on("/d", handleData);
  server.begin();
  
  // ESP-NOW
  if (esp_now_init() == ESP_OK) {
    #ifdef ESP_IDF_VERSION_MAJOR
      #if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
        esp_now_register_send_cb(onDataSentNew);
      #else
        esp_now_register_send_cb(onDataSent);
      #endif
    #else
      esp_now_register_send_cb(onDataSent);
    #endif
    
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, receiverMAC, 6);
    peer.channel = 0;
    peer.encrypt = false;
    esp_now_add_peer(&peer);
    Serial.println("ESP-NOW ready");
  }
  
  Serial.println("System Ready!");
}

// ============ MAIN LOOP ============
unsigned long lastUpdate = 0;

void loop() {
  server.handleClient();
  
  unsigned long now = millis();
  if (now - lastUpdate >= UPDATE_INTERVAL) {
    lastUpdate = now;
    readPPMChannels();
    sendESPNOW();
    sendBLEGamepad();
  }
}