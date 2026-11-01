// net_internal.h - Used only between net.cpp and each radio implementation
// (net_espnow.cpp / net_lora.cpp)
#pragma once
#include "net.h"

namespace net {
namespace detail {

// Push to the receive queue (from callbacks/tasks; not called from an ISR)
void pushRx(const RxItem& item);

// Update stats: updateStats([](Stats& s) { s.sent++; }); (locking is done
// internally)
void lockStats();
void unlockStats();
Stats& statsRef();
template <typename F>
void updateStats(F f) {
    lockStats();
    f(statsRef());
    unlockStats();
}

// Loss injection: true if this packet should be dropped (also counted in stats)
bool injectDrop(const uint8_t* data, size_t len);

}  // namespace detail

namespace espnow {
bool begin(uint8_t wifiChannel);
bool send(const uint8_t* data, size_t len);
}  // namespace espnow

namespace lora {
bool available();  // This board has LoRa
bool begin(uint8_t channel);
bool send(const uint8_t* data, size_t len);
void setChannel(uint8_t channel);
void end();
}  // namespace lora

}  // namespace net
