#include <Arduino.h>
#include "config.h"

// =================================================================
// RUNTIME STATE VARIABLES
// =================================================================
portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;

// Per-channel light state (used by both ISRs and main loop)
volatile bool lightState[NUM_LIGHTS]          = {false, false, false, false};
volatile uint32_t lastButtonPress[NUM_LIGHTS] = {0, 0, 0, 0};

// Motion tracking
uint32_t lastMotionTime = 0;
bool previousMotionDetected = false;

// Master override state tracking (for edge-logging only)
bool masterOverrideActive  = false;
bool prevMasterOverride    = false;

// =================================================================
// HARDWARE DRIVER HELPERS
// =================================================================

inline void writeRelay(uint8_t index, bool turnOn) {
  if (index < NUM_LIGHTS) {
    digitalWrite(RELAY_PINS[index], turnOn ? RELAY_ON : RELAY_OFF);
  }
}

inline bool isMotionDetected() {
  return (digitalRead(PIR_PIN_1) == HIGH) || (digitalRead(PIR_PIN_2) == HIGH);
}

/**
 * @brief Turns off all 4 relay channels immediately.
 */
void allLightsOff() {
  portENTER_CRITICAL(&stateMux);
  for (uint8_t i = 0; i < NUM_LIGHTS; i++) {
    lightState[i] = false;
  }
  portEXIT_CRITICAL(&stateMux);

  for (uint8_t i = 0; i < NUM_LIGHTS; i++) {
    writeRelay(i, false);
  }
}

// =================================================================
// BUTTON ISRs (per-channel toggle — operates only when master override is INACTIVE)
// =================================================================

void IRAM_ATTR handleButtonPress(uint8_t index) {
  uint32_t currentTime = millis();

  if ((currentTime - lastButtonPress[index]) > DEBOUNCE_DELAY_MS) {
    bool newState = false;

    portENTER_CRITICAL_ISR(&stateMux);
    lightState[index] = !lightState[index];
    newState = lightState[index];
    lastButtonPress[index] = currentTime;
    portEXIT_CRITICAL_ISR(&stateMux);

    writeRelay(index, newState);
  }
}

void IRAM_ATTR isr0() { handleButtonPress(0); }
void IRAM_ATTR isr1() { handleButtonPress(1); }
void IRAM_ATTR isr2() { handleButtonPress(2); }
void IRAM_ATTR isr3() { handleButtonPress(3); }

// =================================================================
// SETUP
// =================================================================

void setup() {
  Serial.begin(SERIAL_BAUD_RATE);
  delay(200);
  Serial.println("\n==============================================");
  Serial.println("   ESP32 MotionLights Controller v2.0        ");
  Serial.println("==============================================");

  // Master override pin: HIGH when inactive, LOW when override wire is connected
  pinMode(MASTER_OVERRIDE_PIN, INPUT_PULLUP);

  // PIR sensor pins
  pinMode(PIR_PIN_1, INPUT_PULLDOWN);
  pinMode(PIR_PIN_2, INPUT_PULLDOWN);

  // Relay outputs and per-channel button inputs
  for (uint8_t i = 0; i < NUM_LIGHTS; i++) {
    pinMode(RELAY_PINS[i], OUTPUT);
    writeRelay(i, false);            // Start all relays OFF
    pinMode(BUTTON_PINS[i], INPUT_PULLUP);
  }

  attachInterrupt(digitalPinToInterrupt(BUTTON_PINS[0]), isr0, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PINS[1]), isr1, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PINS[2]), isr2, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PINS[3]), isr3, FALLING);

  Serial.println("[SYSTEM] Ready. Entering main loop.");
}

// =================================================================
// MAIN LOOP
// =================================================================

void loop() {
  const uint32_t currentTime = millis();

  // ---------------------------------------------------------------
  // 1. READ MASTER OVERRIDE WIRE (GPIO 32 → GND = override active)
  // ---------------------------------------------------------------
  masterOverrideActive = (digitalRead(MASTER_OVERRIDE_PIN) == LOW);

  // Log transitions once (not every 50ms)
  if (masterOverrideActive && !prevMasterOverride) {
    Serial.println("[OVERRIDE] Master override ACTIVE — all lights OFF, PIR disabled.");
    allLightsOff();
    // Push lastMotionTime forward so PIR doesn't trigger the instant override releases
    lastMotionTime = currentTime;
  }
  if (!masterOverrideActive && prevMasterOverride) {
    Serial.println("[OVERRIDE] Master override RELEASED — resuming automatic PIR control.");
    // Reset lastMotionTime so the 10-second inactivity clock starts fresh
    lastMotionTime = currentTime;
  }
  prevMasterOverride = masterOverrideActive;

  // While override is active: keep all lights off and skip all PIR logic
  if (masterOverrideActive) {
    allLightsOff();          // Continuously enforce OFF in case an ISR toggled a relay
    delay(LOOP_POLL_INTERVAL_MS);
    return;
  }

  // ---------------------------------------------------------------
  // 2. NORMAL PIR AUTOMATION (only reached when override is inactive)
  // ---------------------------------------------------------------
  const bool motionNow = isMotionDetected();

  if (motionNow && !previousMotionDetected) {
    Serial.println("[PIR] Motion detected! Activating lights.");
  }
  previousMotionDetected = motionNow;

  if (motionNow) {
    lastMotionTime = currentTime;

    // Turn on any light that is currently off
    for (uint8_t i = 0; i < NUM_LIGHTS; i++) {
      bool needTurnOn = false;

      portENTER_CRITICAL(&stateMux);
      if (!lightState[i]) {
        lightState[i] = true;
        needTurnOn = true;
      }
      portEXIT_CRITICAL(&stateMux);

      if (needTurnOn) {
        writeRelay(i, true);
        Serial.printf("[AUTO] Channel %u ON by motion.\n", i + 1);
      }
    }
  } else {
    // No motion: turn off all channels after inactivity timeout
    if ((currentTime - lastMotionTime) > MOTION_TIMEOUT_MS) {
      for (uint8_t i = 0; i < NUM_LIGHTS; i++) {
        bool needTurnOff = false;

        portENTER_CRITICAL(&stateMux);
        if (lightState[i]) {
          lightState[i] = false;
          needTurnOff = true;
        }
        portEXIT_CRITICAL(&stateMux);

        if (needTurnOff) {
          writeRelay(i, false);
          Serial.printf("[AUTO] Channel %u OFF (inactivity timeout).\n", i + 1);
        }
      }
    }
  }

  delay(LOOP_POLL_INTERVAL_MS);
}