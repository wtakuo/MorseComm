// net_internal.h — net.cpp と各無線の実装 (net_espnow.cpp / net_lora.cpp) の間だけで使う
#pragma once
#include "net.h"

namespace net {
namespace detail {

// 受信キューに積む (コールバック・タスクから。ISR からは呼ばない)
void pushRx(const RxItem& item);

// 統計の更新: updateStats([](Stats& s) { s.sent++; }); (排他は内部で取る)
void lockStats();
void unlockStats();
Stats& statsRef();
template <typename F>
void updateStats(F f) {
  lockStats();
  f(statsRef());
  unlockStats();
}

// ロス注入: このパケットを落とすなら true (統計も数える)
bool injectDrop(const uint8_t* data, size_t len);

}  // namespace detail

namespace espnow {
bool begin(uint8_t wifiChannel);
bool send(const uint8_t* data, size_t len);
}  // namespace espnow

namespace lora {
bool available();  // このボードに LoRa がある
bool begin(uint8_t channel);
bool send(const uint8_t* data, size_t len);
void setChannel(uint8_t channel);
void end();
}  // namespace lora

}  // namespace net
