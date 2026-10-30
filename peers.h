// peers.h — 複数局の受信: 送信元 (MAC アドレス) ごとに Receiver とデコーダを持つ
// Arduino 非依存。同じチャンネルの局が 3 台以上いても、局ごとに遷移を復元・再生する。
// トーンは全局の再生状態の OR、デコードは局ごとに行う。
#pragma once
#include <stdint.h>
#include <string.h>
#include "config.h"
#include "jitter.h"
#include "morse_decoder.h"

namespace cw {

struct Peer {
  bool used = false;
  uint8_t mac[6] = {};
  Receiver rx;
  MorseDecoder dec;
  int8_t rssi = 0;
  uint32_t lastRxMs = 0;    // 最後にパケットを受けた時刻
  uint32_t lastPlayMs = 0;  // 最後に遷移を再生した時刻
  uint32_t lastEventMs = 0; // ログ用: 直前の遷移の再生時刻
};

class PeerTable {
 public:
  static constexpr int MAX = cfg::MAX_PEERS;

  void setDelay(uint32_t ms) {
    delay_ = ms;
    for (auto& p : peers_)
      if (p.used) p.rx.setDelay(ms);
  }

  // 送信元のスロットを返す (無ければ確保する)。index には 0..MAX-1 を返す
  Peer& get(const uint8_t mac[6], uint32_t nowMs, int& index) {
    int freeSlot = -1, oldest = -1;
    for (int i = 0; i < MAX; i++) {
      Peer& p = peers_[i];
      if (p.used && memcmp(p.mac, mac, 6) == 0) {
        index = i;
        return p;
      }
      if (!p.used) {
        if (freeSlot < 0) freeSlot = i;
      } else if (oldest < 0 || timeBefore(p.lastRxMs, peers_[oldest].lastRxMs)) {
        oldest = i;
      }
    }
    // 空きが無ければ、最も長く受信していない局を追い出す
    int i = freeSlot >= 0 ? freeSlot : oldest;
    if (peers_[i].used) retire(peers_[i]);
    Peer& p = peers_[i];
    p = Peer();
    p.used = true;
    memcpy(p.mac, mac, 6);
    p.rx.setDelay(delay_);
    p.lastRxMs = nowMs;
    index = i;
    return p;
  }

  Peer& at(int i) { return peers_[i]; }

  // チャンネル切り替え時など: 全局を忘れる (再生中の音も止まる)
  void clear() {
    for (auto& p : peers_) {
      if (p.used) retire(p);
      p.used = false;
    }
  }

  // どれかの局が DOWN を再生中か
  bool anyDown() const {
    for (auto& p : peers_)
      if (p.used && p.rx.state()) return true;
    return false;
  }

  // リンクが生きている局の数
  int activeCount(uint32_t nowMs) const {
    int n = 0;
    for (auto& p : peers_)
      if (p.used && p.rx.linkUp(nowMs)) n++;
    return n;
  }

  // 最後にパケットを受けた局 (無ければ nullptr)
  const Peer* latestRx() const { return latest([](const Peer& p) { return p.lastRxMs; }); }
  // 最後に遷移を再生した局 (無ければ nullptr)
  const Peer* latestPlay() const { return latest([](const Peer& p) { return p.lastPlayMs; }); }

  // 全局 (追い出した局を含む) の統計の合計
  RxStats totalStats() const {
    RxStats s = retired_;
    for (auto& p : peers_)
      if (p.used) add(s, p.rx.stats());
    return s;
  }

 private:
  template <typename F>
  const Peer* latest(F key) const {
    const Peer* best = nullptr;
    for (auto& p : peers_)
      if (p.used && (!best || timeBefore(key(*best), key(p)))) best = &p;
    return best;
  }

  static void add(RxStats& a, const RxStats& b) {
    a.packets += b.packets;
    a.lost += b.lost;
    a.stale += b.stale;
    a.resyncs += b.resyncs;
    a.late += b.late;
    a.watchdogs += b.watchdogs;
    a.reasserts += b.reasserts;
    a.restarts += b.restarts;
    if (timeBefore(a.lastRxMs, b.lastRxMs)) a.lastRxMs = b.lastRxMs;
  }

  void retire(const Peer& p) { add(retired_, p.rx.stats()); }

  Peer peers_[MAX];
  RxStats retired_;
  uint32_t delay_ = cfg::JITTER_DELAY_MS;
};

}  // namespace cw
