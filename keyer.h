// keyer.h — キー入力処理: デバウンス、イアンビックキーヤー、テキスト自動キーイング
// Arduino 非依存。時刻は引数 nowMs で渡す。
// どのモードも出力は「キー状態 (DOWN/UP) の遷移列」(KeyEvent) になる (§5)。
#pragma once
#include <stdint.h>
#include <string.h>
#include "config.h"
#include "morse_table.h"

namespace cw {

enum class KeyMode : uint8_t { Straight = 0, IambicA = 1, IambicB = 2 };
constexpr int KEY_MODE_COUNT = 3;

inline const char* keyModeName(KeyMode m) {
  switch (m) {
    case KeyMode::Straight: return "STRAIGHT";
    case KeyMode::IambicA: return "IAMBIC_A";
    case KeyMode::IambicB: return "IAMBIC_B";
  }
  return "?";
}

inline uint32_t ditMsForWpm(int wpm) { return (1200 + wpm / 2) / wpm; }

struct KeyEvent {
  uint32_t t;      // 遷移時刻 (正確な予定時刻。update の呼び出しが遅れても補正される)
  uint8_t state;   // 1=DOWN, 0=UP
};

// 小さなイベントキュー
class KeyEventQueue {
 public:
  static constexpr int SIZE = 16;
  void push(uint32_t t, uint8_t st) {
    int next = (tail_ + 1) % SIZE;
    if (next == head_) head_ = (head_ + 1) % SIZE;
    q_[tail_] = KeyEvent{t, st};
    tail_ = next;
  }
  bool pop(KeyEvent& ev) {
    if (head_ == tail_) return false;
    ev = q_[head_];
    head_ = (head_ + 1) % SIZE;
    return true;
  }
  void clear() { head_ = tail_ = 0; }

 private:
  KeyEvent q_[SIZE];
  int head_ = 0, tail_ = 0;
};

// ロックアウト型デバウンス: 最初のエッジを即座に採用し、その後 DEBOUNCE_MS は変化を無視する
// (遅延ゼロで、接点のバウンスだけを除く)
class Debouncer {
 public:
  bool update(bool raw, uint32_t nowMs) {
    if (raw != stable_ && (nowMs - lastChangeMs_) >= cfg::DEBOUNCE_MS) {
      stable_ = raw;
      lastChangeMs_ = nowMs;
      changed_ = true;
    }
    return stable_;
  }
  bool state() const { return stable_; }
  uint32_t lastChangeMs() const { return lastChangeMs_; }
  // 前回呼んでから変化したか
  bool takeChanged() {
    bool c = changed_;
    changed_ = false;
    return c;
  }

 private:
  bool stable_ = false;
  bool changed_ = false;
  uint32_t lastChangeMs_ = 0;
};

// イアンビックキーヤー
//  Mode A: 次の要素は、前の要素間スペースの終わり時点のパドル状態で決める
//  Mode B: 要素の送出中 (マーク+スペース) に反対側パドルが押されたことをラッチする。
//          スクイーズを要素の途中で離しても、反対側の要素をもう1つ送る
class IambicKeyer {
 public:
  void setWpm(int wpm) { ditMs_ = ditMsForWpm(wpm); }
  uint32_t ditMs() const { return ditMs_; }
  void setModeB(bool b) { modeB_ = b; }
  bool down() const { return phase_ == Phase::Mark; }
  bool idle() const { return phase_ == Phase::Idle; }

  void reset() {
    phase_ = Phase::Idle;
    memDit_ = memDah_ = false;
    events_.clear();
  }

