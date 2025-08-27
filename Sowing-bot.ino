#define BLYNK_PRINT Serial
#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>

// Blynk Template Information
#define BLYNK_TEMPLATE_ID "TMPL3WHs6_Nvx"
#define BLYNK_TEMPLATE_NAME "Sowing BOT"
#define BLYNK_AUTH_TOKEN "s20U5uWr1CV91Ujg4sbsNex-RFGxikDy"

// WiFi credentials
char ssid[] = "YourWiFiSSID";
char pass[] = "YourWiFiPassword";

// Pin definitions
const int RIGHT_FORWARD = D0;
const int RIGHT_BACKWARD = D5;
const int LEFT_FORWARD = D6;
const int LEFT_BACKWARD = D7;
const int SOWING_MOTOR = D8;

// State variables
struct {
  int joystick = 1;        // V0 last value
  int turn = 1;            // V1 last value
  bool sowing = false;     // V5 last value
  bool autoMode = true;    // V4 last value
  uint8_t autoPhase = 0;   // Auto mode phase
  unsigned long phaseStart = 0;
} state;

struct {
  bool wifiConnected = false;
  unsigned long lastAttempt = 0;
  int lastReportedState = -1;
  bool lastSowingState = false;
} connection;

// Timing constants
constexpr unsigned long FORWARD_DURATION = 30000;
constexpr unsigned long SOWING_OFF_TIME = 25000;
constexpr unsigned long BACKWARD_DURATION = 30000;
constexpr unsigned long WIFI_RETRY_INTERVAL = 30000;
constexpr uint16_t WIFI_TIMEOUT_MS = 10000;
constexpr uint16_t UPDATE_INTERVAL = 200;

BlynkTimer timer;

void setup() {
  Serial.begin(115200);
  
  // Initialize relay pins
  const uint8_t pins[] = {RIGHT_FORWARD, RIGHT_BACKWARD, LEFT_FORWARD, LEFT_BACKWARD, SOWING_MOTOR};
  for (uint8_t pin : pins) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }
  
  connectToWiFi();
  timer.setInterval(UPDATE_INTERVAL, updateSystem);
}

void connectToWiFi() {
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, pass);
  
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) {
    delay(500);
    Serial.print(".");
  }

  connection.wifiConnected = (WiFi.status() == WL_CONNECTED);
  if (connection.wifiConnected) {
    Serial.println("\nWiFi connected!");
    Blynk.config(BLYNK_AUTH_TOKEN);
    if (Blynk.connect()) {
      Serial.println("Blynk connected");
      Blynk.syncAll();
    }
  } else {
    Serial.println("\nWiFi connection failed");
  }
  connection.lastAttempt = millis();
}

void updateSystem() {
  handleConnection();
  
  if (state.autoMode) {
    handleAutoMode();
  }

  reportStatus();
}

void handleConnection() {
  if (!connection.wifiConnected && millis() - connection.lastAttempt > WIFI_RETRY_INTERVAL) {
    connectToWiFi();
  }
}

void handleAutoMode() {
  const unsigned long now = millis();
  
  switch(state.autoPhase) {
    case 0: // Start forward
      allStop();
      setMovement(true, false, true);
      state.phaseStart = now;
      state.autoPhase = 1;
      Serial.println("Auto: Forward with sowing");
      break;
      
    case 1: // Forward phase
      if (now - state.phaseStart >= SOWING_OFF_TIME) {
        digitalWrite(SOWING_MOTOR, LOW);
        Serial.println("Auto: Sowing stopped");
      }
      if (now - state.phaseStart >= FORWARD_DURATION) {
        allStop();
        state.phaseStart = now;
        state.autoPhase = 2;
        Serial.println("Auto: Preparing backward");
      }
      break;
      
    case 2: // Start backward
      setMovement(false, true, false);
      state.phaseStart = now;
      state.autoPhase = 3;
      Serial.println("Auto: Moving backward");
      break;
      
    case 3: // Backward phase
      if (now - state.phaseStart >= BACKWARD_DURATION) {
        allStop();
        state.autoPhase = 0;
        Serial.println("Auto: Cycle complete");
      }
      break;
  }
}

