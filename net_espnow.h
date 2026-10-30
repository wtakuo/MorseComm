// net_espnow.h — ESP-NOW 送受信ラッパ
// 受信コールバックでは受信時刻を記録してキューに積むだけにし、処理は呼び出し側タスクで行う。
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "protocol.h"

namespace net {

struct RxItem {
  uint32_t rxMs;   // 受信コールバック時点の millis()
  int8_t rssi;
  uint8_t len;
  uint8_t src[6];
  uint8_t data[cw::PKT_MAX_SIZE];
};

struct Stats {
  uint32_t sent = 0;       // esp_now_send 成功
  uint32_t sendErr = 0;    // esp_now_send がエラーを返した
  uint32_t cbOk = 0;       // 送信コールバック: 成功
  uint32_t cbFail = 0;     // 送信コールバック: 失敗 (ブロードキャストでは通常発生しない)
  uint32_t rxDropped = 0;  // 受信キューあふれ
};

bool begin(uint8_t channel);
bool send(const uint8_t* data, size_t len);
bool receive(RxItem& item);  // キューから1つ取り出す (待たない)
Stats stats();
void ownMac(uint8_t mac[6]);

}  // namespace net
