// net_lora.cpp — LoRa (SX1262) の送受信 (Arduino Nesso N1 のみ)
//
// 920MHz 帯の特定小電力無線局 (20mW 以下) として、キャリアセンス 5ms 以上の区分の条件を守る:
//   - 送信前に 5ms 以上キャリアセンスし、-80dBm (給電線入力点) 以上の電波があれば送らない
//   - 送信後 50ms 以上休止する (1 回の送信は約 7ms で、4 秒の上限よりずっと短い)
// 設定値は config.h の LORA_*。
//
// 専用タスクで動かす。エンジン (1ms 周期) は send() で最新のパケットを置いていくだけで、
// このタスクが「送ってよい時」になったら、その時点の最新パケットの送信時刻 (と履歴の age) を
// 付け替えて送る。キー操作の遷移があればエンジンがすぐ組み立て直すので、内容は送信時点の状態と一致する。
// 通番は実際に送ったパケットに振り直す (受信側のロス率推定のため)。
//
// LoRa には送信元アドレスが無いので、無線フレームは [送信元 MAC 6 バイト][cw パケット] とする。
// 全局が同じ周波数でブロードキャストし合う対等な関係 (親機・子機の区別なし)。
#include "board.h"
#include "net_internal.h"

#if defined(BOARD_NESSO_N1)
#include <Arduino.h>
#include <Arduino_Nesso_N1.h>
#include <RadioLib.h>
#include <esp_random.h>
#include <string.h>
#include "config.h"
#include "hw.h"

namespace net {
namespace lora {
namespace {

constexpr size_t SRC_LEN = 6;
constexpr size_t FRAME_MAX = SRC_LEN + cw::PKT_MAX_SIZE;
constexpr UBaseType_t TASK_PRIORITY = 4;  // エンジン (5) より低く、loop() (1) より高い
constexpr uint32_t TASK_STACK = 4096;
constexpr uint32_t IDLE_POLL_MS = 20;
constexpr uint32_t BACKOFF_MIN_MS = 2, BACKOFF_MAX_MS = 12;  // キャリアセンスで他局を検出した後の待ち

SX1262 radio = new Module(LORA_CS, LORA_IRQ, RADIOLIB_NC, LORA_BUSY);
TaskHandle_t task = nullptr;
volatile bool ready = false;
uint8_t ownMacAddr[SRC_LEN];

// DIO1 割り込み (送信完了・受信完了)
volatile bool dio1 = false;
volatile uint32_t dio1Ms = 0;

// エンジン・loop() → タスク: 送信待ちの最新パケット、チャンネル変更、停止
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
uint8_t mbox[cw::PKT_MAX_SIZE];
size_t mboxLen = 0;
volatile uint8_t wantChannel = 0;
volatile bool stopReq = false;

// タスク内の状態
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

float freqOf(uint8_t ch) { return cfg::LORA_FREQ_BASE_MHZ + cfg::LORA_FREQ_STEP_MHZ * (ch - cfg::CH_MIN); }

// SX1262 の RSSI 読み値 → アンテナ端の dBm
int8_t antennaDbm(float raw) {
  float v = raw - cfg::LORA_LNA_GAIN_DB;
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

// 以下、SPI を使う関数はバスをロックしてから呼ぶ

// DIO1: 送信完了または受信完了
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
  int st = len <= sizeof(buf) ? radio.readData(buf, len) : RADIOLIB_ERR_PACKET_TOO_LONG;
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

// キャリアセンス: LORA_CS_US の間、受信レベルが閾値未満なら Clear
Cs carrierSense() {
  const float level = cfg::LORA_CS_LEVEL_DBM + cfg::LORA_LNA_GAIN_DB;  // SX1262 の読み値での閾値
  uint32_t t0 = micros();
  while (micros() - t0 < cfg::LORA_CS_US) {
    if (dio1) return Cs::RxDone;  // 受信が完了した: 先に処理する
    if (radio.getRSSI(false) >= level) return Cs::Busy;
  }
  return Cs::Clear;
}

void tryTransmit() {
  Cs cs = carrierSense();
  if (cs == Cs::Busy) {
    detail::updateStats([](Stats& s) { s.csBusy++; });
    backoffUntil = millis() + BACKOFF_MIN_MS + esp_random() % (BACKOFF_MAX_MS - BACKOFF_MIN_MS + 1);
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
    txEndMs = now;  // 送ったものとして休止する (無線区間でのロスを模擬)
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
      // 休止 (前回の送信終了から) とバックオフが明けるまで待つ
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
      // 送信完了の割り込みが来なかった: 受信に戻す
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
    if (!txActive && mboxPending() && (int32_t)(now - txEndMs) >= (int32_t)cfg::LORA_PAUSE_MS &&
        (int32_t)(now - backoffUntil) >= 0) {
      tryTransmit();
    }
    hw::unlockBus();
  }
  // 停止: 送信中なら終わるのを待ってから眠らせる
  hw::lockBus();
  uint32_t t0 = millis();
  while (txActive && !dio1 && millis() - t0 < cfg::LORA_TX_TIMEOUT_MS) delay(1);
  radio.sleep();
  hw::unlockBus();
  ready = false;
  task = nullptr;
  vTaskDelete(nullptr);
}

}  // namespace

bool available() { return true; }

bool begin(uint8_t channel) {
  net::ownMac(ownMacAddr);
  txSeq = (uint16_t)esp_random();
  // 制御線は IO エキスパンダ (0x43): P7 = リセット (HIGH で動作), P6 = RF スイッチの電源, P5 = 受信 LNA
  pinMode(LORA_ENABLE, OUTPUT);
  pinMode(LORA_ANTENNA_SWITCH, OUTPUT);
  pinMode(LORA_LNA_ENABLE, OUTPUT);
  digitalWrite(LORA_ENABLE, LOW);
  delay(10);
  digitalWrite(LORA_ENABLE, HIGH);
  delay(10);
  digitalWrite(LORA_ANTENNA_SWITCH, HIGH);
  digitalWrite(LORA_LNA_ENABLE, HIGH);

  hw::lockBus();
  int st = radio.begin(freqOf(channel), cfg::LORA_BW_KHZ, cfg::LORA_SF, cfg::LORA_CR, cfg::LORA_SYNC_WORD,
                       cfg::LORA_POWER_DBM, cfg::LORA_PREAMBLE, cfg::LORA_TCXO_V, true);
  if (st == RADIOLIB_ERR_NONE) st = radio.setDio2AsRfSwitch(true);  // 送受信の切り替えは DIO2 (データシート §6.2.2)
  if (st == RADIOLIB_ERR_NONE) {
    radio.setDio1Action(onDio1);
    st = radio.startReceive();
  }
  hw::unlockBus();
  if (st != RADIOLIB_ERR_NONE) {
    Serial.printf("LoRa: init failed (%d)\n", st);
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
  digitalWrite(LORA_LNA_ENABLE, LOW);
  digitalWrite(LORA_ANTENNA_SWITCH, LOW);
}

}  // namespace lora
}  // namespace net

#else  // LoRa の無いボード

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
