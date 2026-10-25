// NCR Ranger Pip-Boy / Adafruit Metro ESP32-S3
// Native ESP-NOW helmet link, authenticated by learned sender MAC (not cryptographic).
// Includes fan controls and TRACKING-linked helmet lenses over ESP-NOW.
// Based on the verified ILI9486 + ADC1 touch + channel 6 Pip-Boy firmware.

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

// Second row: SFX on left; helmet fan on right.
const int SFX_X = 20;
const int SFX_Y = 185;
const int SFX_W = 205;
const int SFX_H = 65;

const int FAN_X = 255;
const int FAN_Y = 185;
const int FAN_W = 205;
const int FAN_H = 65;

bool trackingActive = false;
bool cannonLightActive = false;

// Dynamic helmet link; the other two nodes are not connected yet.
bool helmetOnline = false;
bool cannonOnline = false;
bool audioOnline = false;

bool helmetFanOn = false;
bool fanStateKnown = false;
bool fanCommandPending = false;
bool requestedFanOn = false;
unsigned long fanCommandSentAt = 0;

// Helmet lens outputs follow TRACKING mode, never a separate UI button.
// trackingActive is the desired mode; the Feather ACK confirms the hardware state.
bool helmetLensesOn = false;
bool lensStateKnown = false;
bool lensCommandPending = false;
bool requestedLensesOn = false;
bool lensSyncRequired = true;   // Reconcile lenses OFF after initial connection.
unsigned long lensCommandSentAt = 0;
uint8_t lensCommandAttempts = 0;
const uint8_t MAX_LENS_ATTEMPTS = 3;

// handleTouch() precedes radio implementation below.
void sendHelmetFanCommand(bool turnOn);

bool touchWasDown = false;
uint8_t releaseSamples = 0;
unsigned long lastTouchRead = 0;

const uint8_t ESPNOW_CHANNEL = 6;
bool radioReady = false;

// ======================================================
// AUDIO NODE CONFIGURATION
// ======================================================

const uint8_t AUDIO_MAC[6] = {
    0xC8, 0x2E, 0x18, 0x5F, 0x00, 0x6C
};

bool audioPeerReady = false;
unsigned long lastAudioPing = 0;
unsigned long lastAudioPong = 0;

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

