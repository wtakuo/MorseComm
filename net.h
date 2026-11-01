// net.h - Radio send/receive (common interface for ESP-NOW / LoRa)
// Reception only records the receive time in the interrupt/callback and pushes
// it to a queue; processing is done in the caller's task. Implementation:
// net.cpp (common part: receive queue, stats, loss injection) / net_espnow.cpp
// / net_lora.cpp (Nesso N1 only)
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "protocol.h"

namespace net {

struct RxItem {
    uint32_t rxMs;  // millis() at the moment of reception
    int8_t rssi;    // dBm (for LoRa, the value at the antenna with the LNA gain
                    // subtracted)
    uint8_t len;
    uint8_t src[6];  // Sender (ESP-NOW: MAC address / LoRa: sender MAC address
                     // carried in the packet)
    uint8_t data[cw::PKT_MAX_SIZE];
};

struct Stats {
    uint32_t sent = 0;     // Sent (ESP-NOW: esp_now_send succeeded / LoRa:
                           // transmission complete)
    uint32_t sendErr = 0;  // Send errors
    uint32_t cbOk = 0;     // ESP-NOW send callback: success
    uint32_t cbFail = 0;   // ESP-NOW send callback: failure (normally does not
                           // occur with broadcast)
    uint32_t rxDropped = 0;  // Receive queue overflow
    uint32_t rxErr = 0;      // LoRa: CRC errors etc.
    uint32_t injected = 0;   // Not sent due to loss injection
    uint32_t csBusy = 0;     // LoRa: number of times carrier sense detected
                             // another station's signal and waited
    uint32_t airMs = 0;      // LoRa: total transmit (air) time
};

// radio = cfg::RADIO_*. With RADIO_OFF, neither Wi-Fi nor LoRa is initialized
// (no RF emission)
bool begin(uint8_t radio, uint8_t channel);
uint8_t radio();
// Send (does nothing with RADIO_OFF). LoRa keeps only the latest packet and
// sends it when transmission is allowed (older ones are overwritten)
bool send(const uint8_t* data, size_t len);
bool receive(RxItem& item);  // Take one item from the queue (does not wait)
// The communication channel changed (LoRa changes frequency)
void setChannel(uint8_t channel);
// Loss injection for hardware testing (decided right before sending)
void setLossInjection(uint8_t pct, bool dropUp);
// Stop the radio before sleep
void end();
Stats stats();
void ownMac(uint8_t mac[6]);
bool loraAvailable();  // Whether LoRa is available on this board

}  // namespace net
