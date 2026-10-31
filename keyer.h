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
// "[SK]" のように角括弧で囲んだ文字は、文字間を空けずに続けて送る (略符号)
class TextKeyer {
 public:
  static constexpr int MAX_TEXT = 64;

  void start(const char* text, int wpm, uint32_t nowMs) {
    build(text);
    ditMs_ = ditMsForWpm(wpm);
    pos_ = 0;
    down_ = false;
    tNext_ = nowMs;
    running_ = len_ > 0;
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
  // 列の記号: '.' '-' = 要素 (要素間は 1 dit)、間隔記号 GAP_* は直後の要素の前に置く
  static constexpr char GAP_JOIN = 'j';  // 略符号内の文字間 (1 dit)
  static constexpr char GAP_CHAR = 'c';  // 文字間 (3 dit)
  static constexpr char GAP_WORD = 'w';  // 語間 (7 dit)
  static constexpr int MAX_SEQ = MAX_TEXT * 8;

  void build(const char* text) {
    len_ = 0;
    bool inProsign = false, prevInProsign = false, word = false;
    for (int i = 0; text[i] && i < MAX_TEXT; i++) {
      char c = text[i];
      if (c == '[') {
        inProsign = true;
        continue;
      }
      if (c == ']') {
        inProsign = false;
        continue;
      }
      const char* code = morseEncode(c);
      if (!code) {  // 空白や未対応の文字は語間
        word = true;
        continue;
      }
      if (len_ > 0 && len_ < MAX_SEQ) {
        seq_[len_++] = word ? GAP_WORD : (inProsign && prevInProsign) ? GAP_JOIN : GAP_CHAR;
      }
      for (; *code && len_ < MAX_SEQ; code++) seq_[len_++] = *code;
      word = false;
      prevInProsign = inProsign;
    }
  }

  static uint32_t gapDits(char g) { return g == GAP_WORD ? 7 : g == GAP_CHAR ? 3 : 1; }

  void step() {
    if (!down_) {
      down_ = true;
      events_.push(tNext_, 1);
      tNext_ += (seq_[pos_] == '-' ? 3 : 1) * ditMs_;
      return;
    }
    down_ = false;
    events_.push(tNext_, 0);
    pos_++;
    uint32_t gap = 1;
    if (pos_ < len_ && (seq_[pos_] == GAP_JOIN || seq_[pos_] == GAP_CHAR || seq_[pos_] == GAP_WORD)) {
      gap = gapDits(seq_[pos_]);
      pos_++;
    }
    if (pos_ >= len_) {
      pos_ = 0;
      gap = 7;
    }
    tNext_ += gap * ditMs_;
  }

  char seq_[MAX_SEQ];
  int len_ = 0, pos_ = 0;
  uint32_t ditMs_ = 60;
  bool down_ = false;
  bool running_ = false;
  uint32_t tNext_ = 0;
  KeyEventQueue events_;
};

// 端末モード (シリアルで打った文字を送る) の送信待ちバッファ。
// 打った側 (loop) が末尾に追加・末尾を取り消し、キーヤー側 (engine) が先頭から 1 単位ずつ取り出す。
// 排他は呼び出し側で行う
class TypeBuffer {
 public:
  static constexpr int CAP = 128;
  static constexpr int MAX_UNIT = 12;  // 1 単位の最大長 ("[SK]" などの略符号を含む)

  bool push(char c) {
    if (len_ >= CAP) return false;
    buf_[(head_ + len_) % CAP] = c;
    len_++;
    return true;
  }
  // まだ送り始めていない最後の文字を取り消す
  bool popBack() {
    if (len_ == 0) return false;
    len_--;
    return true;
  }
  void clear() { len_ = 0; }
  int size() const { return len_; }

