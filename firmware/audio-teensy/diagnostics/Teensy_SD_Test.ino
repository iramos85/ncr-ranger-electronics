
#include <Arduino.h>
#include <SD.h>

const char* TEST_FILE = "/RANGER_TEST.TXT";

void setup() {
  Serial.begin(115200);

  // Give Windows time to open Serial Monitor.
  unsigned long start = millis();
  while (!Serial && millis() - start < 5000) {
    delay(10);
  }

  Serial.println();
  Serial.println("=== NCR RANGER SD CARD TEST ===");

  Serial.print("Initializing Teensy 4.1 SD card... ");

  if (!SD.begin(BUILTIN_SDCARD)) {
    Serial.println("FAILED");
    Serial.println("Check the card and onboard slot.");
    return;
  }

  Serial.println("SUCCESS");

  // Clear any previous diagnostic file.
  SD.remove(TEST_FILE);

  Serial.print("Writing test file... ");

  File file = SD.open(TEST_FILE, FILE_WRITE);

  if (!file) {
    Serial.println("FAILED");
    return;
  }

  file.println("NCR RANGER AUDIO SYSTEM");
  file.println("SD CARD READ/WRITE TEST PASSED");
  file.close();

  Serial.println("SUCCESS");

  Serial.print("Reading test file... ");

  file = SD.open(TEST_FILE, FILE_READ);

  if (!file) {
    Serial.println("FAILED");
    return;
  }

  Serial.println("SUCCESS");
  Serial.println("----------------------------");

  while (file.available()) {
    Serial.write(file.read());
  }

  file.close();

  Serial.println("----------------------------");
  Serial.println("SD CARD TEST COMPLETE");
}

void loop() {
  // Diagnostic runs once at startup.
}
