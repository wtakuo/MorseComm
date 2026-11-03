// net_lora.cpp - LoRa (SX1262) send/receive
// Arduino Nesso N1 (built-in) and Cardputer ADV with Cap LoRa-1262
// (the Cap is detected at run time; the original Cardputer has no bus for it)
//
// As a 920 MHz band specified low-power radio station (20 mW or less), comply
// with the conditions of the carrier sense >= 5 ms category:
//   - Carrier sense for at least 5 ms before transmitting; do not transmit if a
//   signal of -80 dBm (antenna input / feed point) or more is present
//   - Pause at least 50 ms after transmitting (one transmission is about 25 ms,
//   far shorter than the 4 s limit)
// Settings are LORA_* in config.h.
//
// Runs in a dedicated task. The engine (1 ms period) only drops the latest
// packet via send(); when this task reaches a time it may transmit, it
// re-stamps the send time (and the history ages) of the latest packet at that
// moment and sends it. If a key transition occurs, the engine rebuilds the
// packet immediately, so the content matches the state at send time. Sequence
// numbers are reassigned to the packets actually sent (for the receiver's loss
// rate estimate).
//
// LoRa has no source address, so the radio frame is [sender MAC 6 bytes][cw
// packet]. All stations broadcast to each other on the same frequency as peers
// (no master/slave distinction).
#include "board.h"
#include "build_info.h"
BUILD_INFO_UNIT();
#include "net_internal.h"

#if defined(BOARD_NESSO_N1) || defined(BOARD_CARDPUTER)
#include <Arduino.h>
#include <RadioLib.h>
#include <esp_random.h>
#include <string.h>
#include "config.h"
#include "hw.h"
#if defined(BOARD_NESSO_N1)
#include <Arduino_Nesso_N1.h>
#else
#include <M5Unified.h>
#include <SPI.h>
#include "utility/PI4IOE5V6408_Class.hpp"
#endif

