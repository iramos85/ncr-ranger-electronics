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

  while (Wire.available()) {
    Wire.read();
  }

  // Invalidate any previous response.
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

  Serial.print("ESP-NOW RX: ");
  Serial.println(latestResponse);
}


// =====================================================
// ESP-NOW RECEIVE CALLBACK
// =====================================================
#if ESP_ARDUINO_VERSION_MAJOR >= 3

void onDataReceived(
  const esp_now_recv_info_t *info,
  const uint8_t *data,
  int len
) {

  (void)info;

  processRadioResponse(data, len);
}

#else

void onDataReceived(
  const uint8_t *mac_addr,
  const uint8_t *data,
  int len
) {

  (void)mac_addr;

  processRadioResponse(data, len);
}

#endif


// =====================================================
// SETUP
// =====================================================
void setup() {

  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("==============================");
  Serial.println(" NCR XIAO RADIO BRIDGE");
  Serial.println("==============================");

  setResponse("READY");


  // ---------------------------------------------------
  // I2C
  // ---------------------------------------------------
  Wire.onReceive(receiveEvent);
  Wire.onRequest(requestEvent);

  Wire.begin(
    I2C_ADDRESS,
    SDA_PIN,
    SCL_PIN,
    100000
  );

  Serial.println("I2C: ONLINE");
  Serial.println("Address: 0x42");


  // ---------------------------------------------------
  // Wi-Fi / ESP-NOW radio
  // ---------------------------------------------------
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  delay(200);

  esp_wifi_set_promiscuous(true);

  esp_err_t channelResult = esp_wifi_set_channel(
    ESPNOW_CHANNEL,
    WIFI_SECOND_CHAN_NONE
  );

  esp_wifi_set_promiscuous(false);


  uint8_t actualChannel = 0;
  wifi_second_chan_t secondaryChannel;

  esp_wifi_get_channel(
    &actualChannel,
    &secondaryChannel
  );

  Serial.print("Channel set result: ");
  Serial.println(channelResult);

  Serial.print("Requested channel: ");
  Serial.println(ESPNOW_CHANNEL);

  Serial.print("Actual channel:    ");
  Serial.println(actualChannel);


  // ---------------------------------------------------
  // ESP-NOW
  // ---------------------------------------------------
  if (esp_now_init() != ESP_OK) {

    Serial.println("ESP-NOW INIT FAILED");

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

  esp_err_t peerResult = esp_now_add_peer(&peer);

  Serial.print("Peer add result: ");
  Serial.println(peerResult);

  Serial.println("ESP-NOW: ONLINE");
  Serial.println("System: READY");
}


// =====================================================
// LOOP
// =====================================================
void loop() {

  if (commandPending) {

    commandPending = false;

    Serial.print("I2C RX: ");
    Serial.println(pendingCommand);

    awaitingResponse = true;

    esp_err_t result = esp_now_send(
      broadcastAddress,
      (const uint8_t *)pendingCommand,
      strlen(pendingCommand) + 1
    );

    Serial.print("ESP-NOW TX result: ");
    Serial.println(result);

    if (result != ESP_OK) {

      awaitingResponse = false;

      setResponse("SEND_ERR");
      responseReady = true;
    }
  }

  delay(1);
}