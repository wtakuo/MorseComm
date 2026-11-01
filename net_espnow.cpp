// net_espnow.cpp — ESP-NOW (ブロードキャスト) の送受信
#include "net_internal.h"
#include "build_info.h"
BUILD_INFO_UNIT();
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>

namespace net {
namespace espnow {
namespace {

const uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  RxItem item;
  item.rxMs = millis();
  if (len <= 0 || len > (int)sizeof(item.data)) return;
  item.rssi = info->rx_ctrl ? info->rx_ctrl->rssi : 0;
  item.len = (uint8_t)len;
  memcpy(item.src, info->src_addr, 6);
  memcpy(item.data, data, len);
  detail::pushRx(item);
}

void onSent(const esp_now_send_info_t* info, esp_now_send_status_t status) {
  (void)info;
  bool ok = status == ESP_NOW_SEND_SUCCESS;
  detail::updateStats([ok](Stats& s) { ok ? s.cbOk++ : s.cbFail++; });
}

}  // namespace

bool begin(uint8_t wifiChannel) {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_ps(WIFI_PS_NONE);  // 省電力を切って受信遅延を小さくする
  esp_wifi_set_channel(wifiChannel, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) return false;
  esp_now_register_recv_cb(onRecv);
  esp_now_register_send_cb(onSent);
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, BROADCAST, 6);
  peer.channel = wifiChannel;
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;
  return esp_now_add_peer(&peer) == ESP_OK;
}

bool send(const uint8_t* data, size_t len) {
  bool ok = esp_now_send(BROADCAST, data, len) == ESP_OK;
  detail::updateStats([ok](Stats& s) { ok ? s.sent++ : s.sendErr++; });
  return ok;
}

}  // namespace espnow
}  // namespace net
