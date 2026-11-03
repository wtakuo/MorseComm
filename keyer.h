// keyer.h - key input processing: debounce, iambic keyer, automatic text keying
// Arduino-independent. Time is passed as the argument nowMs.
// In every mode the output is a "sequence of key state (DOWN/UP) transitions"
// (KeyEvent) (sec. 5).
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
        case KeyMode::Straight:
            return "STRAIGHT";
        case KeyMode::IambicA:
            return "IAMBIC_A";
        case KeyMode::IambicB:
            return "IAMBIC_B";
    }
    return "?";
}

inline uint32_t ditMsForWpm(int wpm) { return (1200 + wpm / 2) / wpm; }

struct KeyEvent {
    uint32_t t;     // transition time (exact scheduled time; corrected even if
                    // update is called late)
    uint8_t state;  // 1=DOWN, 0=UP
};

// Small event queue
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

// Lockout-style debounce: the first edge is accepted immediately, then changes
// are ignored for DEBOUNCE_MS (zero latency; removes only contact bounce)
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
    // Whether it changed since the last call
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

// Iambic keyer
//  Mode A: the next element is decided by the paddle state at the end of the
//  preceding inter-element space Mode B: latches presses of the opposite paddle
//  during an element (mark + space).
//          Even if a squeeze is released mid-element, one more element of the
//          opposite side is sent
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
            // Opposite side: latch if pressed. Same side: latch if newly
            // pressed
            if (curDit ? dah : dit) (curDit ? memDah_ : memDit_) = true;
            if (curDit ? (dit && !prevDit_) : (dah && !prevDah_))
                (curDit ? memDit_ : memDah_) = true;
        }
        prevDit_ = dit;
        prevDah_ = dah;

        for (int guard = 0; guard < 8; guard++) {
            if (phase_ == Phase::Idle) {
                if (dit)
                    start(Elem::Dit, nowMs);
                else if (dah)
                    start(Elem::Dah, nowMs);
                break;
            }
            if (timeBefore(nowMs, tEnd_)) break;
            if (phase_ == Phase::Mark) {
                events_.push(tEnd_, 0);
                phase_ = Phase::Space;
                tEnd_ += ditMs_;
                continue;
            }
            // End of space: decide the next element
            Elem opp = elem_ == Elem::Dit ? Elem::Dah : Elem::Dit;
            bool oppPressed = opp == Elem::Dit ? dit : dah;
            bool samePressed = elem_ == Elem::Dit ? dit : dah;
            bool oppMem = opp == Elem::Dit ? memDit_ : memDah_;
            bool sameMem = elem_ == Elem::Dit ? memDit_ : memDah_;
            if (oppPressed || (modeB_ && oppMem))
                start(opp, tEnd_);
            else if (samePressed || (modeB_ && sameMem))
                start(elem_, tEnd_);
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

    static bool timeBefore(uint32_t a, uint32_t b) {
        return (int32_t)(a - b) < 0;
    }

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

// Automatically keys text at a given WPM (for test mode). Repeats with a word
// gap at the end Characters enclosed in square brackets like "[SK]" are sent
// continuously without a character gap (prosign)
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
        for (int guard = 0; guard < 16 && (int32_t)(nowMs - tNext_) >= 0;
             guard++)
            step();
    }

    bool pop(KeyEvent& ev) { return events_.pop(ev); }

  private:
    // Sequence symbols: '.' '-' = elements (1 dit between elements); gap
    // symbols GAP_* are placed before the following element
    static constexpr char GAP_JOIN =
        'j';  // character gap inside a prosign (1 dit)
    static constexpr char GAP_CHAR = 'c';  // character gap (3 dit)
    static constexpr char GAP_WORD = 'w';  // word gap (7 dit)
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
            if (!code) {  // spaces and unsupported characters become a word gap
                word = true;
                continue;
            }
            if (len_ > 0 && len_ < MAX_SEQ) {
                seq_[len_++] = word                           ? GAP_WORD
                               : (inProsign && prevInProsign) ? GAP_JOIN
                                                              : GAP_CHAR;
            }
            for (; *code && len_ < MAX_SEQ; code++) seq_[len_++] = *code;
            word = false;
            prevInProsign = inProsign;
        }
    }

    static uint32_t gapDits(char g) {
        return g == GAP_WORD ? 7 : g == GAP_CHAR ? 3 : 1;
    }

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
        if (pos_ < len_ && (seq_[pos_] == GAP_JOIN || seq_[pos_] == GAP_CHAR ||
                            seq_[pos_] == GAP_WORD)) {
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

// Buffer of characters waiting to be sent in terminal mode (characters typed on
// the serial port). The typing side (loop) appends to / removes from the tail;
// the keyer side (engine) takes one unit at a time from the head. Mutual
// exclusion is the caller's responsibility
class TypeBuffer {
  public:
    static constexpr int CAP = 128;
    static constexpr int MAX_UNIT =
        12;  // maximum length of one unit (including prosigns such as "[SK]")

    bool push(char c) {
        if (len_ >= CAP) return false;
        buf_[(head_ + len_) % CAP] = c;
        len_++;
        return true;
    }
    // Remove the last character that has not started being sent yet
    bool popBack() {
        if (len_ == 0) return false;
        len_--;
        return true;
    }
    void clear() { len_ = 0; }
    int size() const { return len_; }
    int space() const { return CAP - len_; }
    // Last character waiting to be sent (0 if empty)
    char back() const { return len_ ? at(len_ - 1) : 0; }
    // Copy the characters waiting to be sent into out (for display.
    // NUL-terminated; if it does not fit, the tail end is copied)
    void copyTo(char* out, int cap) const {
        int n = len_ < cap - 1 ? len_ : cap - 1;
        for (int i = 0; i < n; i++) out[i] = at(len_ - n + i);
        out[n] = 0;
    }

    // Take one unit from the head: a single character, a space (word gap), or a
    // "[...]" prosign. If the "]" matching "[" has not been typed yet, wait
    // (false). out must be at least MAX_UNIT + 1 bytes
    bool takeUnit(char* out) {
        while (len_ > 0) {
            char c = at(0);
            if (c == ']') {  // discard an unmatched "]"
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
                if (len_ <= MAX_UNIT) return false;  // wait for "]"
                drop(1);  // too long: discard "[" and send the rest as ordinary
                          // characters
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

// Keyer that sends one unit at a time (for terminal mode). A unit is the output
// of TypeBuffer::takeUnit (one character / a prosign like "[SK]" = continued
// without a character gap / " " = word gap). The next unit starts a character
// gap of 3 dit (7 dit for a word gap) after the last element of the previous
// unit. If more time has passed, it starts immediately
class StreamKeyer {
  public:
    void setWpm(int wpm) { ditMs_ = ditMsForWpm(wpm); }
    void reset() {
        running_ = down_ = hasPrev_ = wordGap_ = false;
        events_.clear();
    }
    // Ready to accept the next unit (the unit being sent has finished)
    bool ready() const { return !running_; }
    bool down() const { return down_; }

    // Start sending a unit. Returns false (does nothing) if it contains only
    // characters without a Morse code
    bool send(const char* unit, uint32_t nowMs) {
        if (unit[0] == ' ' && unit[1] == 0) {
            wordGap_ = hasPrev_;
            return true;
        }
        len_ = 0;
        for (const char* p = unit; *p; p++) {
            const char* code =
                (*p == '[' || *p == ']') ? nullptr : morseEncode(*p);
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
        for (int guard = 0;
             guard < 16 && running_ && (int32_t)(nowMs - tNext_) >= 0; guard++)
            step();
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
        tNext_ += ditMs_;  // inter-element gap
    }

    char seq_[MAX_SEQ];
    int len_ = 0, pos_ = 0;
    uint32_t ditMs_ = 60;
    uint32_t tNext_ = 0, lastEnd_ = 0;
    bool running_ = false, down_ = false, hasPrev_ = false, wordGap_ = false;
    KeyEventQueue events_;
};

}  // namespace cw