void drawFanButton() {
    if (!helmetOnline) {
        drawButton(FAN_X, FAN_Y, FAN_W, FAN_H, "FAN: OFFLINE", NCR_GRAY);
    } else if (fanCommandPending) {
        drawButton(FAN_X, FAN_Y, FAN_W, FAN_H, "HELMET FAN: ...", NCR_AMBER);
    } else if (!fanStateKnown) {
        drawButton(FAN_X, FAN_Y, FAN_W, FAN_H, "HELMET FAN: ?", NCR_GREEN);
    } else {
        drawButton(FAN_X, FAN_Y, FAN_W, FAN_H,
                   helmetFanOn ? "HELMET FAN: ON" : "HELMET FAN: OFF",
                   helmetFanOn ? NCR_AMBER : NCR_GREEN);
    }
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
    drawFanButton();

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

void setTrackingMode(bool enabled) {
    if (trackingActive == enabled) return;
    trackingActive = enabled;
    lensSyncRequired = true;
    lensCommandAttempts = 0;
    Serial.println(trackingActive ?
                   "EVENT: TRACKING_START" : "EVENT: TRACKING_STOP");
    drawScreen();
}

void handleTouch(int x, int y) {
    Serial.printf("TOUCH: X=%d Y=%d\n", x, y);

    if (insideButton(x, y, TRACK_X, TRACK_Y, TRACK_W, TRACK_H)) {
        setTrackingMode(!trackingActive);
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
    if (insideButton(x, y, FAN_X, FAN_Y, FAN_W, FAN_H)) {
        // If status is unknown, request ON and await acknowledgement.
        sendHelmetFanCommand(fanStateKnown ? !helmetFanOn : true);
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


// =====================================================
// DIRECT ESP-NOW HELMET LINK
// =====================================================

const uint8_t HELMET_BROADCAST[6] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

bool helmetProbeReady = false;
bool helmetPeerReady = false;
bool helmetMacKnown = false;
uint8_t helmetMac[6] = {0};
unsigned long lastHelmetPing = 0;
unsigned long lastHelmetPong = 0;

// ESPNOW callbacks execute on the Wi-Fi task. Only copy data here;
// peer registration, drawing, printing, and state updates happen in loop().
portMUX_TYPE helmetRxMux = portMUX_INITIALIZER_UNLOCKED;
bool rxPongPending = false;
uint8_t rxPongMac[6] = {0};
// Audio node response mailbox
bool rxAudioPongPending = false;
uint8_t rxAudioPongMac[6] = {0};
uint8_t rxFanReply = 0;
uint8_t rxFanMac[6] = {0};
// Fan reply: 1=FAN_ON_OK, 2=FAN_OFF_OK, 3=FAN_ON, 4=FAN_OFF
uint8_t rxLensReply = 0;
uint8_t rxLensMac[6] = {0};
// Lens reply: 1=LENS_ON_OK, 2=LENS_OFF_OK; 3/4=LENS_ON/LENS_OFF.

void onHelmetReceive(const esp_now_recv_info_t *info,
                     const uint8_t *data, int len) {
    if (info == nullptr || info->src_addr == nullptr || data == nullptr)
        return;
    // ======================================================
// AUDIO NODE DISCOVERY
// ======================================================

if (len == (int)sizeof("AUDIO_PONG") &&
    memcmp(data, "AUDIO_PONG", sizeof("AUDIO_PONG")) == 0) {

    // Only accept messages from our known audio ESP32.
    if (memcmp(info->src_addr, AUDIO_MAC, 6) != 0)
        return;

    portENTER_CRITICAL(&helmetRxMux);

    memcpy(rxAudioPongMac, info->src_addr, 6);
    rxAudioPongPending = true;

    portEXIT_CRITICAL(&helmetRxMux);

    return;
}                   
    if (len >= (int)sizeof("PONG") &&
        memcmp(data, "PONG", sizeof("PONG")) == 0) {
        portENTER_CRITICAL(&helmetRxMux);
        memcpy(rxPongMac, info->src_addr, 6);
        rxPongPending = true;
        portEXIT_CRITICAL(&helmetRxMux);
        return;
    }

    uint8_t reply = 0;
    if (len >= (int)sizeof("FAN_ON_OK") &&
        memcmp(data, "FAN_ON_OK", sizeof("FAN_ON_OK")) == 0) reply = 1;
    else if (len >= (int)sizeof("FAN_OFF_OK") &&
             memcmp(data, "FAN_OFF_OK", sizeof("FAN_OFF_OK")) == 0) reply = 2;
    else if (len >= (int)sizeof("FAN_ON") &&
             memcmp(data, "FAN_ON", sizeof("FAN_ON")) == 0) reply = 3;
    else if (len >= (int)sizeof("FAN_OFF") &&
             memcmp(data, "FAN_OFF", sizeof("FAN_OFF")) == 0) reply = 4;

    if (reply != 0) {
        portENTER_CRITICAL(&helmetRxMux);
        memcpy(rxFanMac, info->src_addr, 6);
        rxFanReply = reply;
        portEXIT_CRITICAL(&helmetRxMux);
        return;
    }

    uint8_t lensReply = 0;
    if (len >= (int)sizeof("LENS_ON_OK") &&
        memcmp(data, "LENS_ON_OK", sizeof("LENS_ON_OK")) == 0) lensReply = 1;
    else if (len >= (int)sizeof("LENS_OFF_OK") &&
             memcmp(data, "LENS_OFF_OK", sizeof("LENS_OFF_OK")) == 0) lensReply = 2;
    else if (len >= (int)sizeof("LENS_ON") &&
             memcmp(data, "LENS_ON", sizeof("LENS_ON")) == 0) lensReply = 3;
    else if (len >= (int)sizeof("LENS_OFF") &&
             memcmp(data, "LENS_OFF", sizeof("LENS_OFF")) == 0) lensReply = 4;

    if (lensReply != 0) {
        portENTER_CRITICAL(&helmetRxMux);
        memcpy(rxLensMac, info->src_addr, 6);
        rxLensReply = lensReply;
        portEXIT_CRITICAL(&helmetRxMux);
    }
}

void startHelmetProbe() {
    if (!radioReady) {
        Serial.println("HELMET: RADIO NOT READY");
        return;
    }
    esp_err_t result = esp_now_register_recv_cb(onHelmetReceive);
    if (result != ESP_OK) {
        Serial.printf("HELMET RX ERROR: %d\n", (int)result);
        return;
    }

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, HELMET_BROADCAST, 6);
    peer.channel = ESPNOW_CHANNEL;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;

    result = esp_now_add_peer(&peer);
    if (result != ESP_OK && result != ESP_ERR_ESPNOW_EXIST) {
        Serial.printf("HELMET BROADCAST PEER ERROR: %d\n", (int)result);
        return;
    }
    helmetProbeReady = true;
    Serial.println("HELMET: PROBE READY");
}

bool registerHelmetPeer() {
    if (!helmetMacKnown) return false;
    if (esp_now_is_peer_exist(helmetMac)) return true;
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, helmetMac, 6);
    peer.channel = ESPNOW_CHANNEL;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    esp_err_t result = esp_now_add_peer(&peer);
    if (result != ESP_OK && result != ESP_ERR_ESPNOW_EXIST) {
        Serial.printf("HELMET UNICAST PEER ERROR: %d\n", (int)result);
        return false;
    }
    Serial.printf("HELMET MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  helmetMac[0], helmetMac[1], helmetMac[2],
                  helmetMac[3], helmetMac[4], helmetMac[5]);
    Serial.println("HELMET: UNICAST READY");
    return true;
}

void queryHelmetFanStatus() {
    if (!helmetOnline || !helmetPeerReady) return;
    const char command[] = "STATUS";
    esp_err_t result = esp_now_send(helmetMac,
                      (const uint8_t *)command, sizeof(command));
    Serial.printf("METRO -> HELMET: STATUS %s\n",
                  result == ESP_OK ? "QUEUED" : "FAILED");
}

void sendHelmetFanCommand(bool turnOn) {
    if (!radioReady || !helmetProbeReady ||
        !helmetOnline || !helmetPeerReady) {
        Serial.println("FAN: HELMET NOT CONNECTED - NOT SENT");
        return;
    }
    if (fanCommandPending) {
        Serial.println("FAN: WAITING FOR PRIOR ACK");
        return;
    }

    const char *command = turnOn ? "FAN_ON" : "FAN_OFF";
    // Set expectation before queueing so a fast acknowledgement is captured.
    fanCommandPending = true;
    requestedFanOn = turnOn;
    fanCommandSentAt = millis();
    drawFanButton();

    esp_err_t result = esp_now_send(
        helmetMac, (const uint8_t *)command, strlen(command) + 1);

    if (result != ESP_OK) {
        fanCommandPending = false;
        drawFanButton();
        Serial.printf("FAN: TRANSMIT ERROR %d\n", (int)result);
    } else {
        Serial.printf("METRO -> HELMET %s (QUEUED, UNICAST)\n", command);
    }
}

// Called only to enforce the requested TRACKING state.
// The serial 2/3 shortcuts also set TRACKING instead of overriding lenses.
void sendHelmetLensCommand(bool turnOn) {
    if (!radioReady || !helmetProbeReady ||
        !helmetOnline || !helmetPeerReady || !helmetMacKnown ||
        lensCommandPending || lensCommandAttempts >= MAX_LENS_ATTEMPTS) return;

    const char *command = turnOn ? "LENS_ON" : "LENS_OFF";
    lensCommandPending = true;
    requestedLensesOn = turnOn;
    lensCommandSentAt = millis();
    lensCommandAttempts++;
    lensSyncRequired = false;

    esp_err_t result = esp_now_send(
        helmetMac, (const uint8_t *)command, strlen(command) + 1);

    if (result != ESP_OK) {
        lensCommandPending = false;
        lensSyncRequired = true;
        Serial.printf("HELMET LENSES: SEND ERROR %d\n", (int)result);
    } else {
        Serial.printf("METRO -> HELMET: %s (QUEUED, UNICAST)\n", command);
    }
}

void updateHelmetRadio() {
    if (!helmetProbeReady) return;
    unsigned long now = millis();

    // Atomically take the callback mailbox.
    bool gotPong;
    uint8_t pongMac[6];
    bool gotAudioPong;
    uint8_t audioPongMac[6];
    uint8_t fanReply;
    uint8_t replyMac[6];
    uint8_t lensReply;
    uint8_t lensReplyMac[6];
    portENTER_CRITICAL(&helmetRxMux);
    gotPong = rxPongPending;
    memcpy(pongMac, rxPongMac, 6);
    rxPongPending = false;
    gotAudioPong = rxAudioPongPending;
    memcpy(audioPongMac, rxAudioPongMac, 6);
    rxAudioPongPending = false;
    fanReply = rxFanReply;
    memcpy(replyMac, rxFanMac, 6);
    rxFanReply = 0;
    lensReply = rxLensReply;
    memcpy(lensReplyMac, rxLensMac, 6);
    rxLensReply = 0;
    portEXIT_CRITICAL(&helmetRxMux);
    // ======================================================
// AUDIO NODE STATUS
// ======================================================

if (gotAudioPong &&
    memcmp(audioPongMac, AUDIO_MAC, 6) == 0) {

    lastAudioPong = now;

    if (!audioOnline) {
        audioOnline = true;

        Serial.println("AUDIO <- AUDIO_PONG: ONLINE");
        drawNodeStatus(350, "AUDIO", true);
    }
}

    if (gotPong) {
        if (!helmetMacKnown) {
            memcpy(helmetMac, pongMac, 6);
            helmetMacKnown = true;
            helmetPeerReady = registerHelmetPeer();
        }
        // Ignore responses from unrelated ESP-NOW devices.
        if (memcmp(pongMac, helmetMac, 6) == 0) {
            lastHelmetPong = now;
            if (!helmetOnline) {
                helmetOnline = true;
                fanStateKnown = false; // Feather may have rebooted.
                drawNodeStatus(20, "HELMET", true);
                drawFanButton();
                Serial.println("HELMET <- PONG: ONLINE");
                if (!helmetPeerReady) helmetPeerReady = registerHelmetPeer();
                queryHelmetFanStatus();
                // On boot/reconnect, push desired tracking state to Feather.
                // This also switches stray lenses OFF if TRACKING is OFF.
                lensCommandPending = false;
                lensStateKnown = false;
                lensSyncRequired = true;
                lensCommandAttempts = 0;
            }
        }
    }

    if (fanReply != 0 && helmetOnline && helmetMacKnown &&
        memcmp(replyMac, helmetMac, 6) == 0) {
        if (fanReply == 1 || fanReply == 2) {
            bool reportedOn = (fanReply == 1);
            if (fanCommandPending && reportedOn == requestedFanOn) {
                fanCommandPending = false;
                helmetFanOn = reportedOn;
                fanStateKnown = true;
                drawFanButton();
                Serial.printf("HELMET ACK: FAN_%s_OK\n",
                              reportedOn ? "ON" : "OFF");
            } else {
                Serial.println("HELMET: OUT-OF-ORDER FAN ACK IGNORED");
            }
        } else if (!fanCommandPending) {
            // FAN_ON/FAN_OFF are replies to the STATUS query.
            helmetFanOn = (fanReply == 3);
            fanStateKnown = true;
            drawFanButton();
            Serial.printf("HELMET STATUS: FAN_%s\n",
                          helmetFanOn ? "ON" : "OFF");
        }
    }

    if (lensReply != 0 && helmetOnline && helmetMacKnown &&
        memcmp(lensReplyMac, helmetMac, 6) == 0) {
        if (lensReply == 1 || lensReply == 2) {
            bool reportedOn = (lensReply == 1);
            if (lensCommandPending && reportedOn == requestedLensesOn) {
                lensCommandPending = false;
                lensStateKnown = true;
                helmetLensesOn = reportedOn;
                Serial.printf("HELMET ACK: LENS_%s_OK\n",
                              reportedOn ? "ON" : "OFF");
                if (reportedOn != trackingActive) {
                    // User changed tracking mode while previous command was in flight.
                    lensSyncRequired = true;
                    lensCommandAttempts = 0;
                }
            } else {
                Serial.println("HELMET: OUT-OF-ORDER LENS ACK IGNORED");
            }
        }
    }

    // Retry missing ACKs (up to three attempts) rather than assume success.
    if (lensCommandPending && now - lensCommandSentAt >= 1500) {
        lensCommandPending = false;
        lensStateKnown = false;
        lensSyncRequired = (lensCommandAttempts < MAX_LENS_ATTEMPTS);
        Serial.println(lensSyncRequired ?
                       "HELMET: LENS ACK TIMEOUT - RETRYING" :
                       "HELMET: LENS ACK TIMEOUT - CHECK LENSES");
    }

    if (helmetOnline && helmetPeerReady && lensSyncRequired &&
        !lensCommandPending && lensCommandAttempts < MAX_LENS_ATTEMPTS) {
        sendHelmetLensCommand(trackingActive);
    }

    if (now - lastHelmetPing >= 2000) {
        lastHelmetPing = now;
        const char ping[] = "PING";
        esp_err_t result = esp_now_send(
            HELMET_BROADCAST, (const uint8_t *)ping, sizeof(ping));
        if (result != ESP_OK)
            Serial.printf("HELMET PING SEND ERROR: %d\n", (int)result);
    }

    if (fanCommandPending && now - fanCommandSentAt >= 1500) {
        fanCommandPending = false;
        fanStateKnown = false; // Sent does not equal acknowledged.
        drawFanButton();
        Serial.println("HELMET: FAN ACK TIMEOUT - STATE UNKNOWN");
        queryHelmetFanStatus();
    }

    if (helmetOnline && now - lastHelmetPong > 6000) {
        helmetOnline = false;
        fanCommandPending = false;
        fanStateKnown = false;
        lensCommandPending = false;
        lensStateKnown = false;
        lensSyncRequired = true;
        drawNodeStatus(20, "HELMET", false);
        drawFanButton();
        Serial.println("HELMET: OFFLINE");
    }
    // Audio node timeout
if (audioOnline && now - lastAudioPong > 6000) {
    audioOnline = false;

    drawNodeStatus(350, "AUDIO", false);
    Serial.println("AUDIO: OFFLINE");
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

    // Serial Monitor: S=status, 1/0=fan, 2/3=TRACKING on/off.
    if (Serial.available() > 0) {
        char key = Serial.read();
        if (key == 's' || key == 'S') {
            Serial.println("=== NCR RADIO STATUS ===");
            Serial.printf("ESP-NOW: %s\n", radioReady ? "ONLINE" : "OFFLINE");
            uint8_t channel = 0;
            wifi_second_chan_t secondary = WIFI_SECOND_CHAN_NONE;
            if (radioReady &&
                esp_wifi_get_channel(&channel, &secondary) == ESP_OK)
                Serial.printf("CHANNEL: %u\n", (unsigned)channel);
            else
                Serial.println("CHANNEL: UNAVAILABLE");
            Serial.print("MAC: ");
            Serial.println(WiFi.macAddress());
            Serial.printf("HELMET: %s\n", helmetOnline ? "ONLINE" : "OFFLINE");
            Serial.printf("FAN: %s\n", fanCommandPending ? "WAITING FOR ACK" :
                          (fanStateKnown ? (helmetFanOn ? "ON" : "OFF") : "UNKNOWN"));
            Serial.printf("TRACKING: %s\n", trackingActive ? "ON" : "OFF");
            Serial.printf("LENSES: %s\n", lensCommandPending ? "WAITING FOR ACK" :
                          (lensStateKnown ? (helmetLensesOn ? "ON" : "OFF") : "UNKNOWN"));
            Serial.println("========================");
        } else if (key == '1') {
            sendHelmetFanCommand(true);
        } else if (key == '0') {
            sendHelmetFanCommand(false);
        } else if (key == '2') {
            setTrackingMode(true);   // Tracking ON -> lenses ON
        } else if (key == '3') {
            setTrackingMode(false);  // Tracking OFF -> lenses OFF
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
