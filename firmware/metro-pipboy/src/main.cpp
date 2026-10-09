// NCR Ranger Pip-Boy -- Metro ESP32-S3 recovered working baseline.
// Reconstructed from the validated sketch and subsequent changes in chat.
// This baseline includes display, reversed ADC1 touch, three UI event buttons,
// and ESP-NOW initialization on channel 6. Direct helmet link is NOT included.

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <TouchScreen.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#define NCR_BLACK       0x0000
#define NCR_GREEN       0x07E0
#define NCR_DARK_GREEN  0x0320
#define NCR_AMBER       0xFD20
#define NCR_GRAY        0x8410

// Confirmed working Metro ESP32-S3 + original 3.5-inch ILI9486 LCD shield.
Arduino_DataBus *bus = new Arduino_SWPAR8(
    A2, A3, A1, A0,
    8, 9, 2, 3, 4, 5, 6, 7
);
Arduino_GFX *gfx = new Arduino_ILI9486(bus, A4, 1, false);

// Original physical touchscreen wiring on the parallel TFT shield.
#define XP 8
#define YM 9
#define XM A2
#define YP A3

// Reversed electrodes, as tested: use D8/D9 (ADC1) for touch reads.
// Leave these in this order -- the standard constructor conflicted with Wi-Fi.
TouchScreen ts(XM, YM, XP, YP, 300);

// Starting calibration from the successful reversed-electrode test.
#define TS_LEFT   141
#define TS_RIGHT  774
#define TS_TOP    241
#define TS_BOTTOM 573

#define MINPRESSURE 80
#define MAXPRESSURE 2000

// Original UNO layout in 480x320 landscape coordinates.
const int TRACK_X = 20;
const int TRACK_Y = 80;
const int TRACK_W = 205;
const int TRACK_H = 75;

const int LIGHT_X = 255;
const int LIGHT_Y = 80;
const int LIGHT_W = 205;
const int LIGHT_H = 75;

const int SFX_X = 80;
const int SFX_Y = 185;
const int SFX_W = 320;
const int SFX_H = 65;

bool trackingActive = false;
bool cannonLightActive = false;

// Link logic intentionally NOT enabled yet; these are truthful placeholders.
bool helmetOnline = false;
bool cannonOnline = false;
bool audioOnline = false;

bool touchWasDown = false;
uint8_t releaseSamples = 0;
unsigned long lastTouchRead = 0;

const uint8_t ESPNOW_CHANNEL = 6;
bool radioReady = false;

void drawButton(int x, int y, int w, int h,
                const char *label, uint16_t color) {
    gfx->fillRect(x, y, w, h, NCR_BLACK);
    gfx->drawRect(x, y, w, h, color);
    gfx->drawRect(x + 3, y + 3, w - 6, h - 6, color);
    gfx->setTextColor(color);
    gfx->setTextSize(2);
    gfx->setCursor(x + 15, y + 27);
    gfx->print(label);
}

void drawNodeStatus(int x, const char *label, bool online) {
    gfx->fillRect(x, 275, 110, 42, NCR_BLACK);
    gfx->setTextSize(1);
    gfx->setTextColor(NCR_GRAY);
    gfx->setCursor(x, 285);
    gfx->print(label);
    gfx->setCursor(x, 300);
    gfx->setTextColor(online ? NCR_GREEN : NCR_AMBER);
    gfx->print(online ? "ONLINE" : "OFFLINE");
}

void drawScreen() {
    gfx->fillScreen(NCR_BLACK);
    gfx->fillRect(0, 0, 480, 45, NCR_DARK_GREEN);
    gfx->setTextColor(NCR_GREEN);
    gfx->setTextSize(2);
    gfx->setCursor(15, 14);
    gfx->print("NCR RANGER // CONTROL");
    gfx->setTextSize(1);
    gfx->setCursor(15, 58);
    gfx->print("RANGER SYSTEM BUS // LOCAL CONTROL");

    drawButton(TRACK_X, TRACK_Y, TRACK_W, TRACK_H,
               trackingActive ? "TRACKING: ON" : "TRACKING: OFF",
               trackingActive ? NCR_AMBER : NCR_GREEN);
    drawButton(LIGHT_X, LIGHT_Y, LIGHT_W, LIGHT_H,
               cannonLightActive ? "LIGHT: ON" : "LIGHT: OFF",
               cannonLightActive ? NCR_AMBER : NCR_GREEN);
    drawButton(SFX_X, SFX_Y, SFX_W, SFX_H,
               "PLAY TEST SFX", NCR_GREEN);

    drawNodeStatus(20, "HELMET", helmetOnline);
    drawNodeStatus(180, "CANNON", cannonOnline);
    drawNodeStatus(350, "AUDIO", audioOnline);
}

