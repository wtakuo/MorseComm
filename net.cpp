// net.cpp - Common radio part: receive queue, stats, loss injection, dispatch
// by radio type
#include "net_internal.h"
#include "build_info.h"
BUILD_INFO_UNIT();
#include <Arduino.h>
#include <esp_mac.h>
#include <esp_random.h>
#include "config.h"

namespace net {
namespace {

constexpr int RX_QUEUE_LEN = 32;

QueueHandle_t rxQueue = nullptr;
portMUX_TYPE statsMux = portMUX_INITIALIZER_UNLOCKED;
Stats st;
uint8_t curRadio = cfg::RADIO_ESPNOW;
volatile uint8_t lossPct = 0;
volatile bool dropUp = false;

}  // namespace

namespace detail {

void pushRx(const RxItem& item) {
    if (xQueueSend(rxQueue, &item, 0) != pdTRUE)
        updateStats([](Stats& s) { s.rxDropped++; });
}

void lockStats() { portENTER_CRITICAL(&statsMux); }
void unlockStats() { portEXIT_CRITICAL(&statsMux); }
Stats& statsRef() { return st; }

bool injectDrop(const uint8_t* data, size_t len) {
    bool drop = (lossPct && (esp_random() % 100) < lossPct) ||
                (dropUp && len > 3 && data[3] == 0);
    if (drop) updateStats([](Stats& s) { s.injected++; });
    return drop;
}

}  // namespace detail

bool begin(uint8_t radio, uint8_t channel) {
    rxQueue = xQueueCreate(RX_QUEUE_LEN, sizeof(RxItem));
    if (radio == cfg::RADIO_OFF) {
        curRadio = cfg::RADIO_OFF;
        return true;
    }
    if (radio == cfg::RADIO_LORA && lora::available()) {
        curRadio = cfg::RADIO_LORA;
        return lora::begin(channel);
    }
    curRadio = cfg::RADIO_ESPNOW;
    return espnow::begin(cfg::ESPNOW_CHANNEL);
}

uint8_t radio() { return curRadio; }

bool send(const uint8_t* data, size_t len) {
    if (curRadio == cfg::RADIO_OFF) return false;
    if (curRadio == cfg::RADIO_LORA)
        return lora::send(
            data, len);  // Loss injection is decided right before transmission
    if (detail::injectDrop(data, len)) return true;
    return espnow::send(data, len);
}

bool receive(RxItem& item) {
    return rxQueue && xQueueReceive(rxQueue, &item, 0) == pdTRUE;
}

void setChannel(uint8_t channel) {
    if (curRadio == cfg::RADIO_LORA) lora::setChannel(channel);
}

void setLossInjection(uint8_t pct, bool up) {
    lossPct = pct;
    dropUp = up;
}

void end() {
    if (curRadio == cfg::RADIO_LORA) lora::end();
}

Stats stats() {
    detail::lockStats();
    Stats s = st;
    detail::unlockStats();
    return s;
}

void ownMac(uint8_t mac[6]) { esp_read_mac(mac, ESP_MAC_WIFI_STA); }

bool loraAvailable() { return lora::available(); }

}  // namespace net
