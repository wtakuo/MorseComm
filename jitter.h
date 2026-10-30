// jitter.h — 受信側: 遷移の復元、ジッタバッファ再生、ウォッチドッグ (§4.1, §4.5)
// Arduino 非依存。時刻は受信側ローカルの ms (uint32 wrap あり) を引数で渡す。
//
// 再生時間軸: 送信側時刻 T の遷移を、受信側時刻 T + offset + delay に再生する。
//   offset = 直近数秒の (rxMs - txMs) の最小値 (最もネットワーク遅延の小さいパケット)
// ウォッチドッグ: 再生位置が「受信済みの最新パケットの送信時刻 + WATCHDOG_MS」を
//   越えてもまだ DOWN なら、情報が途絶えたとみなして強制的に UP にする。
//   その後、同じ遷移IDのまま DOWN のパケットが届いた場合 (単なるロス) は DOWN を再開する。
#pragma once
#include <stdint.h>
#include "config.h"
#include "protocol.h"

namespace cw {

inline bool timeBefore(uint32_t a, uint32_t b) { return (int32_t)(a - b) < 0; }

// 時刻オフセット推定 (バケットごとの最小値, 直近 OFFSET_BUCKETS 個の最小)
class OffsetEstimator {
 public:
  void reset() { valid_ = false; }

  void addSample(int32_t off, uint32_t nowMs) {
    if (!valid_) {
      valid_ = true;
      bucketStart_ = nowMs;
      head_ = 0;
      for (int i = 0; i < cfg::OFFSET_BUCKETS; i++) has_[i] = false;
    }
    while (nowMs - bucketStart_ >= cfg::OFFSET_BUCKET_MS) {
      bucketStart_ += cfg::OFFSET_BUCKET_MS;
      head_ = (head_ + 1) % cfg::OFFSET_BUCKETS;
      has_[head_] = false;
      // 長時間空いた場合に while が長く回らないようにする
      if (nowMs - bucketStart_ >= cfg::OFFSET_BUCKET_MS * cfg::OFFSET_BUCKETS) {
        for (int i = 0; i < cfg::OFFSET_BUCKETS; i++) has_[i] = false;
        bucketStart_ = nowMs;
      }
    }
    if (!has_[head_] || off < min_[head_]) {
      min_[head_] = off;
      has_[head_] = true;
    }
  }

  bool valid() const { return valid_; }

  int32_t value() const {
    bool any = false;
    int32_t m = 0;
    for (int i = 0; i < cfg::OFFSET_BUCKETS; i++) {
      if (has_[i] && (!any || min_[i] < m)) {
        m = min_[i];
        any = true;
      }
    }
    return m;
  }

 private:
  bool valid_ = false;
  uint32_t bucketStart_ = 0;
  int head_ = 0;
  int32_t min_[cfg::OFFSET_BUCKETS] = {};
  bool has_[cfg::OFFSET_BUCKETS] = {};
};

enum class EventKind : uint8_t {
  Normal,    // パケットの履歴から復元した遷移
  Forced,    // ウォッチドッグによる強制 UP
  Reassert,  // 強制 UP 後に DOWN 継続が判明して再開
};

// 再生された遷移 (デコーダ・ログ用)
struct PlayedEvent {
  uint32_t playMs;   // 再生時間軸上の時刻 (遅延超過時は過去の値のこともある)
  uint8_t state;
  EventKind kind;
};

struct RxStats {
  uint32_t packets = 0;     // 受理したパケット
  uint32_t lost = 0;        // pktSeq の欠番から推定したロス
  uint32_t stale = 0;       // 重複・古い (新しい遷移を含まない逆順) パケット
  uint32_t resyncs = 0;     // 履歴数を越える欠落による再同期
  uint32_t late = 0;        // 再生時刻を過ぎてから届いた遷移
  uint32_t watchdogs = 0;   // ウォッチドッグによる強制 UP
  uint32_t reasserts = 0;   // 強制 UP 後の DOWN 再開
  uint32_t restarts = 0;    // 初期化 (初回・リンク断・相手再起動)
  uint32_t lastRxMs = 0;
};

class Receiver {
 public:
  static constexpr int QUEUE_SIZE = 64;
  static constexpr int OUT_SIZE = 64;

  void setDelay(uint32_t ms) { delay_ = ms; }
  uint32_t delay() const { return delay_; }

  // 現在の再生状態 (トーン出力用)
  uint8_t state() const { return playState_; }
  bool linkUp(uint32_t nowMs) const { return init_ && (nowMs - stats_.lastRxMs) < cfg::LINK_TIMEOUT_MS; }
  int32_t offset() const { return offset_.value(); }
  const RxStats& stats() const { return stats_; }