void restoreLCDPins() {
    pinMode(XP, OUTPUT);
    pinMode(YM, OUTPUT);
    pinMode(XM, OUTPUT);
    pinMode(YP, OUTPUT);
    digitalWrite(A3, HIGH);  // TFT CS idle
    digitalWrite(A1, HIGH);  // TFT WR idle
    digitalWrite(A0, HIGH);  // TFT RD idle
}

bool readTouch(int &screenX, int &screenY) {
    digitalWrite(A3, HIGH);
    digitalWrite(A1, HIGH);
    digitalWrite(A0, HIGH);

    TSPoint p = ts.getPoint();
    restoreLCDPins();

    if (p.z < MINPRESSURE || p.z > MAXPRESSURE) return false;

    screenX = constrain(map(p.y, TS_LEFT, TS_RIGHT, 45, 435), 0, 479);
    screenY = constrain(map(p.x, TS_TOP, TS_BOTTOM, 70, 210), 0, 319);
    return true;
}

bool insideButton(int px, int py,
                  int x, int y, int w, int h) {
    return (px >= x && px < x + w && py >= y && py < y + h);
}

void handleTouch(int x, int y) {
    Serial.printf("TOUCH: X=%d Y=%d\n", x, y);

    if (insideButton(x, y, TRACK_X, TRACK_Y, TRACK_W, TRACK_H)) {
        trackingActive = !trackingActive;
        Serial.println(trackingActive ?
                       "EVENT: TRACKING_START" : "EVENT: TRACKING_STOP");
        drawScreen();
        return;
    }
    if (insideButton(x, y, LIGHT_X, LIGHT_Y, LIGHT_W, LIGHT_H)) {
        cannonLightActive = !cannonLightActive;
        Serial.println(cannonLightActive ?
                       "EVENT: CANNON_LIGHT_ON" : "EVENT: CANNON_LIGHT_OFF");
        drawScreen();
        return;
    }
    if (insideButton(x, y, SFX_X, SFX_Y, SFX_W, SFX_H)) {
        Serial.println("EVENT: SFX_TEST");
        return;
    }
}

