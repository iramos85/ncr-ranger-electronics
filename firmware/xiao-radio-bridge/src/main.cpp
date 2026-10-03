#include <Arduino.h>
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

char pendingCommand[FRAME_SIZE] = {0};
char latestResponse[FRAME_SIZE] = {0};


void setResponse(const char *msg) {
  memset(latestResponse, 0, FRAME_SIZE);
  strncpy(latestResponse, msg, FRAME_SIZE - 1);
}


void receiveEvent(int howMany) {

  memset(pendingCommand, 0, FRAME_SIZE);

  int i = 0;

  while (Wire.available() && i < FRAME_SIZE - 1) {
    pendingCommand[i++] = Wire.read();
  }

  // Drain anything extra
  while (Wire.available()) {
    Wire.read();
  }

  setResponse("WAIT");

  commandPending = true;
}


void requestEvent() {

  // ALWAYS return exactly 16 bytes
  Wire.write(
    (const uint8_t *)latestResponse,
    FRAME_SIZE
  );
}


void onDataReceived(
  const uint8_t *mac_addr,
  const uint8_t *data,
  int len
) {

  // Sender MAC is not currently needed
  (void)mac_addr;

  memset(latestResponse, 0, FRAME_SIZE);

  int copyLength = min(len, FRAME_SIZE - 1);

  memcpy(
    latestResponse,
    data,
    copyLength
  );
}


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


void loop() {

  if (commandPending) {

    commandPending = false;

    esp_now_send(
      broadcastAddress,
      (const uint8_t *)pendingCommand,
      strlen(pendingCommand) + 1
    );
  }

  delay(1);
}
