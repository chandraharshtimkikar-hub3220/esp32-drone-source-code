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
float currentRoll = 0.0f;
float currentPitch = 0.0f;
float currentYaw = 0.0f;
uint16_t currentThrust = 0;

// --- State Machine Variables ---
unsigned long stateStartTime = 0;
int flightState = 0;
unsigned long lastSendTime = 0;
const unsigned int sendInterval = 10; // Send packet every 10 ms (100 Hz)

// --- CRTP Packet Structure ---
// Port 3 (Commander), Channel 0 is used for legacy setpoints
struct __attribute__((packed)) CrtpSetpoint {
  uint8_t header; // 0x30 represents Port 3, Channel 0 (legacy setpoint) - validate for your firmware
  float roll;
  float pitch;
  float yaw;
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
  Serial.println("\n--- LiteWing / CRTP Sender ---");

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

// Function to construct and send the CRTP UDP payload
void sendCrtpSetpoint(float roll, float pitch, float yaw, uint16_t thrust) {
  // Clamp thrust to safe bounds (0..65535). Adjust if your firmware uses a different range.
  if (thrust > 65535) thrust = 65535;

  CrtpSetpoint packet;
  packet.header = 0x30; // port 3, channel 0 (legacy commander). Verify for your target firmware.
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
  // The drone requires a steady stream of commands to keep the connection alive
  if (flightState < 3 && (currentTime - lastSendTime >= sendInterval)) {
    sendCrtpSetpoint(currentRoll, currentPitch, currentYaw, currentThrust);
    lastSendTime = currentTime;
  }

  // Optional: handle any incoming UDP (e.g., telemetry) - non-blocking
  int packetSize = udp.parsePacket();
  if (packetSize) {
    Serial.print("UDP packet received, size=");
    Serial.println(packetSize);
    // You can read data if needed:
    // char incoming[128];
    // int len = udp.read(incoming, sizeof(incoming)-1);
    // incoming[len] = 0;
    // Serial.println(incoming);
  }
}
