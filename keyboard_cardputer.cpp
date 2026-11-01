// keyboard_cardputer.cpp — Cardputer (初代) のキーボード: 4 x 14 = 56 キーのマトリクス
//
// 74HC138 の A0/A1/A2 = G8/G9/G11 で 8 本の行のどれかを LOW にし、7 本の列 G13/G15/G3/G4/G5/G6/G7
// (プルアップ、押下で LOW) を読む。座標の対応とキー配列は M5Cardputer ライブラリ (IOMatrix.cpp, Keyboard.h) に従う。
// loop() から 20ms ごとに readKey() が呼ばれる前提で、そのたびに全体を走査し、押された瞬間のキーを返す。
//
// Cardputer ADV (同じビルドで M5Unified が起動時に判別する) はキーボードコントローラ TCA8418 (I2C 0x34, 内部 I2C)。
// キーイベントの FIFO を読み、行 r / 列 c (イベント番号 = r*10 + c + 1, bit7 = 押下) を初代と同じ座標に直す
// (x = 2r + (c > 3), y = c % 4。M5Cardputer の TCA8418.cpp と同じ)。割り込みは使わずに 20ms ごとに読む。
#include "board.h"
#include "hw.h"

#if defined(BOARD_CARDPUTER)
#include <Arduino.h>
#include <M5Unified.h>
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
bool adv = false;  // Cardputer ADV (TCA8418)
bool prev[ROWS][COLS] = {};

// ---- ADV: TCA8418 ----
constexpr uint8_t TCA_ADDR = 0x34;
constexpr uint32_t TCA_FREQ = 400000;
constexpr uint8_t REG_CFG = 0x01, REG_INT_STAT = 0x02, REG_KEY_EVENT_A = 0x04, REG_KP_GPIO_1 = 0x1D,
                  REG_KP_GPIO_2 = 0x1E, REG_KP_GPIO_3 = 0x1F;

void tcaWrite(uint8_t reg, uint8_t v) { M5.In_I2C.writeRegister8(TCA_ADDR, reg, v, TCA_FREQ); }
uint8_t tcaRead(uint8_t reg) { return M5.In_I2C.readRegister8(TCA_ADDR, reg, TCA_FREQ); }

void beginTca() {
  tcaWrite(REG_KP_GPIO_1, 0x7F);  // ROW0..6 をキーマトリクスに
  tcaWrite(REG_KP_GPIO_2, 0xFF);  // COL0..7
  tcaWrite(REG_KP_GPIO_3, 0x00);
  for (int i = 0; i < 16 && tcaRead(REG_KEY_EVENT_A) != 0; i++) {}  // 溜まっているイベントを捨てる
  tcaWrite(REG_INT_STAT, 0x03);
}

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

// 修飾キー (fn / shift) が今押されているか
bool modifierDown(int which) {
  for (int y = 0; y < ROWS; y++)
    for (int x = 0; x < COLS; x++)
      if (prev[y][x] && KEYMAP[y][x].normal == which) return true;
  return false;
}

// キーが押された: 修飾キーの状態に応じた値をキューに積む (修飾キーだけでは何も出さない)
void pressed(int y, int x) {
  const KeyDef& d = KEYMAP[y][x];
  if (d.normal >= K_FN) return;
  int k = modifierDown(K_FN) ? d.fn : modifierDown(K_SHIFT) ? d.shifted : d.normal;
  if (k != KEY_NONE) push(k);
}

// ADV: FIFO のイベントを順に処理する (短い押下も取りこぼさないよう、押下イベントごとにキーを出す)
void readTca() {
  for (int i = 0; i < 16; i++) {
    uint8_t ev = tcaRead(REG_KEY_EVENT_A);
    if (ev == 0) break;
    int code = (ev & 0x7F) - 1;
    int r = code / 10, c = code % 10;
    if (code < 0 || r > 6 || c > 7) continue;  // キーマトリクス以外 (GPI) のイベント
    int x = 2 * r + (c > 3 ? 1 : 0), y = c % 4;
    bool down = (ev & 0x80) != 0;
    if (down && !prev[y][x]) pressed(y, x);
    prev[y][x] = down;
  }
  tcaWrite(REG_INT_STAT, 0x03);
}

}  // namespace

bool hasKeyboard() { return true; }

int readKey() {
  if (!ready) {
    adv = M5.getBoard() == m5::board_t::board_M5CardputerADV;
    if (adv) beginTca();
    else beginMatrix();  // ADV では G8/G9 が I2C なので、マトリクスの走査をしてはいけない
    ready = true;
  }
  if (qLen == 0 && adv) {
    readTca();
  } else if (qLen == 0) {
    // 初代: 全キーを走査し、押された瞬間のキーを出す (修飾キーは走査前の状態ではなく今回の状態で見る)
    bool now[ROWS][COLS];
    scan(now);
    bool was[ROWS][COLS];
    for (int y = 0; y < ROWS; y++)
      for (int x = 0; x < COLS; x++) {
        was[y][x] = prev[y][x];
        prev[y][x] = now[y][x];
      }
    for (int y = 0; y < ROWS; y++)
      for (int x = 0; x < COLS; x++)
        if (now[y][x] && !was[y][x]) pressed(y, x);
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