  // パケット受信時に呼ぶ。rxMs は受信した瞬間のローカル時刻
  void onPacket(const Packet& pk, uint32_t rxMs) {
    bool reinit = !init_ || (rxMs - stats_.lastRxMs) >= cfg::LINK_TIMEOUT_MS ||
                  (int32_t)(pk.txMs - horizonTx_) < -cfg::SENDER_RESTART_BACKSTEP_MS;

    if (init_ && !reinit) {
      countSeq(pk.pktSeq);
    } else {
      lastSeq_ = pk.pktSeq;
      seqSeen_ = 1;
    }
    stats_.packets++;
    stats_.lastRxMs = rxMs;

    if (reinit) {
      offset_.reset();
      stats_.restarts++;
    }
    offset_.addSample((int32_t)(rxMs - pk.txMs), rxMs);

    if (reinit) {
      init_ = true;
      horizonTx_ = pk.txMs;
      appliedTid_ = pk.lastTid;
      forcedUp_ = false;
      qHead_ = qTail_ = 0;
      lastQueuedMs_ = rxMs;
      lastQueuedState_ = playState_;
      // 初回は最新の遷移だけを採用する (古い履歴は再生しない)
      if (pk.state != playState_) {
        uint32_t t = pk.n > 0 ? pk.txMs - pk.age[0] : pk.txMs;
        enqueue(playTime(t), pk.state, EventKind::Normal, rxMs);
      }
      return;
    }

    if (timeBefore(horizonTx_, pk.txMs)) horizonTx_ = pk.txMs;

    uint16_t d = pk.lastTid - appliedTid_;
    if (d == 0) {
      // 新しい遷移なし。強制 UP 中に DOWN 継続が判明したら再開する
      if (forcedUp_ && pk.state == 1 && lastQueuedState_ == 0) {
        enqueue(playTime(pk.txMs), 1, EventKind::Reassert, rxMs);
        stats_.reasserts++;
        forcedUp_ = false;
      }
      return;
    }
    if (d >= 0x8000) {  // 自分より古いパケット (順序逆転・重複)
      stats_.stale++;
      return;
    }

    forcedUp_ = false;
    int avail = pk.n;
    if (d > avail) {
      // 履歴数を越える欠落: 見えている範囲で再同期する
      stats_.resyncs++;
      if (avail == 0) {
        enqueue(playTime(pk.txMs), pk.state, EventKind::Normal, rxMs);
      }
    }
    int first = (d < avail ? d : avail) - 1;
    for (int i = first; i >= 0; i--) {
      enqueue(playTime(pk.txMs - pk.age[i]), pk.stateOf(i), EventKind::Normal, rxMs);
    }
    appliedTid_ = pk.lastTid;
  }

  // 定期的に (1ms 程度ごとに) 呼ぶ。再生時刻に達した遷移を適用し、ウォッチドッグを判定する
  void update(uint32_t nowMs) {
    while (qHead_ != qTail_ && !timeBefore(nowMs, queue_[qHead_].playMs)) {
      apply(queue_[qHead_]);
      qHead_ = (qHead_ + 1) % QUEUE_SIZE;
    }
    if (init_ && playState_ == 1 && qHead_ == qTail_) {
      uint32_t horizonPlay = playTime(horizonTx_);
      if ((int32_t)(nowMs - horizonPlay) > (int32_t)cfg::WATCHDOG_MS) {
        PlayedEvent ev{nowMs, 0, EventKind::Forced};
        lastQueuedMs_ = nowMs;
        lastQueuedState_ = 0;
        apply(ev);
        forcedUp_ = true;
        stats_.watchdogs++;
      }
    }
  }

  // 再生済みの遷移を取り出す
  bool pop(PlayedEvent& ev) {
    if (outHead_ == outTail_) return false;
    ev = out_[outHead_];
    outHead_ = (outHead_ + 1) % OUT_SIZE;
    return true;
  }

 private:
  // pktSeq からロス数を数える。直近 64 通番の受信ビットマップで、遅れて届いた分は差し引く
  void countSeq(uint16_t seq) {
    uint16_t ds = seq - lastSeq_;
    if (ds == 0) return;
    if (ds < 0x8000) {
      stats_.lost += ds - 1;
      seqSeen_ = ds < 64 ? (seqSeen_ << ds) | 1 : 1;
      lastSeq_ = seq;
    } else {
      uint16_t back = lastSeq_ - seq;
      if (back < 64 && !(seqSeen_ & (1ULL << back))) {
        seqSeen_ |= 1ULL << back;
        if (stats_.lost) stats_.lost--;
      }
    }
  }

  uint32_t playTime(uint32_t senderMs) const { return senderMs + (uint32_t)offset_.value() + delay_; }

  void enqueue(uint32_t playMs, uint8_t st, EventKind kind, uint32_t nowMs) {
    if (st == lastQueuedState_) return;  // 交互にならない遷移 (再同期時) は捨てる
    if (timeBefore(playMs, lastQueuedMs_)) playMs = lastQueuedMs_;  // 時間軸を単調に保つ
    if (timeBefore(playMs, nowMs)) stats_.late++;
    int next = (qTail_ + 1) % QUEUE_SIZE;
    if (next == qHead_) {  // あふれた: 最古を即時適用して空ける
      apply(queue_[qHead_]);
      qHead_ = (qHead_ + 1) % QUEUE_SIZE;
    }
    queue_[qTail_] = PlayedEvent{playMs, st, kind};
    qTail_ = next;
    lastQueuedMs_ = playMs;
    lastQueuedState_ = st;
  }

  void apply(const PlayedEvent& ev) {
    if (ev.state == playState_) return;
    playState_ = ev.state;
    int next = (outTail_ + 1) % OUT_SIZE;
    if (next == outHead_) outHead_ = (outHead_ + 1) % OUT_SIZE;  // 読まれていなければ古いものを捨てる
    out_[outTail_] = ev;
    outTail_ = next;
  }

  uint32_t delay_ = cfg::JITTER_DELAY_MS;
  bool init_ = false;
  bool forcedUp_ = false;
  uint8_t playState_ = 0;
  uint16_t appliedTid_ = 0;
  uint16_t lastSeq_ = 0;
  uint64_t seqSeen_ = 0;  // bit k = 通番 lastSeq_-k を受信済み
  uint32_t horizonTx_ = 0;
  uint32_t lastQueuedMs_ = 0;
  uint8_t lastQueuedState_ = 0;
  OffsetEstimator offset_;
  RxStats stats_;

  PlayedEvent queue_[QUEUE_SIZE];
  int qHead_ = 0, qTail_ = 0;
  PlayedEvent out_[OUT_SIZE];
  int outHead_ = 0, outTail_ = 0;
};

}  // namespace cw