  // 先頭の 1 単位を取り出す: 1 文字、空白 (語間)、または "[...]" の略符号。
  // "[" に対応する "]" がまだ打たれていなければ待つ (false)。out は MAX_UNIT + 1 バイト以上
  bool takeUnit(char* out) {
    while (len_ > 0) {
      char c = at(0);
      if (c == ']') {  // 対応の無い "]" は捨てる
        drop(1);
        continue;
      }
      if (c != '[') {
        out[0] = c;
        out[1] = 0;
        drop(1);
        return true;
      }
      int close = -1;
      for (int i = 1; i < len_ && i <= MAX_UNIT; i++)
        if (at(i) == ']') {
          close = i;
          break;
        }
      if (close < 0) {
        if (len_ <= MAX_UNIT) return false;  // "]" を待つ
        drop(1);                             // 長すぎる: "[" を捨てて普通の文字として送る
        continue;
      }
      for (int i = 0; i <= close; i++) out[i] = at(i);
      out[close + 1] = 0;
      drop(close + 1);
      return true;
    }
    return false;
  }

 private:
  char at(int i) const { return buf_[(head_ + i) % CAP]; }
  void drop(int n) {
    head_ = (head_ + n) % CAP;
    len_ -= n;
  }
  char buf_[CAP];
  int head_ = 0, len_ = 0;
};

// 1 単位ずつ送るキーヤー (端末モード用)。単位は TypeBuffer::takeUnit の出力
// (1 文字 / "[SK]" のような略符号 = 文字間を空けずに続ける / " " = 語間)。
// 前の単位の最後の要素から文字間 3 dit (語間なら 7 dit) 空けて次を始める。それ以上空いていればすぐ始める
class StreamKeyer {
 public:
  void setWpm(int wpm) { ditMs_ = ditMsForWpm(wpm); }
  void reset() {
    running_ = down_ = hasPrev_ = wordGap_ = false;
    events_.clear();
  }
  // 次の単位を受け付けられる (送信中の単位が終わった)
  bool ready() const { return !running_; }
  bool down() const { return down_; }

  // 単位を送り始める。モールス符号の無い文字だけなら false (何もしない)
  bool send(const char* unit, uint32_t nowMs) {
    if (unit[0] == ' ' && unit[1] == 0) {
      wordGap_ = hasPrev_;
      return true;
    }
    len_ = 0;
    for (const char* p = unit; *p; p++) {
      const char* code = (*p == '[' || *p == ']') ? nullptr : morseEncode(*p);
      if (!code) continue;
      for (; *code && len_ < MAX_SEQ; code++) seq_[len_++] = *code;
    }
    if (len_ == 0) return false;
    uint32_t start = nowMs;
    if (hasPrev_) {
      uint32_t earliest = lastEnd_ + (wordGap_ ? 7 : 3) * ditMs_;
      if ((int32_t)(earliest - nowMs) > 0) start = earliest;
    }
    wordGap_ = false;
    pos_ = 0;
    tNext_ = start;
    running_ = true;
    return true;
  }

  void update(uint32_t nowMs) {
    for (int guard = 0; guard < 16 && running_ && (int32_t)(nowMs - tNext_) >= 0; guard++) step();
  }

  bool pop(KeyEvent& ev) { return events_.pop(ev); }

 private:
  static constexpr int MAX_SEQ = TypeBuffer::MAX_UNIT * 8;

  void step() {
    if (!down_) {
      down_ = true;
      events_.push(tNext_, 1);
      tNext_ += (seq_[pos_] == '-' ? 3 : 1) * ditMs_;
      return;
    }
    down_ = false;
    events_.push(tNext_, 0);
    if (++pos_ >= len_) {
      running_ = false;
      hasPrev_ = true;
      lastEnd_ = tNext_;
      return;
    }
    tNext_ += ditMs_;  // 要素間
  }

  char seq_[MAX_SEQ];
  int len_ = 0, pos_ = 0;
  uint32_t ditMs_ = 60;
  uint32_t tNext_ = 0, lastEnd_ = 0;
  bool running_ = false, down_ = false, hasPrev_ = false, wordGap_ = false;
  KeyEventQueue events_;
};

}  // namespace cw
