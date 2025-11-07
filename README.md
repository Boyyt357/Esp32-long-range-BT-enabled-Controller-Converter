# **Long-Range RC Controller/Converter with ESP-NOW and BLE**

This project transforms a standard RC Transmitter's trainer port output (PPM signal) into a powerful, dual-purpose wireless controller using two ESP32 devices.

The system simultaneously transmits channel data over **ESP-NOW** for long-range drone/vehicle control (via iBus) and converts the input into a standard **Bluetooth Gamepad** for simulator or PC control.

## **Tx Installation**

<img width="766" height="561" alt="image" src="https://github.com/user-attachments/assets/294d30dd-65f9-40cb-924f-dc405440d7a7" />

### ***change to huge app or else you won't able to upload to esp32***

## **🚀 Architecture and Dual Output**

The system is split into two primary components:

### **1\. Transmitter (TX Unit \- The Controller)**

* **Input:** Reads the **PPM signal** (Pulse Position Modulation) from an RC transmitter's trainer port across 6 dedicated GPIO pins.  
* **Signal Processing:** The raw PPM pulse widths (1000-2000 $\\mu$s) are processed, centered, and subjected to a deadzone for stick inputs.  
* **Dual Output:**  
  * **ESP-NOW:** Sends compressed 16-channel data packets (including 10 virtual switch channels managed via the Web UI) to the RX Unit at a high rate (e.g., 50Hz).  
  * **Bluetooth Gamepad (BLE):** Presents itself as a standard wireless joystick to a connected PC, phone, or simulator, mapping RC sticks (Roll, Pitch, Throttle, Yaw) to axes and virtual switches to buttons.  
* **Configuration Interface:** Hosts a Wi-Fi Access Point and Web Server (http://192.168.4.1) allowing users to monitor channel values and control 10 virtual switches.

### **2\. Receiver (RX Unit \- The Drone Receiver)**

* **Input:** Receives 16-channel data packets via **ESP-NOW**.  
* **Failsafe:** Implements a timeout failsafe, reverting key channels (e.g., Throttle) to safe values if the signal is lost for over 1 second.  
* **Output:** Converts the received 16-channel data into the **iBus serial protocol** and outputs it on UART2's TX pin (Pin 14\) for direct connection to a modern Flight Controller (e.g., running Betaflight, ArduPilot).

## **🪢 Wiring & Hardware**

<img width="2972" height="1388" alt="full" src="https://github.com/user-attachments/assets/7c1c62fd-c20b-4505-b28f-bce6d30329c9" />

### **TX Unit (Controller Side)**

The TX unit connects to the PPM output of your RC Transmitter. Ensure you connect the PPM signal wire to the correct GPIO pins for each channel as defined in TX.ino.

| PPM Channel | Function | ESP32 GPIO Pin |
| :---- | :---- | :---- |
| CH1 | Roll | **32** |
| CH2 | Pitch | **33** |
| CH3 | Throttle | **25** |
| CH4 | Yaw | **26** |
| CH5 | Knob 1 | **27** |
| CH6 | Knob 2 | **14** |

### **RX Unit (iBus Receiver Side)**

The RX unit connects to the Flight Controller's receiver input (typically an RX pad, though this code uses TX Pin 14 as the output).

| Signal | ESP32 GPIO Pin | Connection to Flight Controller |
| :---- | :---- | :---- |
| iBus Signal Out | **14** (UART2 TX) | Flight Controller's RX Pin |
| GND | GND | Flight Controller's GND |
| VCC | 5V/3.3V | Flight Controller's VCC/5V/3.3V |

## **🛠️ Setup and Configuration**

The ESP-NOW protocol requires explicit MAC address pairing. You must exchange the Station MAC addresses between the two boards.

### **1\. Identify MAC Addresses**

Upload a basic sketch to both your TX and RX devices to find their Station MAC addresses:

\#include \<WiFi.h\>  
void setup() {  
  Serial.begin(115200);  
  delay(1000);  
  Serial.print("STA MAC Address: ");  
  Serial.println(WiFi.macAddress());  
}  
void loop() {}

### **2\. Configure TX Unit (TX.ino)**

Since the TX unit is configured to broadcast to a default address ({0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}), which is fine for simple setup, the most important step is setting the **TX power**.

* The code already sets the maximum TX power (20.5 dBm) for maximum range: esp\_wifi\_set\_max\_tx\_power(82);  
* **Crucial:** If you decide to use a specific MAC address instead of broadcast (for a cleaner connection), update the receiverMAC array in TX.ino with the **STA MAC address of your RX Unit**.

// ESP-NOW Peer MAC (Keep FFs for broadcast, or set specific RX MAC)  
uint8\_t receiverMAC\[\] \= {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

### **3\. Configure RX Unit (RX.ino)**

The RX unit operates in STA mode to receive all ESP-NOW packets and does not need to be configured with the TX MAC if the TX is broadcasting.

* The RX unit's purpose is to output iBus data on Pin **14**. Ensure your flight controller is configured to use the **iBus protocol** on the corresponding UART port.  
* **Note:** If you change the IBUS\_TX\_PIN in RX.ino, you must update the wiring diagram above.

### **4\. Upload and Calibrate**

1. Upload RX.ino to your RX ESP32 board.  
2. Upload TX.ino to your TX ESP32 board.  
3. On the TX unit, the setup() function will run a **calibration** routine. Keep the sticks centered during this phase.

## **💻 Web Configuration and Monitor (TX Unit)**

The TX unit hosts a simple web interface for monitoring channels and controlling virtual switches.

1. **Connect Client:** Connect your PC or phone to the Wi-Fi Access Point created by the TX Unit:  
   * **SSID:** RC\_Config  
   * **Password:** 12345678  
2. **Access Page:** Open a web browser and navigate to: http://192.168.4.1

On the web page, you can:

* View live Pulse Widths (1000-2000) for the 6 physical PPM channels.  
* See the connection status of the Bluetooth Gamepad.  
* Set the value (LOW: 1000, MID: 1500, HIGH: 2000\) for the **Virtual Switches (CH7-16)**. These values are transmitted via ESP-NOW to the drone receiver and mapped to buttons on the BLE Gamepad.