namespace net {
namespace lora {
namespace {

// ---- Board-specific hardware ----
#if defined(BOARD_NESSO_N1)
// SPI is shared with the display (default SPI pins); no reset line
SX1262 radio = new Module(LORA_CS, LORA_IRQ, RADIOLIB_NC, LORA_BUSY);
constexpr int8_t POWER_DBM = cfg::LORA_POWER_DBM_NESSO;
constexpr int LNA_GAIN_DB = cfg::LORA_LNA_GAIN_DB;

bool probe() { return true; }

// Control lines are on the IO expander (0x43): P7 = reset (HIGH to run), P6
// = RF switch power, P5 = RX LNA
void powerUp() {
    pinMode(LORA_ENABLE, OUTPUT);
    pinMode(LORA_ANTENNA_SWITCH, OUTPUT);
    pinMode(LORA_LNA_ENABLE, OUTPUT);
    digitalWrite(LORA_ENABLE, LOW);
    delay(10);
    digitalWrite(LORA_ENABLE, HIGH);
    delay(10);
    digitalWrite(LORA_ANTENNA_SWITCH, HIGH);
    digitalWrite(LORA_LNA_ENABLE, HIGH);
}

void powerDown() {
    digitalWrite(LORA_LNA_ENABLE, LOW);
    digitalWrite(LORA_ANTENNA_SWITCH, LOW);
}
#else
// Cardputer ADV + Cap LoRa-1262 (EXT 14-pin bus). The Cap's SPI (SCK G40,
// MISO G39, MOSI G14) is shared only with the microSD slot (CS G12), not with
// the display (SPI3, G35/G36), so no bus lock is needed. NSS G5, DIO1 G4,
// NRST G3, BUSY G6. The RF switch (FM8625H) is powered from P0 of the Cap's
// PI4IOE5V6408 (0x43, internal I2C G8/G9) and switched TX/RX by DIO2.
// TCXO 3.0V on DIO3. No external LNA
constexpr int PIN_SCK = 40, PIN_MISO = 39, PIN_MOSI = 14, PIN_NSS = 5;
constexpr int PIN_IRQ = 4, PIN_RST = 3, PIN_BUSY = 6, PIN_SD_CS = 12;
constexpr uint8_t IOE_ADDR = 0x43, IOE_ANT_SW = 0;
SPIClass loraSpi(FSPI);
SX1262 radio = new Module(PIN_NSS, PIN_IRQ, PIN_RST, PIN_BUSY, loraSpi);
m5::PI4IOE5V6408_Class ioe(IOE_ADDR, 400000, &m5::In_I2C);
constexpr int8_t POWER_DBM = cfg::LORA_POWER_DBM_CAP;
constexpr int LNA_GAIN_DB = 0;

// The Cap can only be attached to the ADV (on the original Cardputer, G8/G9
// are keyboard matrix lines, so I2C must not be used there)
bool probe() {
    static int8_t found = -1;
    if (found < 0) found = hw::hasPhoneJack() && ioe.begin() ? 1 : 0;
    return found == 1;
}

void powerUp() {
    // Keep a microSD card (if inserted) off the shared bus
    pinMode(PIN_SD_CS, OUTPUT);
    digitalWrite(PIN_SD_CS, HIGH);
    ioe.setDirection(IOE_ANT_SW, true);
    ioe.setHighImpedance(IOE_ANT_SW, false);
    ioe.digitalWrite(IOE_ANT_SW, true);
    loraSpi.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_NSS);
}

void powerDown() { ioe.digitalWrite(IOE_ANT_SW, false); }
#endif

constexpr size_t SRC_LEN = 6;
constexpr size_t FRAME_MAX = SRC_LEN + cw::PKT_MAX_SIZE;
constexpr UBaseType_t TASK_PRIORITY =
    4;  // Lower than the engine (5), higher than loop() (1)
constexpr uint32_t TASK_STACK = 4096;
constexpr uint32_t IDLE_POLL_MS = 20;
constexpr uint32_t BACKOFF_MIN_MS = 2,
                   BACKOFF_MAX_MS =
                       12;  // Wait after carrier sense detects another station

TaskHandle_t task = nullptr;
volatile bool ready = false;
uint8_t ownMacAddr[SRC_LEN];

// DIO1 interrupt (TX done / RX done)
volatile bool dio1 = false;
volatile uint32_t dio1Ms = 0;

// Engine/loop() -> task: latest packet waiting to be sent, channel change, stop
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
uint8_t mbox[cw::PKT_MAX_SIZE];
size_t mboxLen = 0;
volatile uint8_t wantChannel = 0;
volatile bool stopReq = false;

// State inside the task
uint8_t curChannel = 0;
bool txActive = false;
uint32_t txStartMs = 0, txEndMs = 0, backoffUntil = 0;
uint16_t txSeq = 0;

void IRAM_ATTR onDio1() {
    dio1Ms = millis();
    dio1 = true;
    BaseType_t woken = pdFALSE;
    if (task) vTaskNotifyGiveFromISR(task, &woken);
    portYIELD_FROM_ISR(woken);
}

float freqOf(uint8_t ch) {
    return cfg::LORA_FREQ_BASE_MHZ +
           cfg::LORA_FREQ_STEP_MHZ * (ch - cfg::CH_MIN);
}

// SX1262 RSSI reading -> dBm at the antenna
int8_t antennaDbm(float raw) {
    float v = raw - LNA_GAIN_DB;
    if (v < -128) v = -128;
    if (v > 0) v = 0;
    return (int8_t)v;
}

bool mboxPending() {
    portENTER_CRITICAL(&mux);
    bool p = mboxLen > 0;
    portEXIT_CRITICAL(&mux);
    return p;
}

// Functions below that use SPI must be called with the bus locked

// DIO1: TX done or RX done
void handleDio1() {
    dio1 = false;
    uint32_t t = dio1Ms;
    if (txActive) {
        radio.finishTransmit();
        txActive = false;
        txEndMs = t;
        uint32_t air = t - txStartMs;
        detail::updateStats([air](Stats& s) {
            s.sent++;
            s.airMs += air;
        });
        radio.startReceive();
        return;
    }
    uint8_t buf[FRAME_MAX];
    size_t len = radio.getPacketLength();
    int st = len <= sizeof(buf) ? radio.readData(buf, len)
                                : RADIOLIB_ERR_PACKET_TOO_LONG;
    float rssi = radio.getRSSI();
    radio.startReceive();
    if (st != RADIOLIB_ERR_NONE || len <= SRC_LEN) {
        detail::updateStats([](Stats& s) { s.rxErr++; });
        return;
    }
    RxItem item;
    item.rxMs = t;
    item.rssi = antennaDbm(rssi);
    memcpy(item.src, buf, SRC_LEN);
    item.len = (uint8_t)(len - SRC_LEN);
    memcpy(item.data, buf + SRC_LEN, item.len);
    detail::pushRx(item);
}

enum class Cs { Clear, Busy, RxDone };

// Carrier sense: Clear if the received level stays below the threshold for
// LORA_CS_US
Cs carrierSense() {
    // Threshold in terms of the SX1262 reading
    const float level = cfg::LORA_CS_LEVEL_DBM + LNA_GAIN_DB;
    uint32_t t0 = micros();
    while (micros() - t0 < cfg::LORA_CS_US) {
        if (dio1) return Cs::RxDone;  // Reception completed: handle it first
        if (radio.getRSSI(false) >= level) return Cs::Busy;
    }
    return Cs::Clear;
}

void tryTransmit() {
    Cs cs = carrierSense();
    if (cs == Cs::Busy) {
        detail::updateStats([](Stats& s) { s.csBusy++; });
        backoffUntil = millis() + BACKOFF_MIN_MS +
                       esp_random() % (BACKOFF_MAX_MS - BACKOFF_MIN_MS + 1);
    }
    if (cs != Cs::Clear) return;
    uint8_t frame[FRAME_MAX];
    size_t len;
    portENTER_CRITICAL(&mux);
    len = mboxLen;
    memcpy(frame + SRC_LEN, mbox, len);
    mboxLen = 0;
    portEXIT_CRITICAL(&mux);
    if (!len) return;
    uint32_t now = millis();
    uint8_t* pkt = frame + SRC_LEN;
    cw::restampPacket(pkt, len, now);
    cw::setPacketSeq(pkt, txSeq++);
    if (detail::injectDrop(pkt, len)) {
        txEndMs =
            now;  // Pause as if it had been sent (simulates loss over the air)
        return;
    }
    memcpy(frame, ownMacAddr, SRC_LEN);
    if (radio.startTransmit(frame, SRC_LEN + len) == RADIOLIB_ERR_NONE) {
        txActive = true;
        txStartMs = now;
    } else {
        detail::updateStats([](Stats& s) { s.sendErr++; });
        txEndMs = now;
        radio.startReceive();
    }
}

void taskFn(void*) {
    for (;;) {
        uint32_t now = millis();
        uint32_t wait = IDLE_POLL_MS;
        if (txActive) {
            wait = 2;
        } else if (mboxPending()) {
            // Wait until the pause (from the end of the previous transmission)
            // and backoff are over
            uint32_t readyAt = txEndMs + cfg::LORA_PAUSE_MS;
            if ((int32_t)(backoffUntil - readyAt) > 0) readyAt = backoffUntil;
            int32_t dt = (int32_t)(readyAt - now);
            wait = dt > 0 ? (uint32_t)dt : 0;
        }
        if (wait && !dio1) ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait));

