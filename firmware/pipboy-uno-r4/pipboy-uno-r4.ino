#include <Arduino_GFX_Library.h>
#include <TouchScreen.h>

// =====================================================
// COLORS
// =====================================================
#define NCR_BLACK       0x0000
#define NCR_GREEN       0x07E0
#define NCR_DARK_GREEN  0x0320
#define NCR_RED         0xF800
#define NCR_AMBER       0xFD20
#define NCR_GRAY        0x8410

// =====================================================
// TFT
// =====================================================
Arduino_DataBus *bus = new Arduino_SWPAR8(
  A2, A3, A1, A0,
  8, 9, 2, 3, 4, 5, 6, 7
);

Arduino_GFX *gfx = new Arduino_ILI9486(
  bus,
  A4,
  1,
  false
);

// =====================================================
// TOUCH
// =====================================================
#define XP 8
#define YM 9
#define XM A2
#define YP A3

#define MINPRESSURE 20
#define MAXPRESSURE 1000

TouchScreen ts = TouchScreen(XP, YP, XM, YM, 300);

// Current calibration
#define TS_LEFT    880
#define TS_RIGHT   220
#define TS_TOP     810
#define TS_BOTTOM  250

// =====================================================
// SYSTEM STATE
// =====================================================
bool trackingActive = false;
bool cannonLightActive = false;
bool touchWasDown = false;

// =====================================================
// SYSTEM EVENTS
// =====================================================
enum SystemEvent {
  EVENT_TRACKING_START,
  EVENT_TRACKING_STOP,
  EVENT_CANNON_LIGHT_ON,
  EVENT_CANNON_LIGHT_OFF,
  EVENT_SFX_TEST
};

const char* eventName(SystemEvent event) {

  switch (event) {

    case EVENT_TRACKING_START:
      return "TRACKING_START";

    case EVENT_TRACKING_STOP:
      return "TRACKING_STOP";

    case EVENT_CANNON_LIGHT_ON:
      return "CANNON_LIGHT_ON";

    case EVENT_CANNON_LIGHT_OFF:
      return "CANNON_LIGHT_OFF";

    case EVENT_SFX_TEST:
      return "SFX_TEST";

    default:
      return "UNKNOWN";
  }
}

void sendSystemEvent(SystemEvent event) {

  // Diagnostic output ONLY.
  // Later this same event also goes to the ESP32 radio.

  Serial.print("EVENT: ");
  Serial.println(eventName(event));
}

// =====================================================
// BUTTON GEOMETRY
// =====================================================

// Tracking
const int TRACK_X = 20;
const int TRACK_Y = 80;
const int TRACK_W = 205;
const int TRACK_H = 75;

// Cannon light
const int LIGHT_X = 255;
const int LIGHT_Y = 80;
const int LIGHT_W = 205;
const int LIGHT_H = 75;

// Sound test
const int SFX_X = 80;
const int SFX_Y = 185;
const int SFX_W = 320;
const int SFX_H = 65;

// =====================================================
// UI
// =====================================================
void drawHeader() {

  gfx->fillRect(0, 0, 480, 45, NCR_DARK_GREEN);

  gfx->setTextColor(NCR_GREEN);
  gfx->setTextSize(2);
  gfx->setCursor(15, 14);
  gfx->print("NCR RANGER // CONTROL");
}

void drawButton(
  int x,
  int y,
  int w,
  int h,
  const char* label,
  uint16_t color
) {

  gfx->fillRect(x, y, w, h, NCR_BLACK);

  gfx->drawRect(x, y, w, h, color);
  gfx->drawRect(x + 3, y + 3, w - 6, h - 6, color);

  gfx->setTextColor(color);
  gfx->setTextSize(2);

  int16_t textX = x + 15;
  int16_t textY = y + 27;

  gfx->setCursor(textX, textY);
  gfx->print(label);
}

