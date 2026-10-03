#include <Arduino.h>
#include <esp_arduino_version.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#define I2C_ADDRESS 0x42
#define SDA_PIN D4
#define SCL_PIN D5

#define FRAME_SIZE 16

const uint8_t ESPNOW_CHANNEL = 6;

const uint8_t broadcastAddress[] = {
  0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF
};

volatile bool commandPending = false;
volatile bool responseReady = false;
volatile bool awaitingResponse = false;

char pendingCommand[FRAME_SIZE] = {0};
char latestResponse[FRAME_SIZE] = {0};


// =====================================================
// RESPONSE BUFFER
// =====================================================
void setResponse(const char *msg) {

  memset(latestResponse, 0, FRAME_SIZE);
  strncpy(latestResponse, msg, FRAME_SIZE - 1);
}


// =====================================================
// I2C RECEIVE FROM UNO
// =====================================================
void receiveEvent(int howMany) {

  (void)howMany;

  memset(pendingCommand, 0, FRAME_SIZE);

  int i = 0;

  while (Wire.available() && i < FRAME_SIZE - 1) {
    pendingCommand[i++] = Wire.read();
  }

  // Drain anything extra
  while (Wire.available()) {
    Wire.read();
  }

  // Invalidate any response from the previous command.
  setResponse("WAIT");

  responseReady = false;
  awaitingResponse = false;
  commandPending = true;
}


// =====================================================
// I2C RESPONSE TO UNO
// =====================================================
void requestEvent() {

  static const uint8_t waitFrame[FRAME_SIZE] = {
    'W', 'A', 'I', 'T'
  };

  if (responseReady) {

    Wire.write(
      (const uint8_t *)latestResponse,
      FRAME_SIZE
    );

    // A response is valid for one read only.
    responseReady = false;
    awaitingResponse = false;
  }
  else {

    Wire.write(
      waitFrame,
      FRAME_SIZE
    );
  }
}


// =====================================================
// PROCESS ESP-NOW RESPONSE
// =====================================================
void processRadioResponse(
  const uint8_t *data,
  int len
) {

  // Ignore packets unless we are waiting for a response
  // to the command we just transmitted.
  if (!awaitingResponse) {
    return;
  }

  memset(latestResponse, 0, FRAME_SIZE);

  int copyLength = min(len, FRAME_SIZE - 1);

  memcpy(
    latestResponse,
    data,
    copyLength
  );

  responseReady = true;
  awaitingResponse = false;
}


// =====================================================
// ESP-NOW RECEIVE CALLBACK
// Arduino-ESP32 3.x
// =====================================================
#if ESP_ARDUINO_VERSION_MAJOR >= 3

void onDataReceived(
  const esp_now_recv_info_t *info,
  const uint8_t *data,
  int len
) {

  // Sender information is not currently needed
  (void)info;

  processRadioResponse(data, len);
}


// =====================================================
// ESP-NOW RECEIVE CALLBACK
// Arduino-ESP32 2.x
// =====================================================
#else

void onDataReceived(
  const uint8_t *mac_addr,
  const uint8_t *data,
  int len
) {

  // Sender MAC is not currently needed
  (void)mac_addr;

  processRadioResponse(data, len);
}

#endif


// =====================================================
// SETUP
// =====================================================
void setup() {

  setResponse("READY");

  // I2C
  Wire.onReceive(receiveEvent);
  Wire.onRequest(requestEvent);

  Wire.begin(
    I2C_ADDRESS,
    SDA_PIN,
    SCL_PIN,
    100000
  );


  // ESP-NOW
  WiFi.mode(WIFI_STA);
  delay(100);

  esp_wifi_set_channel(
    ESPNOW_CHANNEL,
    WIFI_SECOND_CHAN_NONE
  );

  if (esp_now_init() != ESP_OK) {

    setResponse("RADIO_ERR");

    while (true) {
      delay(1000);
    }
  }

  esp_now_register_recv_cb(onDataReceived);


  esp_now_peer_info_t peer = {};

  memcpy(
    peer.peer_addr,
    broadcastAddress,
    6
  );

  peer.channel = ESPNOW_CHANNEL;
  peer.encrypt = false;
  peer.ifidx = WIFI_IF_STA;

  esp_now_add_peer(&peer);
}


// =====================================================
// LOOP
// =====================================================
void loop() {

  if (commandPending) {

    commandPending = false;

    // Only responses arriving after this point
    // can satisfy the current command.
    awaitingResponse = true;

    esp_err_t result = esp_now_send(
      broadcastAddress,
      (const uint8_t *)pendingCommand,
      strlen(pendingCommand) + 1
    );

    if (result != ESP_OK) {

      awaitingResponse = false;

      setResponse("SEND_ERR");
      responseReady = true;
    }
  }

  delay(1);
}