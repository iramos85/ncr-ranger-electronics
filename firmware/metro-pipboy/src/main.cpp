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
#include <time.h>
#include <sys/time.h>

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
constexpr uint8_t SCREEN_ROTATION = 3;
constexpr int SCREEN_WIDTH = 480;
constexpr int SCREEN_HEIGHT = 320;
Arduino_GFX *gfx = new Arduino_ILI9486(bus, A4, SCREEN_ROTATION, false);

// Original physical touchscreen wiring on the parallel TFT shield.
#define XP 8
#define YM 9
#define XM A2
#define YP A3

// Reversed electrodes, as tested: use D8/D9 (ADC1) for touch reads.
// Leave these in this order -- the standard constructor conflicted with Wi-Fi.
TouchScreen ts(XM, YM, XP, YP, 300);

// Calibrated from four physical touchscreen samples.
// Includes an estimated margin for touches inside the edges.
constexpr int TS_RAW_LEFT   = 865;
constexpr int TS_RAW_RIGHT  = 157;
constexpr int TS_RAW_TOP    = 829;
constexpr int TS_RAW_BOTTOM = 223;

// Approximate on-screen positions of calibration touches.
constexpr int CAL_X_LEFT   = 35;
constexpr int CAL_X_RIGHT  = 445;
constexpr int CAL_Y_TOP    = 30;
constexpr int CAL_Y_BOTTOM = 290;

#define MINPRESSURE 80
#define MAXPRESSURE 2000

// All interactive areas are within the existing calibrated touch Y span.
// The bottom status strip is display-only.
enum ScreenPage : uint8_t { PAGE_HOME, PAGE_HELMET, PAGE_CANNON, PAGE_AUDIO };
ScreenPage currentPage = PAGE_HOME;
enum AudioPage : uint8_t {
    AUDIO_SFX,
    AUDIO_RADIO
};

AudioPage currentAudioPage = AUDIO_SFX;

// Display state is updated only by verified audio-node replies.
uint8_t radioVolume = 50;
bool radioVolumeKnown = false;
bool radioPlaying = false;
bool radioStateKnown = false;
uint8_t radioTrack = 1;
bool radioTrackKnown = false;
uint8_t radioTrackCount = 43;
bool radioPaused = false;
bool ambientKnown = false;
bool ambientEnabled = false;
unsigned long ambientQueryAt = 0;
void sendAudioFxCommand(const char *command);
unsigned long radioVolumeQueryAt = 0;
const int TAB_Y = 82, TAB_H = 36, TAB_W = 110;
const int TAB_X[4] = {8, 126, 244, 362};
const int ACTION_X = 20, ACTION_Y = 143, ACTION_W = 440, ACTION_H = 60;
const int HALF_W = 209;
const int TRACK_X = 20, TRACK_Y = 143, TRACK_W = HALF_W, TRACK_H = 60;
const int FAN_X = 251, FAN_Y = 143, FAN_W = HALF_W, FAN_H = 60;
const int LIGHT_X = ACTION_X, LIGHT_Y = ACTION_Y, LIGHT_W = ACTION_W, LIGHT_H = ACTION_H;
const int SFX_X = ACTION_X, SFX_Y = ACTION_Y, SFX_W = ACTION_W, SFX_H = ACTION_H;
bool clockSet = false;  // No RTC/NTP source: time must be set after boot.
unsigned long lastHomeClockDraw = 0;

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
void sendAudioTestCommand();
void sendAudioMusicCommand(const char *command);

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
    gfx->drawRect(x + 2, y + 2, w - 4, h - 4, color);
    gfx->setTextColor(color);
    gfx->setTextSize(2);
    // Text uses the small default 12px character advance at size 2.
    int textW = (int)strlen(label) * 12;
    gfx->setCursor(x + max(6, (w - textW) / 2), y + (h - 16) / 2);
    gfx->print(label);
}

