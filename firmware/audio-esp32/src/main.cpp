
 // NCR Ranger audio ESP32: ESP-NOW presence + Metro SFX -> Teensy UART bridge
 // Board: ESP32-WROOM-32 / PlatformIO esp32dev
 // Radio: ESP-NOW channel 6
 // UART2: ESP32 RX=GPIO16, TX=GPIO17 -> Teensy Serial1

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_mac.h>
#include <cstring>

constexpr uint8_t ESPNOW_CHANNEL = 6;
constexpr uint32_t UART_BAUD = 115200;
// Teensy response buffer
constexpr size_t TEENSY_REPLY_SIZE = 64;
char teensyReply[TEENSY_REPLY_SIZE] = {};
size_t teensyReplyLength = 0;
constexpr int ESP32_RX2 = 16; // From Teensy TX1, pin 1
constexpr int ESP32_TX2 = 17; // To Teensy RX1, pin 0

const uint8_t BROADCAST_MAC[6] = {
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

// Callback runs on Wi-Fi task.
// Process received commands in loop().
portMUX_TYPE receiveMux = portMUX_INITIALIZER_UNLOCKED;

volatile bool pingPending = false;
volatile bool sfxPending = false;

// Radio New Vegas wireless command mailbox
// The ESP-NOW callback only copies commands.
// The main loop forwards them to the Teensy.

constexpr size_t AUDIO_COMMAND_SIZE = 32;

char musicCommandPending[AUDIO_COMMAND_SIZE] = {};
bool musicCommandReady = false;

void onReceive(const esp_now_recv_info_t *info,
               const uint8_t *data, int len) {
  if (!info || !data) return;

  if (len == (int)sizeof("PING") &&
      memcmp(data, "PING", sizeof("PING")) == 0) {

    portENTER_CRITICAL(&receiveMux);
    pingPending = true;
    portEXIT_CRITICAL(&receiveMux);

  } else if (len == (int)sizeof("SFX_TEST") &&
             memcmp(data, "SFX_TEST",
                    sizeof("SFX_TEST")) == 0) {

    portENTER_CRITICAL(&receiveMux);
    sfxPending = true;
    portEXIT_CRITICAL(&receiveMux);
  }
  else if (
    len > 1 &&
    len <= (int)AUDIO_COMMAND_SIZE &&
    data[len - 1] == '\0' &&
    (strncmp((const char*)data, "MUSIC:", 6) == 0 ||
     strncmp((const char*)data, "FX:", 3) == 0 ||
     strncmp((const char*)data, "AMBIENT:", 8) == 0)
) {
    const char* command = (const char*)data;

    const bool supported =
        !strcmp(command, "MUSIC:PLAY") ||
        !strcmp(command, "MUSIC:PAUSE") ||
        !strcmp(command, "MUSIC:NEXT") ||
        !strcmp(command, "MUSIC:PREV") ||
        !strcmp(command, "MUSIC:STOP") ||
        !strcmp(command, "MUSIC:STATUS") ||
        !strcmp(command, "MUSIC:VOLUP") ||
        !strcmp(command, "MUSIC:VOLDOWN") ||
        !strcmp(command, "MUSIC:VOLUME") ||
        !strcmp(command, "FX:READY") ||
        !strcmp(command, "FX:BOOT") ||
        !strcmp(command, "FX:TRACKON") ||
        !strcmp(command, "FX:TRACKOFF") ||
        !strcmp(command, "FX:CHARGE") ||
        !strcmp(command, "FX:PLASMA") ||
        !strcmp(command, "FX:COOL") ||
        !strcmp(command, "FX:WARN") ||
        !strcmp(command, "FX:RADIO") ||
        !strcmp(command, "FX:SELECT") ||
        !strcmp(command, "FX:STOP") ||
        !strcmp(command, "AMBIENT:ON") ||
        !strcmp(command, "AMBIENT:OFF") ||
        !strcmp(command, "AMBIENT:STATUS");

    if (supported) {
        portENTER_CRITICAL(&receiveMux);

        if (!musicCommandReady) {
            memcpy(musicCommandPending, data, len);
            musicCommandReady = true;
        }

        portEXIT_CRITICAL(&receiveMux);
    }
}
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println(
    "\n=== NCR RANGER AUDIO NODE / SFX BRIDGE ==="
  );

  // Hardware UART to Teensy
  Serial2.begin(
    UART_BAUD,
    SERIAL_8N1,
    ESP32_RX2,
    ESP32_TX2
  );

  Serial.println(
    "UART2 -> TEENSY: READY (RX16 / TX17)"
  );

  if (!WiFi.mode(WIFI_STA)) {
    Serial.println("WiFi station: FAILED");
    return;
  }

  uint8_t mac[6] = {};

  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
    Serial.printf(
      "WiFi STA MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
      mac[0], mac[1], mac[2],
      mac[3], mac[4], mac[5]
    );
  } else {
    Serial.println("MAC read: FAILED");
  }

  esp_err_t rc = esp_wifi_set_channel(
    ESPNOW_CHANNEL,
    WIFI_SECOND_CHAN_NONE
  );

  Serial.printf(
    "Channel %u: %s\n",
    ESPNOW_CHANNEL,
    rc == ESP_OK ? "OK" : "FAILED"
  );

  if (rc != ESP_OK) return;

  rc = esp_now_init();

  Serial.printf(
    "ESP-NOW: %s\n",
    rc == ESP_OK ? "ONLINE" : "FAILED"
  );

  if (rc != ESP_OK) return;

  rc = esp_now_register_recv_cb(onReceive);

  Serial.printf(
    "Receive callback: %s\n",
    rc == ESP_OK ? "OK" : "FAILED"
  );

  if (rc != ESP_OK) return;

  esp_now_peer_info_t broadcastPeer = {};

  memcpy(
    broadcastPeer.peer_addr,
    BROADCAST_MAC,
    6
  );

  broadcastPeer.channel = ESPNOW_CHANNEL;
  broadcastPeer.ifidx = WIFI_IF_STA;
  broadcastPeer.encrypt = false;

  rc = esp_now_add_peer(&broadcastPeer);

  bool peerOK =
    rc == ESP_OK ||
    rc == ESP_ERR_ESPNOW_EXIST;

  Serial.printf(
    "Broadcast peer: %s\n",
    peerOK ? "OK" : "FAILED"
  );

  if (!peerOK) return;

  Serial.println("AUDIO NODE READY");
}