void drawScreen() {

  gfx->fillScreen(NCR_BLACK);

  drawHeader();

  gfx->setTextColor(NCR_GREEN);
  gfx->setTextSize(1);
  gfx->setCursor(15, 58);
  gfx->print("RANGER SYSTEM BUS // LOCAL CONTROL");

  // Tracking button
  drawButton(
    TRACK_X,
    TRACK_Y,
    TRACK_W,
    TRACK_H,
    trackingActive ? "TRACKING: ON" : "TRACKING: OFF",
    trackingActive ? NCR_AMBER : NCR_GREEN
  );

  // Cannon light button
  drawButton(
    LIGHT_X,
    LIGHT_Y,
    LIGHT_W,
    LIGHT_H,
    cannonLightActive ? "LIGHT: ON" : "LIGHT: OFF",
    cannonLightActive ? NCR_AMBER : NCR_GREEN
  );

  // SFX
  drawButton(
    SFX_X,
    SFX_Y,
    SFX_W,
    SFX_H,
    "PLAY TEST SFX",
    NCR_GREEN
  );

  // Footer
  gfx->setTextSize(1);
  gfx->setTextColor(NCR_GRAY);

  gfx->setCursor(20, 285);
  gfx->print("HELMET");

  gfx->setCursor(180, 285);
  gfx->print("CANNON");

  gfx->setCursor(350, 285);
  gfx->print("AUDIO");

  gfx->setTextColor(NCR_AMBER);

  gfx->setCursor(20, 300);
  gfx->print("OFFLINE");

  gfx->setCursor(180, 300);
  gfx->print("OFFLINE");

  gfx->setCursor(350, 300);
  gfx->print("OFFLINE");
}

// =====================================================
// TOUCH
// =====================================================
bool getTouch(int &screenX, int &screenY) {

  TSPoint p = ts.getPoint();

  // Restore shared pins for TFT
  pinMode(XP, OUTPUT);
  pinMode(YM, OUTPUT);
  pinMode(XM, OUTPUT);
  pinMode(YP, OUTPUT);

  if (p.z < MINPRESSURE || p.z > MAXPRESSURE) {
    return false;
  }

  screenX = map(
    p.y,
    TS_LEFT,
    TS_RIGHT,
    0,
    479
  );

  screenY = map(
    p.x,
    TS_TOP,
    TS_BOTTOM,
    0,
    319
  );

  screenX = constrain(screenX, 0, 479);
  screenY = constrain(screenY, 0, 319);

  return true;
}

bool insideButton(
  int px,
  int py,
  int x,
  int y,
  int w,
  int h
) {

  return (
    px >= x &&
    px <= x + w &&
    py >= y &&
    py <= y + h
  );
}

// =====================================================
// TOUCH ACTION
// =====================================================
void handleTouch(int x, int y) {

  Serial.print("TOUCH: X=");
  Serial.print(x);
  Serial.print(" Y=");
  Serial.println(y);

  // Tracking
  if (insideButton(
      x, y,
      TRACK_X, TRACK_Y,
      TRACK_W, TRACK_H)) {

    trackingActive = !trackingActive;

    if (trackingActive) {
      sendSystemEvent(EVENT_TRACKING_START);
    }
    else {
      sendSystemEvent(EVENT_TRACKING_STOP);
    }

    drawScreen();
    return;
  }

  // Cannon flashlight
  if (insideButton(
      x, y,
      LIGHT_X, LIGHT_Y,
      LIGHT_W, LIGHT_H)) {

    cannonLightActive = !cannonLightActive;

    if (cannonLightActive) {
      sendSystemEvent(EVENT_CANNON_LIGHT_ON);
    }
    else {
      sendSystemEvent(EVENT_CANNON_LIGHT_OFF);
    }

    drawScreen();
    return;
  }

  // SFX test
  if (insideButton(
      x, y,
      SFX_X, SFX_Y,
      SFX_W, SFX_H)) {

    sendSystemEvent(EVENT_SFX_TEST);

    return;
  }
}

// =====================================================
// SETUP
// =====================================================
void setup() {

  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("==============================");
  Serial.println("NCR PIP-BOY CONTROL SYSTEM");
  Serial.println("==============================");

  if (!gfx->begin()) {
    Serial.println("DISPLAY INIT FAILED");
    while (1);
  }

  Serial.println("Display: ONLINE");
  Serial.println("Touch:   ONLINE");
  Serial.println("Radio:   NOT INSTALLED");
  Serial.println("System:  STANDBY");

  drawScreen();
}

// =====================================================
// LOOP
// =====================================================
void loop() {

  int touchX;
  int touchY;

  bool touchDown = getTouch(touchX, touchY);

  // Only trigger when finger first lands
  if (touchDown && !touchWasDown) {
    handleTouch(touchX, touchY);
  }

  touchWasDown = touchDown;

  delay(20);
}