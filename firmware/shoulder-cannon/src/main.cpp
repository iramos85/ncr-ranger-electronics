#include <Arduino.h>
#include <ESP32Servo.h>

// Confirmed hardware connections
constexpr int TILT_PIN = 18; // Upper servo — YES
constexpr int PAN_PIN  = 19; // Lower servo — NO

Servo tiltServo;
Servo panServo;

bool servosArmed = false;

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("=============================");
    Serial.println("NCR RANGER SHOULDER CANNON");
    Serial.println("=============================");
    Serial.println("GPIO18 = TILT (UPPER / YES)");
    Serial.println("GPIO19 = PAN  (LOWER / NO)");
    Serial.println("SERVOS DISARMED");
}

void loop() {
    // No movement until servo arming and calibration
    // have been implemented and verified.
    delay(20);
}