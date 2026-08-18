#include <WiFi.h> // NOTE: Change to <ESP8266WiFi.h> if you are using an ESP8266
#include <WiFiUdp.h>

// --- Wi-Fi Credentials ---
const char* ssid = "ESP32-Drone";
const char* password = "12345678";

// --- Drone Settings ---
IPAddress droneIP(192, 168, 43, 42);
const int dronePort = 2390; // Default Crazyflie/LiteWing UDP port
const int localPort = 2390; // local UDP port to bind to

WiFiUDP udp;

// --- Flight Variables ---
int16_t currentRoll = 0;
int16_t currentPitch = 0;
int16_t currentYaw = 0;
uint16_t currentThrust = 0;

// Scaling configuration (adjust to match your firmware expectations)
// Example: roll/pitch/yaw in degrees * 100 -> int16 range
const float ANGLE_SCALE = 100.0f; // multiply degrees by this to get int16_t
const float THRUST_SCALE = 1.0f;  // if you need to scale thrust (e.g., 0..10000)

// --- State Machine Variables ---
unsigned long stateStartTime = 0;
int flightState = 0;
unsigned long lastSendTime = 0;
const unsigned int sendInterval = 10; // Send packet every 10 ms (100 Hz)

// --- CRTP Packet Structure ---
// Port 3 (Commander), Channel 0 is used for legacy setpoints
struct __attribute__((packed)) CrtpSetpointInt16 {
  uint8_t header; // 0x30 represents Port 3, Channel 0 (legacy setpoint)
  int16_t roll;
  int16_t pitch;
  int16_t yaw;
  uint16_t thrust;
};

void connectWiFi(unsigned long timeoutMs = 10000) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
    Serial.print(".");
    if (millis() - start > timeoutMs) {
      Serial.println("\nWiFi connect timeout.");
      return;
    }
  }
  Serial.println("\nWiFi connected.");
  Serial.print("Local IP: ");
  Serial.println(WiFi.localIP());
}

void setup() {
  Serial.begin(115200);
  delay(10);
  Serial.println("\n--- LiteWing / CRTP Sender (int16) ---");

  connectWiFi();

  // Bind local UDP port (helps receive replies and ensures a local socket)
  if (!udp.begin(localPort)) {
    Serial.println("Failed to start UDP on local port.");
  } else {
    Serial.print("UDP bound to port ");
    Serial.println(localPort);
  }

  Serial.println("Waiting 1s for stability...");
  delay(1000);

  Serial.println("Sending zero setpoint to unlock safety...");
  stateStartTime = millis();
}

// Function to construct and send the CRTP UDP payload (int16 version)
void sendCrtpSetpointInt16(int16_t roll, int16_t pitch, int16_t yaw, uint16_t thrust) {
  CrtpSetpointInt16 packet;
  packet.header = 0x30; // port 3, channel 0 (legacy commander). Verify for your target firmware.
  // Values are assumed to be little-endian on the wire (ESP32 is little-endian).
  packet.roll = roll;
  packet.pitch = pitch;
  packet.yaw = yaw;
  packet.thrust = thrust;

  udp.beginPacket(droneIP, dronePort);
  udp.write(reinterpret_cast<uint8_t*>(&packet), sizeof(packet));
  udp.endPacket();
}

void loop() {
  unsigned long currentTime = millis();

  // Reconnect WiFi if dropped
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected, attempting reconnect...");
    connectWiFi(5000);
    // brief pause before continuing state machine
    stateStartTime = millis();
  }

  // --- Flight Sequence State Machine (non-blocking) ---
  if (flightState == 0) {
    // State 0: Unlock safety (Wait 0.1s)
    currentRoll = 0; currentPitch = 0; currentYaw = 0; currentThrust = 0;
    if (currentTime - stateStartTime > 100) {
      Serial.println("Starting motors at minimum speed...");
      flightState = 1;
      stateStartTime = currentTime;
    }
  } else if (flightState == 1) {
    // State 1: Start motors at 10000 thrust (Wait 1.0s)
    currentRoll = 0; currentPitch = 0; currentYaw = 0; currentThrust = 10000;
    if (currentTime - stateStartTime > 1000) {
      Serial.println("Stopping motors...");
      flightState = 2;
      stateStartTime = currentTime;
    }
  } else if (flightState == 2) {
    // State 2: Stop motors (Wait 0.1s)
    currentRoll = 0; currentPitch = 0; currentYaw = 0; currentThrust = 0;
    if (currentTime - stateStartTime > 100) {
      Serial.println("Test complete. Entering idle state.");
      flightState = 3; // Move to idle state
    }
  }

  // --- Continuous Transmission ---
  if (flightState < 3 && (currentTime - lastSendTime >= sendInterval)) {
    // send scaled values
    int16_t sroll = (int16_t)constrain((float)currentRoll * ANGLE_SCALE, -32767, 32767);
    int16_t spitch = (int16_t)constrain((float)currentPitch * ANGLE_SCALE, -32767, 32767);
    int16_t syaw = (int16_t)constrain((float)currentYaw * ANGLE_SCALE, -32767, 32767);
    uint16_t sthrust = (uint16_t)constrain((float)currentThrust * THRUST_SCALE, 0, 65535);

    sendCrtpSetpointInt16(sroll, spitch, syaw, sthrust);
    lastSendTime = currentTime;
  }

  // Optional: handle any incoming UDP (e.g., telemetry) - non-blocking
  int packetSize = udp.parsePacket();
  if (packetSize) {
    Serial.print("UDP packet received, size=");
    Serial.println(packetSize);
  }
}
