#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_mac.h>
#include <cstring>

// NCR Ranger audio node — ESP-NOW discovery test
// Hardware: ESP32-WROOM-32, Arduino IDE board: ESP32 Dev Module
constexpr uint8_t ESPNOW_CHANNEL = 6;
const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

portMUX_TYPE receiveMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool pingPending = false;

void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (!info || !data) return;
  // Metro sends "PING" with its terminating NUL byte.
  if (len == (int)sizeof("PING") &&
      memcmp(data, "PING", sizeof("PING")) == 0) {
    portENTER_CRITICAL(&receiveMux);
    pingPending = true;
    portEXIT_CRITICAL(&receiveMux);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== NCR RANGER AUDIO NODE DISCOVERY ===");

  if (!WiFi.mode(WIFI_STA)) {
    Serial.println("WiFi station: FAILED");
    return;
  }
  uint8_t mac[6] = {};
  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
    Serial.printf("WiFi STA MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  } else {
    Serial.println("MAC read: FAILED");
  }

  esp_err_t rc = esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  Serial.printf("Channel %u: %s\n", ESPNOW_CHANNEL,
                rc == ESP_OK ? "OK" : "FAILED");
  if (rc != ESP_OK) return;

  rc = esp_now_init();
  Serial.printf("ESP-NOW: %s\n", rc == ESP_OK ? "ONLINE" : "FAILED");
  if (rc != ESP_OK) return;

  rc = esp_now_register_recv_cb(onReceive);
  Serial.printf("Receive callback: %s\n", rc == ESP_OK ? "OK" : "FAILED");
  if (rc != ESP_OK) return;

  esp_now_peer_info_t broadcastPeer = {};
  memcpy(broadcastPeer.peer_addr, BROADCAST_MAC, 6);
  broadcastPeer.channel = ESPNOW_CHANNEL;
  broadcastPeer.ifidx = WIFI_IF_STA;
  broadcastPeer.encrypt = false;
  rc = esp_now_add_peer(&broadcastPeer);
  Serial.printf("Broadcast peer: %s\n",
                (rc == ESP_OK || rc == ESP_ERR_ESPNOW_EXIST) ? "OK" : "FAILED");
  Serial.println("AUDIO NODE READY");
}

void loop() {
  bool respond = false;
  portENTER_CRITICAL(&receiveMux);
  if (pingPending) {
    pingPending = false;
    respond = true;
  }
  portEXIT_CRITICAL(&receiveMux);

  if (respond) {
    const char reply[] = "AUDIO_PONG";
    esp_err_t rc = esp_now_send(BROADCAST_MAC,
                                (const uint8_t *)reply, sizeof(reply));
    Serial.printf("METRO PING -> AUDIO_PONG: %s\n",
                  rc == ESP_OK ? "QUEUED" : "FAILED");
  }
  delay(5);
}