void reportStatus() {
  if (!connection.wifiConnected || !Blynk.connected()) return;

  const int currentState = getCurrentState();
  if (currentState != connection.lastReportedState) {
    Blynk.virtualWrite(V2, currentState);
    connection.lastReportedState = currentState;
  }

  if (state.sowing != connection.lastSowingState) {
    Blynk.virtualWrite(V5, state.sowing);
    connection.lastSowingState = state.sowing;
  }
}

// Movement helpers
void setMovement(bool forward, bool backward, bool sowing) {
  digitalWrite(RIGHT_FORWARD, forward);
  digitalWrite(LEFT_FORWARD, forward);
  digitalWrite(RIGHT_BACKWARD, backward);
  digitalWrite(LEFT_BACKWARD, backward);
  digitalWrite(SOWING_MOTOR, sowing && !backward); // Safety: no sowing in reverse
}

void allStop() {
  setMovement(false, false, false);
}

// Blynk handlers
BLYNK_WRITE(V0) { // Joystick
  if (state.autoMode) return;
  
  const int value = param.asInt();
  if (value != state.joystick) {
    state.joystick = value;
    setMovement(value == 2, value == 0, value == 2 && state.sowing);
    Serial.print("Manual: ");
    Serial.println(value == 2 ? "Forward" : value == 0 ? "Backward" : "Stop");
  }
}

BLYNK_WRITE(V1) { // Turn
  if (state.autoMode) return;
  
  const int value = param.asInt();
  if (value != state.turn) {
    state.turn = value;
    allStop();
    
    if (value == 2) { // Right
      digitalWrite(RIGHT_BACKWARD, HIGH);
      digitalWrite(LEFT_FORWARD, HIGH);
      Serial.println("Manual: Right turn");
    } 
    else if (value == 0) { // Left
      digitalWrite(LEFT_BACKWARD, HIGH);
      digitalWrite(RIGHT_FORWARD, HIGH);
      Serial.println("Manual: Left turn");
    } else {
      Serial.println("Manual: Turn stopped");
    }
  }
}

BLYNK_WRITE(V4) { // Auto/Manual
  const bool newMode = param.asInt() == 1;
  if (newMode != state.autoMode) {
    state.autoMode = newMode;
    allStop();
    state.autoPhase = 0;
    
    Serial.print("Mode: ");
    Serial.println(state.autoMode ? "Auto" : "Manual");
    
    if (connection.wifiConnected && Blynk.connected()) {
      Blynk.virtualWrite(V4, state.autoMode); // Confirm mode change
    }
  }
}

BLYNK_WRITE(V5) { // Sowing (Manual only)
  const bool newState = param.asInt() == 1;
  if (newState != state.sowing && !state.autoMode) {
    state.sowing = newState;
    digitalWrite(SOWING_MOTOR, state.sowing && state.joystick == 2);
    Serial.print("Sowing: ");
    Serial.println(state.sowing ? "ON" : "OFF");
  }
}

int getCurrentState() {
  if (state.autoMode) {
    return state.autoPhase + 1; // 1-4 for auto phases
  }
  
  if (digitalRead(RIGHT_FORWARD) && digitalRead(LEFT_FORWARD)) return 5; // Manual forward
  if (digitalRead(RIGHT_BACKWARD) && digitalRead(LEFT_BACKWARD)) return 6; // Manual back
  if (digitalRead(RIGHT_BACKWARD) && digitalRead(LEFT_FORWARD)) return 7;  // Manual right
  if (digitalRead(LEFT_BACKWARD) && digitalRead(RIGHT_FORWARD)) return 8;  // Manual left
  return 0; // Stopped
}

BLYNK_CONNECTED() {
  Blynk.syncAll();
}

void loop() {
  if (connection.wifiConnected) {
    Blynk.run();
  }
  timer.run();
}