        if (stopReq) break;
        hw::lockBus();
        if (dio1) handleDio1();
        if (txActive && millis() - txStartMs > cfg::LORA_TX_TIMEOUT_MS) {
            // TX done interrupt did not come: return to receive
            radio.finishTransmit();
            txActive = false;
            txEndMs = millis();
            detail::updateStats([](Stats& s) { s.sendErr++; });
            radio.startReceive();
        }
        if (!txActive && wantChannel != curChannel) {
            curChannel = wantChannel;
            radio.standby();
            radio.setFrequency(freqOf(curChannel));
            radio.startReceive();
        }
        now = millis();
        if (!txActive && mboxPending() &&
            (int32_t)(now - txEndMs) >= (int32_t)cfg::LORA_PAUSE_MS &&
            (int32_t)(now - backoffUntil) >= 0) {
            tryTransmit();
        }
        hw::unlockBus();
    }
    // Stop: if transmitting, wait for it to finish before putting the radio to
    // sleep
    hw::lockBus();
    uint32_t t0 = millis();
    while (txActive && !dio1 && millis() - t0 < cfg::LORA_TX_TIMEOUT_MS)
        delay(1);
    radio.sleep();
    hw::unlockBus();
    ready = false;
    task = nullptr;
    vTaskDelete(nullptr);
}

}  // namespace

bool available() { return probe(); }

bool begin(uint8_t channel) {
    if (!probe()) return false;
    net::ownMac(ownMacAddr);
    txSeq = (uint16_t)esp_random();
    powerUp();

    hw::lockBus();
    int st = radio.begin(freqOf(channel), cfg::LORA_BW_KHZ, cfg::LORA_SF,
                         cfg::LORA_CR, cfg::LORA_SYNC_WORD, POWER_DBM,
                         cfg::LORA_PREAMBLE, cfg::LORA_TCXO_V, true);
    if (st == RADIOLIB_ERR_NONE)
        st = radio.setDio2AsRfSwitch(
            true);  // TX/RX switching is done by DIO2 (datasheet section 6.2.2)
    if (st == RADIOLIB_ERR_NONE) {
        radio.setDio1Action(onDio1);
        st = radio.startReceive();
    }
    hw::unlockBus();
    if (st != RADIOLIB_ERR_NONE) {
        powerDown();
        Serial.printf("LoRa: init failed (%d)\r\n", st);
        return false;
    }
    curChannel = wantChannel = channel;
    stopReq = false;
    ready = true;
    xTaskCreate(taskFn, "lora", TASK_STACK, nullptr, TASK_PRIORITY, &task);
    return true;
}

bool send(const uint8_t* data, size_t len) {
    if (!ready || len > sizeof(mbox)) return false;
    portENTER_CRITICAL(&mux);
    memcpy(mbox, data, len);
    mboxLen = len;
    portEXIT_CRITICAL(&mux);
    if (task) xTaskNotifyGive(task);
    return true;
}

void setChannel(uint8_t channel) {
    wantChannel = channel;
    if (task) xTaskNotifyGive(task);
}

void end() {
    if (!ready) return;
    stopReq = true;
    if (task) xTaskNotifyGive(task);
    uint32_t t0 = millis();
    while (ready && millis() - t0 < 300) delay(2);
    powerDown();
}

}  // namespace lora
}  // namespace net

#else  // Boards without LoRa

namespace net {
namespace lora {
bool available() { return false; }
bool begin(uint8_t) { return false; }
bool send(const uint8_t*, size_t) { return false; }
void setChannel(uint8_t) {}
void end() {}
}  // namespace lora
}  // namespace net

#endif
