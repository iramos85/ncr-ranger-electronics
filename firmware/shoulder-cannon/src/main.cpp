#include <Arduino.h>
#include <ESP32Servo.h>

constexpr int PAN_PIN = 19;   // Lower servo: left/right
constexpr int TILT_PIN = 18;  // Upper servo: disconnected

constexpr int PAN_MIN_US = 1400;
constexpr int PAN_MAX_US = 1600;
constexpr int PAN_CENTER_US = 1500;
constexpr int PAN_STEP_US = 20;

Servo panServo;

bool panArmed = false;
int panPulse = PAN_CENTER_US;

void printStatus() {
  Serial.println("\n=== SHOULDER CANNON ===");
  Serial.println("PAN: GPIO19 / LOWER");
  Serial.println("TILT: GPIO18 / DISCONNECTED");
  Serial.printf("PAN ARMED: %s\n",
                panArmed ? "YES" : "NO");
  Serial.printf("PAN PULSE: %d us\n", panPulse);
  Serial.println("=======================");
}

void disarmPan() {
  if (panArmed) {
    panServo.detach();
    panArmed = false;
  }
  Serial.println("PAN DISARMED");
}

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(100);
  delay(1000);

  Serial.println("NCR RANGER CANNON CALIBRATION");
  Serial.println("PAN ONLY - GPIO19");
  Serial.println("NO AUTOMATIC MOVEMENT");
  Serial.println("Commands:");
  Serial.println("STATUS");
  Serial.println("ARM PAN");
  Serial.println("PAN +");
  Serial.println("PAN -");
  Serial.println("DISARM");

  printStatus();
}

void loop() {
  if (!Serial.available()) return;

  String command = Serial.readStringUntil('\n');
  command.trim();
  command.toUpperCase();

  if (command == "STATUS") {
    printStatus();

  } else if (command == "ARM PAN") {
    if (!panArmed) {
    panServo.setPeriodHertz(50);

panServo.attach(PAN_PIN, 1000, 2000);

panArmed = panServo.attached();

if (panArmed) {
  panServo.writeMicroseconds(PAN_CENTER_US);
  panPulse = PAN_CENTER_US;
}
      Serial.println(panArmed ?
        "PAN ARMED - WATCH FOR MOVEMENT" :
        "PAN ATTACH FAILED");
    }

  } else if (command == "PAN +") {
    if (!panArmed) {
      Serial.println("PAN NOT ARMED");
      return;
    }

    panPulse = min(PAN_MAX_US,
                   panPulse + PAN_STEP_US);
    panServo.writeMicroseconds(panPulse);
    Serial.printf("PAN: %d us\n", panPulse);

  } else if (command == "PAN -") {
    if (!panArmed) {
      Serial.println("PAN NOT ARMED");
      return;
    }

    panPulse = max(PAN_MIN_US,
                   panPulse - PAN_STEP_US);
    panServo.writeMicroseconds(panPulse);
    Serial.printf("PAN: %d us\n", panPulse);

  } else if (command == "DISARM") {
    disarmPan();

  } else {
    Serial.println("UNKNOWN COMMAND");
  }
}