void drawNodeStatus(int x, const char *label, bool online) {
    gfx->fillRect(x, 271, 112, 45, NCR_BLACK);
    gfx->setTextSize(1);
    gfx->setTextColor(NCR_GRAY);
    gfx->setCursor(x, 278);
    gfx->print(label);
    gfx->setCursor(x, 296);
    gfx->setTextColor(online ? NCR_GREEN : NCR_AMBER);
    gfx->print(online ? "ONLINE" : "OFFLINE");
}

void drawFanButton() {
    if (currentPage != PAGE_HELMET) return;
    if (!helmetOnline) {
        drawButton(FAN_X, FAN_Y, FAN_W, FAN_H, "FAN: OFFLINE", NCR_GRAY);
    } else if (fanCommandPending) {
        drawButton(FAN_X, FAN_Y, FAN_W, FAN_H, "FAN: WAIT...", NCR_AMBER);
    } else if (!fanStateKnown) {
        drawButton(FAN_X, FAN_Y, FAN_W, FAN_H, "FAN: UNKNOWN", NCR_GREEN);
    } else {
        drawButton(FAN_X, FAN_Y, FAN_W, FAN_H,
                   helmetFanOn ? "FAN: ON" : "FAN: OFF",
                   helmetFanOn ? NCR_AMBER : NCR_GREEN);
    }
}

// HOME V2: status console, sized for 480x320 landscape.
// Keep touch hitboxes and the entire ESP-NOW implementation unchanged.
void drawHomeClock() {
    if (currentPage != PAGE_HOME) return;

    // Redraw only the dashboard content, not the navigation or node footer.
    gfx->fillRect(12, 127, 456, 128, NCR_BLACK);
    gfx->drawRect(12, 128, 456, 125, NCR_DARK_GREEN);
    gfx->drawFastVLine(240, 134, 67, NCR_DARK_GREEN);
    gfx->drawFastHLine(18, 205, 444, NCR_DARK_GREEN);

    gfx->setTextSize(1);
    gfx->setTextColor(NCR_GREEN);
    gfx->setCursor(20, 137);
    gfx->print("LOCAL / CENTRAL TIME");
    gfx->setCursor(252, 137);
    gfx->print("MISSION UPTIME");

    if (clockSet) {
        time_t now = time(nullptr);
        struct tm localNow;
        localtime_r(&now, &localNow);
        char dateBuf[26], timeBuf[20];
        strftime(dateBuf, sizeof(dateBuf), "%a %b %d, %Y", &localNow);
        strftime(timeBuf, sizeof(timeBuf), "%I:%M:%S %p", &localNow);
        gfx->setTextColor(NCR_GREEN);
        gfx->setTextSize(2);
        gfx->setCursor(20, 156);
        gfx->print(timeBuf);
        gfx->setTextSize(1);
        gfx->setCursor(20, 184);
        gfx->print(dateBuf);
    } else {
        gfx->setTextColor(NCR_AMBER);
        gfx->setTextSize(2);
        gfx->setCursor(20, 156);
        gfx->print("TIME NOT SET");
        gfx->setTextSize(1);
        gfx->setCursor(20, 184);
        gfx->print("SET VIA USB SERIAL");
    }

    unsigned long total = millis() / 1000UL;
    char upBuf[20];
    snprintf(upBuf, sizeof(upBuf), "%02lu:%02lu:%02lu",
             total / 3600UL, (total / 60UL) % 60UL, total % 60UL);
    gfx->setTextSize(2);
    gfx->setTextColor(NCR_GREEN);
    gfx->setCursor(252, 156);
    gfx->print(upBuf);
    gfx->setTextSize(1);
    gfx->setTextColor(NCR_GRAY);
    gfx->setCursor(252, 184);
    gfx->print("METRO ESP32-S3");

    gfx->setTextColor(NCR_GREEN);
    gfx->setCursor(20, 215);
    gfx->print("RADIO BUS:");
    gfx->setTextColor(radioReady ? NCR_GREEN : NCR_AMBER);
    gfx->setCursor(92, 215);
    gfx->print(radioReady ? "ONLINE" : "OFFLINE");
    gfx->setTextColor(NCR_GRAY);
    gfx->setCursor(252, 215);
    gfx->print("ESP-NOW / CH 06");

    char memBuf[32];
    snprintf(memBuf, sizeof(memBuf), "FREE HEAP: %lu KB",
             (unsigned long)(ESP.getFreeHeap() / 1024UL));
    gfx->setCursor(20, 235);
    gfx->print(memBuf);
    gfx->setCursor(252, 235);
    gfx->print("RANGER OS  V2.0");
}

