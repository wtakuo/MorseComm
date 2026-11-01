// keyboard_cardputer.cpp — Cardputer (初代) のキーボード: 4 x 14 = 56 キーのマトリクス
//
// 74HC138 の A0/A1/A2 = G8/G9/G11 で 8 本の行のどれかを LOW にし、7 本の列 G13/G15/G3/G4/G5/G6/G7
// (プルアップ、押下で LOW) を読む。座標の対応とキー配列は M5Cardputer ライブラリ (IOMatrix.cpp, Keyboard.h) に従う。
// loop() から 20ms ごとに readKey() が呼ばれる前提で、そのたびに全体を走査し、押された瞬間のキーを返す。
// v1.1 / ADV は I2C のキーボードコントローラ (TCA8418) なので、このコードでは動かない。
#include "board.h"
#include "hw.h"

#if defined(BOARD_CARDPUTER)
#include <Arduino.h>
#include <driver/gpio.h>

namespace hw {
namespace {

constexpr int PIN_SELECT[3] = {8, 9, 11};                // 74HC138 A0, A1, A2
constexpr int PIN_INPUT[7] = {13, 15, 3, 4, 5, 6, 7};    // 列 (押下 = LOW)
constexpr int ROWS = 4, COLS = 14;

// 特殊キー (表の中で使う)
constexpr int K_FN = 0x200, K_SHIFT = 0x201, K_CTRL = 0x202, K_OPT = 0x203, K_ALT = 0x204;

struct KeyDef {
  int normal, shifted, fn;
};

// [y][x]。M5Cardputer の _key_value_map と同じ並び
const KeyDef KEYMAP[ROWS][COLS] = {
    {{'`', '~', KEY_ESC}, {'1', '!', KEY_NONE}, {'2', '@', KEY_NONE}, {'3', '#', KEY_NONE}, {'4', '$', KEY_NONE},
     {'5', '%', KEY_NONE}, {'6', '^', KEY_NONE}, {'7', '&', KEY_NONE}, {'8', '*', KEY_NONE}, {'9', '(', KEY_NONE},
     {'0', ')', KEY_NONE}, {'-', '_', KEY_NONE}, {'=', '+', KEY_NONE}, {KEY_BACKSPACE, KEY_BACKSPACE, KEY_BACKSPACE}},
    {{KEY_TAB, KEY_TAB, KEY_TAB}, {'q', 'Q', KEY_NONE}, {'w', 'W', KEY_NONE}, {'e', 'E', KEY_NONE}, {'r', 'R', KEY_NONE},
     {'t', 'T', KEY_NONE}, {'y', 'Y', KEY_NONE}, {'u', 'U', KEY_NONE}, {'i', 'I', KEY_NONE}, {'o', 'O', KEY_NONE},
     {'p', 'P', KEY_NONE}, {'[', '{', KEY_NONE}, {']', '}', KEY_NONE}, {'\\', '|', KEY_NONE}},
    {{K_FN, K_FN, K_FN}, {K_SHIFT, K_SHIFT, K_SHIFT}, {'a', 'A', KEY_NONE}, {'s', 'S', KEY_NONE}, {'d', 'D', KEY_NONE},
     {'f', 'F', KEY_NONE}, {'g', 'G', KEY_NONE}, {'h', 'H', KEY_NONE}, {'j', 'J', KEY_NONE}, {'k', 'K', KEY_NONE},
     {'l', 'L', KEY_NONE}, {';', ':', KEY_UP}, {'\'', '"', KEY_NONE}, {KEY_ENTER, KEY_ENTER, KEY_ENTER}},
    {{K_CTRL, K_CTRL, K_CTRL}, {K_OPT, K_OPT, K_OPT}, {K_ALT, K_ALT, K_ALT}, {'z', 'Z', KEY_NONE}, {'x', 'X', KEY_NONE},
     {'c', 'C', KEY_NONE}, {'v', 'V', KEY_NONE}, {'b', 'B', KEY_NONE}, {'n', 'N', KEY_NONE}, {'m', 'M', KEY_NONE},
     {',', '<', KEY_LEFT}, {'.', '>', KEY_DOWN}, {'/', '?', KEY_RIGHT}, {' ', ' ', ' '}},
};

bool ready = false;
bool prev[ROWS][COLS] = {};
constexpr int QUEUE = 16;
int queue[QUEUE];
int qHead = 0, qLen = 0;

void beginMatrix() {
  for (int p : PIN_SELECT) {
    gpio_reset_pin((gpio_num_t)p);
    pinMode(p, OUTPUT);
    digitalWrite(p, LOW);
  }
  for (int p : PIN_INPUT) {
    gpio_reset_pin((gpio_num_t)p);
    pinMode(p, INPUT_PULLUP);
  }
  ready = true;
}

// 全キーの押下状態を読む
void scan(bool now[ROWS][COLS]) {
  for (int y = 0; y < ROWS; y++)
    for (int x = 0; x < COLS; x++) now[y][x] = false;
  for (int i = 0; i < 8; i++) {
    for (int b = 0; b < 3; b++) digitalWrite(PIN_SELECT[b], (i >> b) & 1);
    delayMicroseconds(2);
    for (int j = 0; j < 7; j++) {
      if (digitalRead(PIN_INPUT[j]) != LOW) continue;
      int x = i > 3 ? 2 * j : 2 * j + 1;
      int y = 3 - (i & 3);
      now[y][x] = true;
    }
  }
}

void push(int k) {
  if (qLen < QUEUE) queue[(qHead + qLen++) % QUEUE] = k;
}

}  // namespace

bool hasKeyboard() { return true; }

int readKey() {
  if (!ready) beginMatrix();
  if (qLen == 0) {
    bool now[ROWS][COLS];
    scan(now);
    bool fn = false, shift = false;
    for (int y = 0; y < ROWS; y++)
      for (int x = 0; x < COLS; x++) {
        if (!now[y][x]) continue;
        if (KEYMAP[y][x].normal == K_FN) fn = true;
        if (KEYMAP[y][x].normal == K_SHIFT) shift = true;
      }
    for (int y = 0; y < ROWS; y++)
      for (int x = 0; x < COLS; x++) {
        bool pressed = now[y][x] && !prev[y][x];
        prev[y][x] = now[y][x];
        if (!pressed) continue;
        const KeyDef& d = KEYMAP[y][x];
        if (d.normal >= K_FN) continue;  // 修飾キーだけでは何も出さない
        int k = fn ? d.fn : shift ? d.shifted : d.normal;
        if (k != KEY_NONE) push(k);
      }
  }
  if (qLen == 0) return KEY_NONE;
  int k = queue[qHead];
  qHead = (qHead + 1) % QUEUE;
  qLen--;
  return k;
}

}  // namespace hw

#else  // キーボードの無いボード

namespace hw {
bool hasKeyboard() { return false; }
int readKey() { return KEY_NONE; }
}  // namespace hw

#endif
