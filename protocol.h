// protocol.h — パケット形式 (§4.3) と送信側の遷移履歴・送信スケジューラ
// Arduino 非依存。時刻はすべて引数 nowMs (ミリ秒, uint32 wrap あり) で受け取る。
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "config.h"

namespace cw {

constexpr uint8_t PKT_MAGIC = 0xC7;
constexpr uint8_t PKT_VERSION = 1;
constexpr size_t PKT_HEADER_SIZE = 13;
constexpr size_t PKT_MAX_SIZE = PKT_HEADER_SIZE + 2 * cfg::HIST_MAX;

struct Packet {
  uint8_t channel = 0;  // 通信チャンネル (同じ値の局どうしだけが受理する)
  uint8_t state = 0;     // 1=DOWN, 0=UP
  uint16_t pktSeq = 0;
  uint16_t lastTid = 0;  // 最新の遷移ID
  uint32_t txMs = 0;     // 送信側 millis()
  uint8_t n = 0;         // 履歴数
  uint16_t age[cfg::HIST_MAX] = {};  // i=0 が最新。txMs からの経過 ms (飽和)

  // 履歴 i の遷移後の状態 (最新 = state, 以降交互)
  uint8_t stateOf(int i) const { return (i & 1) ? (state ^ 1) : state; }
};

inline void put16(uint8_t* p, uint16_t v) { p[0] = v & 0xFF; p[1] = v >> 8; }
inline void put32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (v >> (8 * i)) & 0xFF; }
inline uint16_t get16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
inline uint32_t get32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// エンコード。書き込んだバイト数を返す (容量不足なら 0)
inline size_t encodePacket(const Packet& pk, uint8_t* buf, size_t cap) {
  if (pk.n > cfg::HIST_MAX) return 0;
  size_t len = PKT_HEADER_SIZE + 2 * pk.n;
  if (cap < len) return 0;
  buf[0] = PKT_MAGIC;
  buf[1] = PKT_VERSION;
  buf[2] = pk.channel;
  buf[3] = pk.state;
  put16(buf + 4, pk.pktSeq);
  put16(buf + 6, pk.lastTid);
  put32(buf + 8, pk.txMs);
  buf[12] = pk.n;
  for (int i = 0; i < pk.n; i++) put16(buf + PKT_HEADER_SIZE + 2 * i, pk.age[i]);
  return len;
}

// デコード。形式不正なら false (チャンネルの判定は呼び出し側)
inline bool decodePacket(const uint8_t* buf, size_t len, Packet& pk) {
  if (len < PKT_HEADER_SIZE) return false;
  if (buf[0] != PKT_MAGIC || buf[1] != PKT_VERSION) return false;
  if (buf[3] > 1) return false;
  uint8_t n = buf[12];
  if (n > cfg::HIST_MAX || len < PKT_HEADER_SIZE + 2 * (size_t)n) return false;
  pk.channel = buf[2];
  pk.state = buf[3];
  pk.pktSeq = get16(buf + 4);
  pk.lastTid = get16(buf + 6);
  pk.txMs = get32(buf + 8);
  pk.n = n;
  for (int i = 0; i < n; i++) pk.age[i] = get16(buf + PKT_HEADER_SIZE + 2 * i);
  return true;
}

// 送信側: キー状態の遷移履歴を保持し、パケットを組み立てる
class TxHistory {
 public:
  explicit TxHistory(uint16_t initialTid = 0) : lastTid_(initialTid) {}

  uint8_t state() const { return state_; }
  uint16_t lastTid() const { return lastTid_; }
  uint32_t lastChangeMs() const { return count_ ? times_[0] : 0; }

  // 状態遷移を記録する。同じ状態なら無視して false
  bool push(uint8_t newState, uint32_t tMs) {
    if (newState == state_) return false;
    for (int i = cfg::HIST_MAX - 1; i > 0; i--) times_[i] = times_[i - 1];
    times_[0] = tMs;
    if (count_ < cfg::HIST_MAX) count_++;
    state_ = newState;
    lastTid_++;
    return true;
  }

  void build(Packet& pk, uint32_t nowMs, int nHist, uint8_t channel, uint16_t pktSeq) const {
    pk.channel = channel;
    pk.state = state_;
    pk.pktSeq = pktSeq;
    pk.lastTid = lastTid_;
    pk.txMs = nowMs;
    int n = nHist < count_ ? nHist : count_;
    if (n > cfg::HIST_MAX) n = cfg::HIST_MAX;
    pk.n = (uint8_t)n;
    for (int i = 0; i < n; i++) {
      uint32_t a = nowMs - times_[i];
      pk.age[i] = a > 0xFFFF ? 0xFFFF : (uint16_t)a;
    }
  }

 private:
  uint8_t state_ = 0;
  uint16_t lastTid_;
  uint32_t times_[cfg::HIST_MAX] = {};
  int count_ = 0;
};

// 送信スケジューラ (§4.2): いつパケットを送るかを決める
class TxScheduler {
 public:
  // 遷移が発生したら呼ぶ (次の poll で即時送信)
  void onTransition(uint8_t newState, uint32_t nowMs) {
    state_ = newState;
    lastChangeMs_ = nowMs;
    pending_ = true;
  }

  // 今送るべきなら true (送信したものとして内部時刻を更新する)
  bool poll(uint32_t nowMs) {
    bool active = state_ == 1 || (nowMs - lastChangeMs_) < cfg::TX_ACTIVE_HOLD_MS;
    uint32_t interval = active ? cfg::TX_INTERVAL_ACTIVE_MS : cfg::TX_INTERVAL_IDLE_MS;
    if (pending_ || !started_ || (nowMs - lastSendMs_) >= interval) {
      pending_ = false;
      started_ = true;
      lastSendMs_ = nowMs;
      return true;
    }
    return false;
  }

 private:
  uint8_t state_ = 0;
  uint32_t lastChangeMs_ = 0;
  uint32_t lastSendMs_ = 0;
  bool pending_ = false;
  bool started_ = false;
};

}  // namespace cw