void drawScreen() {
    gfx->fillScreen(NCR_BLACK);
    gfx->fillRect(0, 0, 480, 44, NCR_DARK_GREEN);
    gfx->setTextColor(NCR_GREEN);
    gfx->setTextSize(2);
    gfx->setCursor(12, 14);
    gfx->print("NCR RANGER // CONTROL");
    gfx->setTextSize(1);
    gfx->setCursor(12, 59);
    gfx->print("NCR FIELD SYSTEM / RANGER OS");

    const char *tabs[] = {"HOME", "HELMET", "CANNON", "AUDIO"};
    for (int i = 0; i < 4; i++) {
        const bool active = (int)currentPage == i;
        gfx->fillRect(TAB_X[i], TAB_Y, TAB_W, TAB_H,
                      active ? NCR_DARK_GREEN : NCR_BLACK);
        gfx->drawRect(TAB_X[i], TAB_Y, TAB_W, TAB_H,
                      active ? NCR_AMBER : NCR_GREEN);
        gfx->setTextSize(1);
        gfx->setTextColor(active ? NCR_AMBER : NCR_GREEN);
        gfx->setCursor(TAB_X[i] + 12, TAB_Y + 14);
        gfx->print(tabs[i]);
    }

    if (currentPage == PAGE_HOME) {
        drawHomeClock();
    } else if (currentPage == PAGE_HELMET) {
        drawButton(TRACK_X, TRACK_Y, TRACK_W, TRACK_H,
                   trackingActive ? "TRACKING: ON" : "TRACKING: OFF",
                   trackingActive ? NCR_AMBER : NCR_GREEN);
        drawFanButton();
        gfx->setTextSize(1);
        gfx->setTextColor(NCR_GRAY);
        gfx->setCursor(20, 223);
        gfx->print("TRACKING ALSO CONTROLS HELMET LENSES");
    } else if (currentPage == PAGE_CANNON) {
        drawButton(LIGHT_X, LIGHT_Y, LIGHT_W, LIGHT_H,
                   cannonLightActive ? "LIGHT: ON" : "LIGHT: OFF",
                   cannonLightActive ? NCR_AMBER : NCR_GREEN);
        gfx->setTextSize(1);
        gfx->setTextColor(NCR_GRAY);
        gfx->setCursor(20, 223);
        gfx->print("LIGHT CONTROL: LOCAL STATE ONLY");
} else if (currentPage == PAGE_AUDIO) {
        drawButton(20, 130, 209, 38, "SFX",
                   currentAudioPage == AUDIO_SFX ? NCR_AMBER : NCR_GREEN);
        drawButton(251, 130, 209, 38, "RADIO",
                   currentAudioPage == AUDIO_RADIO ? NCR_AMBER : NCR_GREEN);

        if (currentAudioPage == AUDIO_SFX) {
            // Compact 2x3 tactical grid within the calibrated touch area.
            drawButton(20, 173, 209, 25, "SYSTEM BOOT", NCR_GREEN);
            drawButton(251, 173, 209, 25, "TRACKING", NCR_GREEN);
            drawButton(20, 202, 209, 25, "PLASMA CHARGE", NCR_GREEN);
            drawButton(251, 202, 209, 25, "PLASMA BLAST", NCR_GREEN);
            drawButton(20, 231, 209, 25, "WARNING", NCR_GREEN);
            drawButton(251, 231, 209, 25,
                       !ambientKnown ? "AMBIENT ?" :
                       (ambientEnabled ? "AMBIENT ON" : "AMBIENT OFF"),
                       ambientEnabled ? NCR_AMBER : NCR_GREEN);
        } else {
            gfx->setTextSize(1);
            gfx->setTextColor(NCR_GREEN);
            gfx->setCursor(20, 174);
            gfx->print("RADIO NEW VEGAS");
            gfx->setCursor(260, 174);
            if (radioTrackKnown) {
                gfx->printf("TRACK %02u/%02u", radioTrack, radioTrackCount);
            } else {
                gfx->print("TRACK --/43");
            }
            gfx->setCursor(20, 185);
            gfx->print(radioStateKnown ?
                       (radioPlaying ? "PLAYING" : (radioPaused ? "PAUSED" : "STOPPED")) :
                       "STATE UNKNOWN");
            drawButton(20, 197, 130, 30, "PREV", NCR_GREEN);
            drawButton(175, 197, 130, 30,
                       radioPlaying ? "PAUSE" : "PLAY", NCR_GREEN);
            drawButton(330, 197, 130, 30, "NEXT", NCR_GREEN);
            drawButton(20, 230, 115, 26, "VOL-", NCR_GREEN);
            drawButton(345, 230, 115, 26, "VOL+", NCR_GREEN);
            gfx->setTextSize(1);
            gfx->setTextColor(NCR_AMBER);
            gfx->setCursor(181, 239);
            if (radioVolumeKnown) gfx->printf("VOL %u%%", radioVolume);
            else gfx->print("VOL --");
        }
    }

    gfx->drawFastHLine(12, 262, 456, NCR_DARK_GREEN);
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

    if (p.z >= MINPRESSURE && p.z <= MAXPRESSURE) 
        {Serial.printf("RAW TOUCH: x=%d y=%d z=%d\n",
                  p.x, p.y, p.z);
        }

    if (p.z < MINPRESSURE || p.z > MAXPRESSURE) return false;

  // Raw Y maps to screen X (left to right).
screenX = constrain(
    map(p.y, TS_RAW_LEFT, TS_RAW_RIGHT,
        CAL_X_LEFT, CAL_X_RIGHT),
    0, SCREEN_WIDTH - 1
);

// Raw X maps to screen Y (top to bottom).
screenY = constrain(
    map(p.x, TS_RAW_TOP, TS_RAW_BOTTOM,
        CAL_Y_TOP, CAL_Y_BOTTOM),
    0, SCREEN_HEIGHT - 1
);
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
    // Queue the short acknowledgment sound; helmet link remains independent.
    if (audioOnline) sendAudioFxCommand(enabled ? "FX:TRACKON" : "FX:TRACKOFF");
    drawScreen();
}


