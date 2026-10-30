// net.h — 無線の送受信 (ESP-NOW / LoRa 共通のインターフェース)
// 受信は割り込み・コールバックで受信時刻を記録してキューに積むだけにし、処理は呼び出し側タスクで行う。
// 実装: net.cpp (共通部: 受信キュー・統計・ロス注入) / net_espnow.cpp / net_lora.cpp (Nesso N1 のみ)
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "protocol.h"

namespace net {

struct RxItem {
  uint32_t rxMs;   // 受信した瞬間の millis()
  int8_t rssi;     // dBm (LoRa は LNA の利得を差し引いた、アンテナ端の値)
  uint8_t len;
  uint8_t src[6];  // 送信元 (ESP-NOW: MAC アドレス / LoRa: パケットに載せた送信元の MAC アドレス)
  uint8_t data[cw::PKT_MAX_SIZE];
};

struct Stats {
  uint32_t sent = 0;       // 送信した (ESP-NOW: esp_now_send 成功 / LoRa: 送信完了)
  uint32_t sendErr = 0;    // 送信エラー
  uint32_t cbOk = 0;       // ESP-NOW 送信コールバック: 成功
  uint32_t cbFail = 0;     // ESP-NOW 送信コールバック: 失敗 (ブロードキャストでは通常発生しない)
  uint32_t rxDropped = 0;  // 受信キューあふれ
  uint32_t rxErr = 0;      // LoRa: CRC エラーなど
  uint32_t injected = 0;   // ロス注入で送らなかった
  uint32_t csBusy = 0;     // LoRa: キャリアセンスで他局の電波を検出して待った回数
  uint32_t airMs = 0;      // LoRa: 送信時間の合計
};

// radio = cfg::RADIO_*。RADIO_OFF なら Wi-Fi も LoRa も初期化しない (電波を出さない)
bool begin(uint8_t radio, uint8_t channel);
uint8_t radio();
// 送る (RADIO_OFF では何もしない)。LoRa は最新の 1 パケットだけを保持し、送信できる時になったら送る (古いものは上書き)
bool send(const uint8_t* data, size_t len);
bool receive(RxItem& item);  // キューから1つ取り出す (待たない)
// 通信チャンネルが変わった (LoRa は周波数を変える)
void setChannel(uint8_t channel);
// 実機試験用のロス注入 (送信の直前に判定する)
void setLossInjection(uint8_t pct, bool dropUp);
// スリープ前に無線を止める
void end();
Stats stats();
void ownMac(uint8_t mac[6]);
bool loraAvailable();  // このボードで LoRa が使えるか

}  // namespace net
