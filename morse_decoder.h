// morse_decoder.h — DOWN/UP のタイミング列から欧文をデコードする (§7)
// Arduino 非依存。入力は (時刻, 状態) の遷移列。
#pragma once
#include <stdint.h>
#include "config.h"
#include "morse_table.h"

namespace cw {

class MorseDecoder {
 public:
  static constexpr int MAX_ELEMS = 8;
  static constexpr int OUT_SIZE = 32;

  void reset() {
    nEl_ = 0;
    down_ = false;
    pendingSpace_ = false;
  }

  uint32_t ditMs() const { return ditMs_; }
  int wpm() const { return (int)((1200 + ditMs_ / 2) / ditMs_); }

  // 遷移を入力する
  void onTransition(uint8_t state, uint32_t tMs) {
    if (state && !down_) {
      uint32_t gap = tMs - lastMs_;
      if (started_) handleGap(gap);
      down_ = true;
    } else if (!state && down_) {
      handleMark(tMs - lastMs_);
      down_ = false;
    } else {
      return;
    }
    started_ = true;
    lastMs_ = tMs;
  }

  // 定期的に呼ぶ: UP が十分続いたら文字・語間を確定する (表示を遅らせないため)
  void update(uint32_t nowMs) {
    if (!down_ && started_) handleGap(nowMs - lastMs_);
  }

  // デコード済み文字を取り出す
  bool pop(char& c) {
    if (outHead_ == outTail_) return false;
    c = out_[outHead_];
    outHead_ = (outHead_ + 1) % OUT_SIZE;
    return true;
  }

 private:
  void handleMark(uint32_t d) {
    bool dah = d >= 2 * ditMs_;
    // dit 長の適応推定 (dah は 1/3 として扱う)
    uint32_t sample = dah ? d / 3 : d;
    ditMs_ = (3 * ditMs_ + sample + 2) / 4;
    if (ditMs_ < cfg::DEC_DIT_MIN_MS) ditMs_ = cfg::DEC_DIT_MIN_MS;
    if (ditMs_ > cfg::DEC_DIT_MAX_MS) ditMs_ = cfg::DEC_DIT_MAX_MS;
    if (nEl_ < MAX_ELEMS) el_[nEl_] = dah ? '-' : '.';
    nEl_++;
    pendingSpace_ = true;
  }

  // UP の長さ gap に応じて文字・語間を確定する (同じ UP に対して何度呼ばれてもよい)
  void handleGap(uint32_t gap) {
    if (gap >= 2 * ditMs_ && nEl_ > 0) flushChar();
    if (gap >= 5 * ditMs_ && pendingSpace_) {
      emit(' ');
      pendingSpace_ = false;
    }
  }

  void flushChar() {
    char c = '?';
    if (nEl_ <= MAX_ELEMS) {
      char code[MAX_ELEMS + 1];
      for (int i = 0; i < nEl_; i++) code[i] = el_[i];
      code[nEl_] = 0;
      c = morseDecode(code);
    }
    nEl_ = 0;
    emit(c);
  }

  void emit(char c) {
    int next = (outTail_ + 1) % OUT_SIZE;
    if (next == outHead_) outHead_ = (outHead_ + 1) % OUT_SIZE;
    out_[outTail_] = c;
    outTail_ = next;
  }

  uint32_t ditMs_ = cfg::DEC_DIT_INIT_MS;
  char el_[MAX_ELEMS];
  int nEl_ = 0;
  bool down_ = false;
  bool started_ = false;
  bool pendingSpace_ = false;
  uint32_t lastMs_ = 0;
  char out_[OUT_SIZE];
  int outHead_ = 0, outTail_ = 0;
};

}  // namespace cw