void loop() {
  bool replyToPing;
  bool playSfx;

  char pendingMusicCommand[AUDIO_COMMAND_SIZE] = {};
  bool forwardMusic = false;

  portENTER_CRITICAL(&receiveMux);

  replyToPing = pingPending;
  pingPending = false;

  playSfx = sfxPending;
  sfxPending = false;

  forwardMusic = musicCommandReady;

  if (forwardMusic) {
  memcpy(
    pendingMusicCommand,
    musicCommandPending,
    AUDIO_COMMAND_SIZE
  );

  musicCommandReady = false;
  }

  portEXIT_CRITICAL(&receiveMux);

  // Respond to the Metro's discovery ping
  if (replyToPing) {
    const char reply[] = "AUDIO_PONG";

    esp_err_t rc = esp_now_send(
      BROADCAST_MAC,
      (const uint8_t *)reply,
      sizeof(reply)
    );

    Serial.printf(
      "METRO PING -> AUDIO_PONG: %s\n",
      rc == ESP_OK ? "QUEUED" : "FAILED"
    );
  }

  // Forward Metro SFX command to Teensy
  if (playSfx) {
    Serial2.println("SFX_TEST");

    Serial.println(
      "METRO SFX_TEST -> TEENSY UART: SENT"
    );
  }

  if (forwardMusic) {
  Serial2.println(pendingMusicCommand);

  Serial.print("METRO MUSIC -> TEENSY UART: ");
  Serial.println(pendingMusicCommand);
  }

 // Read Teensy diagnostic replies and forward music
// status messages to the Pip-Boy over ESP-NOW.
while (Serial2.available()) {
  char c = static_cast<char>(Serial2.read());

  if (c == '\n') {
    teensyReply[teensyReplyLength] = '\0';

    if (teensyReplyLength > 0) {
      Serial.println(teensyReply);

      if (strncmp(teensyReply, "MUSIC:", 6) == 0 ||
          strncmp(teensyReply, "FX:", 3) == 0 ||
          strncmp(teensyReply, "AMBIENT:", 8) == 0) {
        esp_err_t result = esp_now_send(
          BROADCAST_MAC,
          reinterpret_cast<const uint8_t*>(teensyReply),
          teensyReplyLength + 1
        );

        Serial.printf(
          "TEENSY -> METRO: %s (%s)\n",
          teensyReply,
          result == ESP_OK ? "QUEUED" : "FAILED"
        );
      }
    }

    teensyReplyLength = 0;

  } else if (c != '\r') {
    if (teensyReplyLength < TEENSY_REPLY_SIZE - 1) {
      teensyReply[teensyReplyLength++] = c;
    } else {
      teensyReplyLength = 0;
    }
  }
}

  delay(5);
}