void initRadio() {
    Serial.println("RADIO: STARTING");

    if (!WiFi.mode(WIFI_STA)) {
        Serial.println("RADIO: WIFI START FAILED");
        return;
    }

    esp_err_t channelResult = esp_wifi_set_channel(
        ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

    if (channelResult != ESP_OK) {
        Serial.printf("RADIO: CHANNEL ERROR %d\n", (int)channelResult);
        return;
    }

    esp_err_t nowResult = esp_now_init();
    if (nowResult != ESP_OK) {
        Serial.printf("RADIO: ESP-NOW ERROR %d\n", (int)nowResult);
        return;
    }

    radioReady = true;
    uint8_t actualChannel = 0;
    wifi_second_chan_t secondary = WIFI_SECOND_CHAN_NONE;
    esp_err_t readResult = esp_wifi_get_channel(&actualChannel, &secondary);

    Serial.println("------------------------");
    Serial.println("ESP-NOW: ONLINE");
    Serial.print("MAC: ");
    Serial.println(WiFi.macAddress());

    if (readResult == ESP_OK) {
        Serial.printf("CHANNEL: %u\n", (unsigned)actualChannel);
    } else {
        Serial.println("CHANNEL: READ FAILED");
    }
    Serial.println("------------------------");
}


//
// DIRECT HELMET ESP-NOW LINK
//

const uint8_t HELMET_BROADCAST[6] = {
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

volatile bool helmetPongPending = false;
bool helmetProbeReady = false;

unsigned long lastHelmetPing = 0;
unsigned long lastHelmetPong = 0;

// Called by the ESP-NOW Wi-Fi task.
// Keep this callback short.
void onHelmetReceive(
  const esp_now_recv_info_t *info,
  const uint8_t *data,
  int len
) {
  if (data != nullptr &&
      len >= 4 &&
      memcmp(data, "PONG", 4) == 0) {

    helmetPongPending = true;
  }
}

void startHelmetProbe() {

  if (!radioReady) {
    Serial.println("HELMET: RADIO NOT READY");
    return;
  }

  esp_err_t result =
    esp_now_register_recv_cb(onHelmetReceive);

  if (result != ESP_OK) {
    Serial.printf("HELMET RX ERROR: %d\n", (int)result);
    return;
  }

  esp_now_peer_info_t peer = {};

  memcpy(
    peer.peer_addr,
    HELMET_BROADCAST,
    6
  );

  peer.channel = ESPNOW_CHANNEL;
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;

  result = esp_now_add_peer(&peer);

  if (result != ESP_OK &&
      result != ESP_ERR_ESPNOW_EXIST) {
    Serial.printf("HELMET PEER ERROR: %d\n", (int)result);
    return;
  }

  helmetProbeReady = true;
  Serial.println("HELMET: PROBE READY");
}

void updateHelmetRadio() {

  if (!helmetProbeReady) return;

  unsigned long now = millis();

  // Process received response outside callback.
  if (helmetPongPending) {

    helmetPongPending = false;
    lastHelmetPong = now;

    Serial.println("HELMET <- PONG");

    if (!helmetOnline) {
      helmetOnline = true;

      drawNodeStatus(20, "HELMET", true);
      Serial.println("HELMET: ONLINE");
    }
  }

  // Send PING every 2 seconds.
  if (now - lastHelmetPing >= 2000) {

    lastHelmetPing = now;

    const char message[] = "PING";

    esp_err_t result = esp_now_send(
      HELMET_BROADCAST,
      (const uint8_t *)message,
      sizeof(message)
    );

    Serial.printf(
      "METRO -> HELMET PING: %d\n",
      (int)result
    );
  }

  // Mark offline after 6 seconds without PONG.
  if (helmetOnline &&
      now - lastHelmetPong > 6000) {

    helmetOnline = false;

    drawNodeStatus(20, "HELMET", false);
    Serial.println("HELMET: OFFLINE");
  }
}


void setup() {
    Serial.begin(115200);
    analogReadResolution(10);
    delay(800);

    Serial.println("==========================");
    Serial.println("NCR RANGER PIP-BOY");
    Serial.println("METRO ESP32-S3");
    Serial.println("==========================");

    if (!gfx->begin()) {
        Serial.println("DISPLAY INIT FAILED");
        while (true) delay(1000);
    }

    drawScreen();
    Serial.println("DISPLAY: ONLINE");
    Serial.println("TOUCH: READY");
    initRadio();
    startHelmetProbe();
    Serial.println("SYSTEM: READY");
}


void loop() {
    updateHelmetRadio();
    // Press S in Serial Monitor for live radio status.
    if (Serial.available() > 0) {
        char command = Serial.read();

        if (command == 's' || command == 'S') {
            Serial.println();
            Serial.println("=== NCR RADIO STATUS ===");

            Serial.printf("ESP-NOW: %s\n",
                radioReady ? "ONLINE" : "OFFLINE");

            uint8_t channel = 0;
            wifi_second_chan_t secondary = WIFI_SECOND_CHAN_NONE;

            if (radioReady &&
                esp_wifi_get_channel(&channel, &secondary) == ESP_OK) {
                Serial.printf("CHANNEL: %u\n", (unsigned)channel);
            } else {
                Serial.println("CHANNEL: UNAVAILABLE");
            }

            Serial.print("MAC: ");
            Serial.println(WiFi.macAddress());

            Serial.printf(
            "HELMET: %s\n",
            helmetOnline ? "ONLINE" : "OFFLINE"
            );
            Serial.println("========================");
        }
    }

    if (millis() - lastTouchRead < 40) return;

    lastTouchRead = millis();

    int touchX = 0;
    int touchY = 0;
    bool down = readTouch(touchX, touchY);

    if (down) {
        releaseSamples = 0;
        if (!touchWasDown) {
            touchWasDown = true;
            handleTouch(touchX, touchY);
        }
    } else {
        if (++releaseSamples >= 3) {
            touchWasDown = false;
            releaseSamples = 3;
        }
    }
}
