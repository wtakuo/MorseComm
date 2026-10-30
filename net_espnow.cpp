#include "net_espnow.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_mac.h>
#include <string.h>

namespace net {
namespace {

const uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
constexpr int RX_QUEUE_LEN = 32;

QueueHandle_t rxQueue = nullptr;
portMUX_TYPE statsMux = portMUX_INITIALIZER_UNLOCKED;
Stats st;

void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  RxItem item;
  item.rxMs = millis();
  if (len <= 0 || len > (int)sizeof(item.data)) return;
  item.rssi = info->rx_ctrl ? info->rx_ctrl->rssi : 0;
  item.len = (uint8_t)len;
  memcpy(item.src, info->src_addr, 6);
  memcpy(item.data, data, len);
  if (xQueueSend(rxQueue, &item, 0) != pdTRUE) {
    portENTER_CRITICAL(&statsMux);
    st.rxDropped++;
    portEXIT_CRITICAL(&statsMux);
  }
}

void onSent(const esp_now_send_info_t* info, esp_now_send_status_t status) {
  (void)info;
  portENTER_CRITICAL(&statsMux);
  if (status == ESP_NOW_SEND_SUCCESS) st.cbOk++;
  else st.cbFail++;
  portEXIT_CRITICAL(&statsMux);
}

}  // namespace

bool begin(uint8_t channel) {
  rxQueue = xQueueCreate(RX_QUEUE_LEN, sizeof(RxItem));
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_ps(WIFI_PS_NONE);  // 省電力を切って受信遅延を小さくする
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) return false;
  esp_now_register_recv_cb(onRecv);
  esp_now_register_send_cb(onSent);
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, BROADCAST, 6);
  peer.channel = channel;
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;
  return esp_now_add_peer(&peer) == ESP_OK;
}

bool send(const uint8_t* data, size_t len) {
  bool ok = esp_now_send(BROADCAST, data, len) == ESP_OK;
  portENTER_CRITICAL(&statsMux);
  if (ok) st.sent++;
  else st.sendErr++;
  portEXIT_CRITICAL(&statsMux);
  return ok;
}

bool receive(RxItem& item) { return rxQueue && xQueueReceive(rxQueue, &item, 0) == pdTRUE; }

Stats stats() {
  portENTER_CRITICAL(&statsMux);
  Stats s = st;
  portEXIT_CRITICAL(&statsMux);
  return s;
}

void ownMac(uint8_t mac[6]) { esp_read_mac(mac, ESP_MAC_WIFI_STA); }

}  // namespace net