void handleTouch(int x, int y) {
    Serial.printf("TOUCH: X=%d Y=%d\n", x, y);
    for (int i = 0; i < 4; i++) {
        if (insideButton(x, y, TAB_X[i], TAB_Y, TAB_W, TAB_H)) {
            if (currentPage != (ScreenPage)i) {
                currentPage = (ScreenPage)i;
                drawScreen();
            }
            return;
        }
    }
    if (currentPage == PAGE_HELMET) {
        if (insideButton(x, y, TRACK_X, TRACK_Y, TRACK_W, TRACK_H)) {
            setTrackingMode(!trackingActive);
        } else if (insideButton(x, y, FAN_X, FAN_Y, FAN_W, FAN_H)) {
            sendHelmetFanCommand(fanStateKnown ? !helmetFanOn : true);
        }
    } else if (currentPage == PAGE_CANNON) {
        if (insideButton(x, y, LIGHT_X, LIGHT_Y, LIGHT_W, LIGHT_H)) {
            cannonLightActive = !cannonLightActive;
            Serial.println(cannonLightActive ?
                           "EVENT: CANNON_LIGHT_ON" : "EVENT: CANNON_LIGHT_OFF");
            drawScreen();
        }
    } else if (currentPage == PAGE_AUDIO) {
        if (insideButton(x, y, 20, 130, 209, 38)) {
            currentAudioPage = AUDIO_SFX;
            drawScreen();
            ambientQueryAt = millis() + 50;
        } else if (insideButton(x, y, 251, 130, 209, 38)) {
            currentAudioPage = AUDIO_RADIO;
            drawScreen();
            sendAudioMusicCommand("MUSIC:STATUS");
            // ESP32 has a one-command mailbox; send the query later.
            radioVolumeQueryAt = millis() + 350;
        } else if (currentAudioPage == AUDIO_SFX) {
            if (insideButton(x, y, 20, 173, 209, 25))
                sendAudioFxCommand("FX:BOOT");
            else if (insideButton(x, y, 251, 173, 209, 25))
                sendAudioFxCommand(trackingActive ? "FX:TRACKON" : "FX:TRACKOFF");
            else if (insideButton(x, y, 20, 202, 209, 25))
                sendAudioFxCommand("FX:CHARGE");
            else if (insideButton(x, y, 251, 202, 209, 25))
                sendAudioFxCommand("FX:PLASMA");
            else if (insideButton(x, y, 20, 231, 209, 25))
                sendAudioFxCommand("FX:WARN");
            else if (insideButton(x, y, 251, 231, 209, 25)) {
                // Query first when state is unknown; never guess the state.
                if (!ambientKnown) sendAudioFxCommand("AMBIENT:STATUS");
                else sendAudioFxCommand(ambientEnabled ? "AMBIENT:OFF" : "AMBIENT:ON");
            }
        } else if (currentAudioPage == AUDIO_RADIO) {
            if (insideButton(x, y, 20, 197, 130, 30))
                sendAudioMusicCommand("MUSIC:PREV");
            else if (insideButton(x, y, 175, 197, 130, 30))
                sendAudioMusicCommand(radioPlaying ? "MUSIC:PAUSE" : "MUSIC:PLAY");
            else if (insideButton(x, y, 330, 197, 130, 30))
                sendAudioMusicCommand("MUSIC:NEXT");
            else if (insideButton(x, y, 20, 230, 115, 26))
                sendAudioMusicCommand("MUSIC:VOLDOWN");
            else if (insideButton(x, y, 345, 230, 115, 26))
                sendAudioMusicCommand("MUSIC:VOLUP");
        }
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
char rxMusicStatus[64] = {};
bool rxMusicStatusReady = false;
char rxAmbientStatus[64] = {};
bool rxAmbientStatusReady = false;
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
    // Only the known audio node may update radio state.
    if (memcmp(info->src_addr, AUDIO_MAC, 6) == 0 &&
        len > 7 && len <= (int)sizeof(rxMusicStatus) &&
        data[len - 1] == '\0' &&
        memcmp(data, "MUSIC:", 6) == 0) {
        portENTER_CRITICAL(&helmetRxMux);
        memcpy(rxMusicStatus, data, len);
        rxMusicStatusReady = true;
        portEXIT_CRITICAL(&helmetRxMux);
        return;
    }
    if (memcmp(info->src_addr, AUDIO_MAC, 6) == 0 &&
        len > 8 && len <= (int)sizeof(rxAmbientStatus) &&
        data[len - 1] == '\0' &&
        memcmp(data, "AMBIENT:", 8) == 0) {
        portENTER_CRITICAL(&helmetRxMux);
        memcpy(rxAmbientStatus, data, len);
        rxAmbientStatusReady = true;
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

// ======================================================
// AUDIO NODE — SEND SFX COMMAND
// ======================================================

void sendAudioTestCommand() {
    if (!radioReady || !audioOnline) {
        Serial.println("SFX: AUDIO NODE OFFLINE");
        return;
    }

    if (!esp_now_is_peer_exist(AUDIO_MAC)) {
        esp_now_peer_info_t peer = {};

        memcpy(peer.peer_addr, AUDIO_MAC, 6);
        peer.channel = ESPNOW_CHANNEL;
        peer.ifidx = WIFI_IF_STA;
        peer.encrypt = false;

        esp_err_t result = esp_now_add_peer(&peer);

        if (result != ESP_OK &&
            result != ESP_ERR_ESPNOW_EXIST) {
            Serial.printf("AUDIO PEER ERROR: %d\n", (int)result);
            return;
        }
    }

    audioPeerReady = true;

    const char command[] = "SFX_TEST";

    esp_err_t result = esp_now_send(
        AUDIO_MAC,
        (const uint8_t*)command,
        sizeof(command)
    );

    Serial.printf(
        "METRO -> AUDIO: SFX_TEST %s\n",
        result == ESP_OK ? "QUEUED" : "FAILED"
    );
}


void sendAudioMusicCommand(const char *command) {
    if (!radioReady || !audioOnline) {
        Serial.println("MUSIC: AUDIO NODE OFFLINE");
        return;
    }
    if (!esp_now_is_peer_exist(AUDIO_MAC)) {
        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, AUDIO_MAC, 6);
        peer.channel = ESPNOW_CHANNEL;
        peer.ifidx = WIFI_IF_STA;
        peer.encrypt = false;
        esp_err_t rc = esp_now_add_peer(&peer);
        if (rc != ESP_OK && rc != ESP_ERR_ESPNOW_EXIST) {
            Serial.printf("MUSIC PEER ERROR: %d\n", (int)rc);
            return;
        }
    }
    const size_t len = strlen(command) + 1;
    const esp_err_t rc = esp_now_send(
        AUDIO_MAC, reinterpret_cast<const uint8_t*>(command), len);
    Serial.printf("METRO -> AUDIO: %s (%s)\n", command,
                  rc == ESP_OK ? "QUEUED" : "FAILED");
}

void sendAudioFxCommand(const char *command) {
    // Reuse existing audio ESP-NOW peer setup and sender logic.
    sendAudioMusicCommand(command);
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
    bool gotMusicStatus;
    char musicStatus[64] = {};
    bool gotAmbientStatus;
    char ambientStatus[64] = {};
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
    gotMusicStatus = rxMusicStatusReady;
    if (gotMusicStatus) memcpy(musicStatus, rxMusicStatus, sizeof(musicStatus));
    rxMusicStatusReady = false;
    gotAmbientStatus = rxAmbientStatusReady;
    if (gotAmbientStatus) memcpy(ambientStatus, rxAmbientStatus, sizeof(ambientStatus));
    rxAmbientStatusReady = false;
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

    if (gotAmbientStatus) {
        Serial.printf("AUDIO <- %s\n", ambientStatus);
        if (!strcmp(ambientStatus, "AMBIENT:ON")) {
            ambientEnabled = true;
            ambientKnown = true;
        } else if (!strcmp(ambientStatus, "AMBIENT:OFF")) {
            ambientEnabled = false;
            ambientKnown = true;
        } else {
            ambientKnown = false;
        }
        if (currentPage == PAGE_AUDIO && currentAudioPage == AUDIO_SFX)
            drawScreen();
    }
    if (gotMusicStatus) {
        Serial.printf("AUDIO <- %s\n", musicStatus);
        unsigned value = 0, total = 0;
        if (sscanf(musicStatus, "MUSIC:VOLUME:%u", &value) == 1 &&
            value <= 100) {
            radioVolume = value;
            radioVolumeKnown = true;
        } else if (strncmp(musicStatus, "MUSIC:", 6) == 0) {
            const char *state = musicStatus + 6;
            const char *sep = strchr(state, ':');
            if (sep &&
                sscanf(sep + 1, "%u/%u", &value, &total) == 2 &&
                value >= 1 && value <= 255 &&
                total >= 1 && total <= 255) {
                radioTrack = value;
                radioTrackCount = total;
                radioTrackKnown = true;
            }
            if (!strncmp(state, "PLAYING:", 8)) {
                radioPlaying = true; radioPaused = false; radioStateKnown = true;
            } else if (!strncmp(state, "PAUSED:", 7)) {
                radioPlaying = false; radioPaused = true; radioStateKnown = true;
            } else if (!strncmp(state, "STOPPED:", 8)) {
                radioPlaying = false; radioPaused = false; radioStateKnown = true;
            }
        }
        if (currentPage == PAGE_AUDIO && currentAudioPage == AUDIO_RADIO)
            drawScreen();
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

// Set local time over USB, e.g. T2026-10-27 13:45:00.
// The clock must be set again after a full power cycle.
void setClockFromSerial(const char *value) {
    int yr, mo, day, hr, min, sec;
    if (sscanf(value, "%d-%d-%d %d:%d:%d", &yr, &mo, &day, &hr, &min, &sec) != 6 ||
        yr < 2024 || mo < 1 || mo > 12 || day < 1 || day > 31 ||
        hr < 0 || hr > 23 || min < 0 || min > 59 || sec < 0 || sec > 59) {
        Serial.println("CLOCK: INVALID FORMAT; USE TYYYY-MM-DD HH:MM:SS");
        return;
    }
    struct tm t = {};
    t.tm_year = yr - 1900;
    t.tm_mon = mo - 1;
    t.tm_mday = day;
    t.tm_hour = hr;
    t.tm_min = min;
    t.tm_sec = sec;
    t.tm_isdst = -1;
    time_t epoch = mktime(&t);
    struct timeval tv = {epoch, 0};
    settimeofday(&tv, nullptr);
    clockSet = true;
    Serial.println("CLOCK: SET (CENTRAL TIME)");
    if (currentPage == PAGE_HOME) drawHomeClock();
}

void setup() {
    Serial.begin(115200);
    // Central time including US daylight-saving rules. No Wi-Fi network needed.
    setenv("TZ", "CST6CDT,M3.2.0/2,M11.1.0/2", 1);
    tzset();
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
    if (currentPage == PAGE_HOME) drawHomeClock();
}

void loop() {
    updateHelmetRadio();
    if (ambientQueryAt && (int32_t)(millis() - ambientQueryAt) >= 0) {
        ambientQueryAt = 0;
        sendAudioFxCommand("AMBIENT:STATUS");
    }
    if (radioVolumeQueryAt && (int32_t)(millis() - radioVolumeQueryAt) >= 0) {
        radioVolumeQueryAt = 0;
        sendAudioMusicCommand("MUSIC:VOLUME");
    }
    if (currentPage == PAGE_HOME && millis() - lastHomeClockDraw >= 1000) {
        lastHomeClockDraw = millis();
        drawHomeClock();
    }

    // Serial Monitor: S=status, 1/0=fan, 2/3=tracking, T...=clock.
    static char timeCommand[32];
    static uint8_t timeIndex = 0;
    static bool collectingTime = false;
    while (Serial.available() > 0) {
        char key = Serial.read();
        if (collectingTime) {
            if (key == '\n' || key == '\r') {
                timeCommand[timeIndex] = '\0';
                setClockFromSerial(timeCommand);
                timeIndex = 0;
                collectingTime = false;
            } else if (timeIndex < sizeof(timeCommand) - 1) {
                timeCommand[timeIndex++] = key;
            } else {
                timeIndex = 0;
                collectingTime = false;
                Serial.println("CLOCK: COMMAND TOO LONG");
            }
            continue;
        }
        if (key == 'T') {
            collectingTime = true;
            timeIndex = 0;
        } else if (key == 's' || key == 'S') {
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