  void update(uint32_t nowMs, bool dit, bool dah) {
    if (phase_ != Phase::Idle && modeB_) {
      bool curDit = elem_ == Elem::Dit;
      // 反対側: 押されていればラッチ。同じ側: 新たに押されたらラッチ
      if (curDit ? dah : dit) (curDit ? memDah_ : memDit_) = true;
      if (curDit ? (dit && !prevDit_) : (dah && !prevDah_)) (curDit ? memDit_ : memDah_) = true;
    }
    prevDit_ = dit;
    prevDah_ = dah;

    for (int guard = 0; guard < 8; guard++) {
      if (phase_ == Phase::Idle) {
        if (dit) start(Elem::Dit, nowMs);
        else if (dah) start(Elem::Dah, nowMs);
        break;
      }
      if (timeBefore(nowMs, tEnd_)) break;
      if (phase_ == Phase::Mark) {
        events_.push(tEnd_, 0);
        phase_ = Phase::Space;
        tEnd_ += ditMs_;
        continue;
      }
      // スペース終了: 次の要素を決める
      Elem opp = elem_ == Elem::Dit ? Elem::Dah : Elem::Dit;
      bool oppPressed = opp == Elem::Dit ? dit : dah;
      bool samePressed = elem_ == Elem::Dit ? dit : dah;
      bool oppMem = opp == Elem::Dit ? memDit_ : memDah_;
      bool sameMem = elem_ == Elem::Dit ? memDit_ : memDah_;
      if (oppPressed || (modeB_ && oppMem)) start(opp, tEnd_);
      else if (samePressed || (modeB_ && sameMem)) start(elem_, tEnd_);
      else {
        phase_ = Phase::Idle;
        break;
      }
    }
  }

  bool pop(KeyEvent& ev) { return events_.pop(ev); }

 private:
  enum class Phase : uint8_t { Idle, Mark, Space };
  enum class Elem : uint8_t { Dit, Dah };

  static bool timeBefore(uint32_t a, uint32_t b) { return (int32_t)(a - b) < 0; }

  void start(Elem e, uint32_t t) {
    elem_ = e;
    phase_ = Phase::Mark;
    tEnd_ = t + (e == Elem::Dit ? ditMs_ : 3 * ditMs_);
    memDit_ = memDah_ = false;
    events_.push(t, 1);
  }

  uint32_t ditMs_ = ditMsForWpm(cfg::WPM_DEFAULT);
  bool modeB_ = true;
  Phase phase_ = Phase::Idle;
  Elem elem_ = Elem::Dit;
  uint32_t tEnd_ = 0;
  bool memDit_ = false, memDah_ = false;
  bool prevDit_ = false, prevDah_ = false;
  KeyEventQueue events_;
};

// テキストを指定 WPM で自動キーイングする (テストモード用)。末尾に語間をおいて繰り返す
class TextKeyer {
 public:
  static constexpr int MAX_TEXT = 64;

  void start(const char* text, int wpm, uint32_t nowMs) {
    strncpy(text_, text, MAX_TEXT - 1);
    text_[MAX_TEXT - 1] = 0;
    ditMs_ = ditMsForWpm(wpm);
    chr_ = 0;
    el_ = 0;
    down_ = false;
    tNext_ = nowMs;
    running_ = text_[0] != 0;
    events_.clear();
  }
  void stop() { running_ = false; }
  bool running() const { return running_; }
  bool down() const { return down_; }

  void update(uint32_t nowMs) {
    if (!running_) {
      if (down_) {
        down_ = false;
        events_.push(nowMs, 0);
      }
      return;
    }
    for (int guard = 0; guard < 16 && (int32_t)(nowMs - tNext_) >= 0; guard++) step();
  }

  bool pop(KeyEvent& ev) { return events_.pop(ev); }

 private:
  // 現在位置の符号 (空白・未対応文字は nullptr)
  const char* codeAt(int i) const { return text_[i] ? morseEncode(text_[i]) : nullptr; }

  void step() {
    if (down_) {
      // マーク終了 → スペースの長さを決める
      down_ = false;
      events_.push(tNext_, 0);
      const char* code = codeAt(chr_);
      el_++;
      uint32_t gap = 1;
      if (code[el_] == 0) {
        el_ = 0;
        chr_++;
        gap = 3;
        // 空白や末尾をまとめて語間にする
        while (text_[chr_] && !codeAt(chr_)) {
          chr_++;
          gap = 7;
        }
        if (!text_[chr_]) {
          chr_ = 0;
          gap = 7;
        }
      }
      tNext_ += gap * ditMs_;
    } else {
      while (text_[chr_] && !codeAt(chr_)) chr_++;
      if (!text_[chr_]) {  // 符号化できる文字がない
        running_ = false;
        return;
      }
      const char* code = codeAt(chr_);
      down_ = true;
      events_.push(tNext_, 1);
      tNext_ += (code[el_] == '-' ? 3 : 1) * ditMs_;
    }
  }

  char text_[MAX_TEXT] = {};
  uint32_t ditMs_ = 60;
  int chr_ = 0, el_ = 0;
  bool down_ = false;
  bool running_ = false;
  uint32_t tNext_ = 0;
  KeyEventQueue events_;
};

}  // namespace cw
