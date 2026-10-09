#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_arduino_version.h>

#define FAN_PIN 27
#define LENS_PIN 33  // MOSFET gate/trigger previously tested on Feather ESP32 V2

const uint8_t ESPNOW_CHANNEL = 6;

bool fanActive = false;
bool lensesActive = false;

// -----------------------------------------------------
// ESP-NOW RESPONSE
// -----------------------------------------------------
void sendResponse(const uint8_t *destination, const char *message) {

  if (!esp_now_is_peer_exist(destination)) {

    esp_now_peer_info_t peer = {};

    memcpy(peer.peer_addr, destination, 6);

    peer.channel = ESPNOW_CHANNEL;
    peer.encrypt = false;
    peer.ifidx = WIFI_IF_STA;

    esp_err_t result = esp_now_add_peer(&peer);

    if (result != ESP_OK && result != ESP_ERR_ESPNOW_EXIST) {
      Serial.print("PEER ADD ERROR: ");
      Serial.println(result);
      return;
    }
  }

  esp_now_send(
    destination,
    (const uint8_t *)message,
    strlen(message) + 1
  );

  Serial.print("TX: ");
  Serial.println(message);
}

// -----------------------------------------------------
// COMMAND HANDLER
// -----------------------------------------------------
void handleCommand(
  const uint8_t *sender,
  const uint8_t *data,
  int len
) {

  char command[16] = {0};

  int copyLength = min(len, 15);

  memcpy(
    command,
    data,
    copyLength
  );

  command[15] = '\0';

  Serial.print("RX: ");
  Serial.println(command);

  if (strcmp(command, "PING") == 0) {
    sendResponse(sender, "PONG");
    return;
  }

  if (strcmp(command, "FAN_ON") == 0) {
    digitalWrite(FAN_PIN, HIGH);
    fanActive = true;

    Serial.println("FAN: ON");

    sendResponse(sender, "FAN_ON_OK");
    return;
  }

  if (strcmp(command, "FAN_OFF") == 0) {
    digitalWrite(FAN_PIN, LOW);
    fanActive = false;

    Serial.println("FAN: OFF");

    sendResponse(sender, "FAN_OFF_OK");
    return;
  }

  if (strcmp(command, "STATUS") == 0) {
    // Keep the existing fan-status protocol unchanged.
    sendResponse(sender, fanActive ? "FAN_ON" : "FAN_OFF");
    return;
  }

  if (strcmp(command, "LENS_ON") == 0) {
    digitalWrite(LENS_PIN, HIGH);
    lensesActive = true;

    Serial.println("LENSES: ON");

    sendResponse(sender, "LENS_ON_OK");
    return;
  }

  if (strcmp(command, "LENS_OFF") == 0) {
    digitalWrite(LENS_PIN, LOW);
    lensesActive = false;

    Serial.println("LENSES: OFF");

    sendResponse(sender, "LENS_OFF_OK");
    return;
  }

  if (strcmp(command, "LENS_STATUS") == 0) {
    sendResponse(sender, lensesActive ? "LENS_ON" : "LENS_OFF");
    return;
  }

  sendResponse(sender, "UNKNOWN");
}

// -----------------------------------------------------
// ESP-NOW RECEIVE CALLBACK
// Arduino-ESP32 3.x
// -----------------------------------------------------
#if ESP_ARDUINO_VERSION_MAJOR >= 3

void onDataReceived(
  const esp_now_recv_info_t *info,
  const uint8_t *data,
  int len
) {

  handleCommand(
    info->src_addr,
    data,
    len
  );
}

// -----------------------------------------------------
// ESP-NOW RECEIVE CALLBACK
// Arduino-ESP32 2.x
// -----------------------------------------------------
#else

void onDataReceived(
  const uint8_t *mac_addr,
  const uint8_t *data,
  int len
) {

  handleCommand(
    mac_addr,
    data,
    len
  );
}

#endif

// -----------------------------------------------------
// SETUP
// -----------------------------------------------------
void setup() {

  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("==============================");
  Serial.println(" NCR HELMET RADIO");
  Serial.println("==============================");

  pinMode(FAN_PIN, OUTPUT);
  pinMode(LENS_PIN, OUTPUT);

  // Safe boot state: both outputs off.
  digitalWrite(FAN_PIN, LOW);
  digitalWrite(LENS_PIN, LOW);
  fanActive = false;
  lensesActive = false;

  // Start from a clean radio state.
  // This helmet node does not need to connect to normal Wi-Fi.
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  delay(200);

  // Force the ESP32 radio onto our dedicated ESP-NOW channel.
  esp_wifi_set_promiscuous(true);

  esp_err_t channelResult = esp_wifi_set_channel(
    ESPNOW_CHANNEL,
    WIFI_SECOND_CHAN_NONE
  );

  esp_wifi_set_promiscuous(false);

  // Verify what channel the radio is ACTUALLY using.
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

  // Start ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW INIT FAILED");
    while (true) {
      delay(1000);
    }
  }

  esp_now_register_recv_cb(onDataReceived);

  Serial.println("ESP-NOW: ONLINE");
  Serial.println("Fan:     OFF");
  Serial.println("Lenses:  OFF");
  Serial.println("System:  READY");
}

// -----------------------------------------------------
// LOOP
// -----------------------------------------------------
void loop() {
  delay(10);
}
