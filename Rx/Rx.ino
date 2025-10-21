#include <WiFi.h>
#include <esp_now.h>

// ============ CONFIGURATION ============
#define IBUS_TX_PIN 17  // UART2 TX to flight controller

// ============ ESP-NOW DATA STRUCTURE ============
typedef struct {
  uint16_t ch[16];
  uint8_t checksum;
} __attribute__((packed)) CompressedRCData;

// ============ GLOBAL VARIABLES ============
HardwareSerial IBusSerial(2);
uint16_t channels[16] = {1500}; // Default to center
unsigned long lastPacketTime = 0;
bool dataValid = false;

// ============ iBUS PROTOCOL FUNCTIONS ============
uint16_t ibus_checksum(uint8_t* buf, uint8_t len) {
  uint16_t sum = 0xFFFF;
  for (uint8_t i = 0; i < len; i++) {
    sum -= buf[i];
  }
  return sum;
}

void sendIBusPacket() {
  uint8_t buf[32];
  buf[0] = 0x20; // Header byte 1
  buf[1] = 0x40; // Header byte 2
  
  // Pack 14 channels (standard iBus supports 14 channels)
  for (int i = 0; i < 14; i++) {
    buf[2 + i * 2] = channels[i] & 0xFF;
    buf[3 + i * 2] = channels[i] >> 8;
  }
  
  // Calculate and add checksum
  uint16_t checksum = ibus_checksum(buf, 30);
  buf[30] = checksum & 0xFF;
  buf[31] = checksum >> 8;
  
  // Send to flight controller
  IBusSerial.write(buf, 32);
}

// ============ ESP-NOW CALLBACK ============
// Callback for older ESP32 core
void onDataRecv(const uint8_t *mac, const uint8_t *data, int len) {
  if (len == sizeof(CompressedRCData)) {
    CompressedRCData *rcData = (CompressedRCData*)data;
    
    // Verify checksum
    uint8_t calc_checksum = 0;
    for (int i = 0; i < 16; i++) {
      calc_checksum ^= (rcData->ch[i] & 0xFF);
      calc_checksum ^= (rcData->ch[i] >> 8);
    }
    
    if (calc_checksum == rcData->checksum) {
      // Copy channels
      for (int i = 0; i < 16; i++) {
        // Validate channel range (1000-2000)
        if (rcData->ch[i] >= 1000 && rcData->ch[i] <= 2000) {
          channels[i] = rcData->ch[i];
        }
      }
      
      lastPacketTime = millis();
      dataValid = true;
      
      // Optional: Print for debugging
      Serial.print("CH1:");
      Serial.print(channels[0]);
      Serial.print(" CH2:");
      Serial.print(channels[1]);
      Serial.print(" CH3:");
      Serial.print(channels[2]);
      Serial.print(" CH4:");
      Serial.println(channels[3]);
    } else {
      Serial.println("Checksum mismatch!");
    }
  }
}

// Callback for newer ESP32 core (ESP-IDF 5.x)
void onDataRecvNew(const esp_now_recv_info *info, const uint8_t *data, int len) {
  if (len == sizeof(CompressedRCData)) {
    CompressedRCData *rcData = (CompressedRCData*)data;
    
    // Verify checksum
    uint8_t calc_checksum = 0;
    for (int i = 0; i < 16; i++) {
      calc_checksum ^= (rcData->ch[i] & 0xFF);
      calc_checksum ^= (rcData->ch[i] >> 8);
    }
    
    if (calc_checksum == rcData->checksum) {
      // Copy channels
      for (int i = 0; i < 16; i++) {
        // Validate channel range (1000-2000)
        if (rcData->ch[i] >= 1000 && rcData->ch[i] <= 2000) {
          channels[i] = rcData->ch[i];
        }
      }
      
      lastPacketTime = millis();
      dataValid = true;
      
      // Optional: Print for debugging
      Serial.print("CH1:");
      Serial.print(channels[0]);
      Serial.print(" CH2:");
      Serial.print(channels[1]);
      Serial.print(" CH3:");
      Serial.print(channels[2]);
      Serial.print(" CH4:");
      Serial.println(channels[3]);
    } else {
      Serial.println("Checksum mismatch!");
    }
  }
}

// ============ SETUP ============
void setup() {
  Serial.begin(115200);
  
  // Initialize iBus Serial for flight controller
  IBusSerial.begin(115200, SERIAL_8N1, -1, IBUS_TX_PIN);
  
  // Set device as WiFi Station
  WiFi.mode(WIFI_STA);
  
  // Print MAC address
  Serial.println("ESP32 Receiver Starting...");
  Serial.print("MAC Address: ");
  Serial.println(WiFi.macAddress());
  Serial.println("Use this MAC address in the transmitter code!");
  
  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }
  
  // Register callback - auto-detect ESP32 core version
  #ifdef ESP_IDF_VERSION_MAJOR
    #if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
      esp_now_register_recv_cb(onDataRecvNew);
    #else
      esp_now_register_recv_cb(onDataRecv);
    #endif
  #else
    esp_now_register_recv_cb(onDataRecv);
  #endif
  
  Serial.println("Receiver Ready - Waiting for data...");
}

// ============ MAIN LOOP ============
void loop() {
  // Check for timeout (failsafe)
  if (millis() - lastPacketTime > 1000 && dataValid) {
    Serial.println("Signal lost! Failsafe activated");
    // Set failsafe values (throttle low, centered controls)
    channels[0] = 1500; // Roll
    channels[1] = 1500; // Pitch
    channels[2] = 1000; // Throttle (LOW for safety)
    channels[3] = 1500; // Yaw
    for (int i = 4; i < 16; i++) {
      channels[i] = 1500;
    }
    dataValid = false;
  }
  
  // Send iBus packet at ~100Hz
  sendIBusPacket();
  delay(10);
}