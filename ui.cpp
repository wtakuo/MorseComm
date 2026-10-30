#include "ui.h"
#include <Arduino_Nesso_N1.h>
#include <string.h>

namespace ui {
namespace {

constexpr int W = 240, H = 135;
constexpr int COLS = 20;           // テキストサイズ2 (12x16) で 1 行 20 文字
constexpr int RX_ROWS = 3, TX_ROWS = 2;
constexpr uint32_t LONG_PRESS_MS = 600;

constexpr uint16_t COL_BG = TFT_BLACK;
constexpr uint16_t COL_RX = 0x07E8;     // 緑
constexpr uint16_t COL_TX = 0xFEA0;     // 黄
constexpr uint16_t COL_DIM = 0x8410;    // 灰
constexpr uint16_t COL_SEL = 0x04DF;    // 選択中の項目の背景
constexpr uint16_t COL_WARN = 0xF9E7;   // 赤

NessoDisplay display;
LGFX_Sprite canvas(&display);

// 末尾 cap 文字だけを保持するテキストバッファ
struct TextBuf {
  static constexpr int CAP = COLS * RX_ROWS;
  char buf[CAP + 1] = {};
  int len = 0;
  void add(char c) {
    if (c == ' ' && (len == 0 || buf[len - 1] == ' ')) return;
    if (len == CAP) {  // 1 行ぶんまとめて捨てる (折り返し位置を保つ)
      memmove(buf, buf + COLS, CAP - COLS);
      len -= COLS;
    }
    buf[len++] = c;
    buf[len] = 0;
  }
  void clear() {
    len = 0;
    buf[0] = 0;
  }
};

TextBuf rxText, txText;

struct BtnState {
  bool down = false;
  uint32_t since = 0;
  bool longFired = false;
};
BtnState btn[2];

// 末尾 rows 行を y から描く (文字単位で折り返し)
void drawText(const TextBuf& t, int rows, int y, uint16_t color) {
  int lines = (t.len + COLS - 1) / COLS;
  int start = (lines > rows ? lines - rows : 0) * COLS;
  canvas.setTextSize(2);
  canvas.setTextColor(color, COL_BG);
  for (int r = 0; r < rows && start + r * COLS < t.len; r++) {
    char line[COLS + 1];
    int n = t.len - (start + r * COLS);
    if (n > COLS) n = COLS;
    memcpy(line, t.buf + start + r * COLS, n);
    line[n] = 0;
    canvas.drawString(line, 0, y + r * 16);
  }
}

void field(const char* text, int x, int y, bool selected) {
  canvas.setTextColor(selected ? TFT_WHITE : TFT_LIGHTGREY, selected ? COL_SEL : COL_BG);
  canvas.drawString(text, x, y);
}

}  // namespace

void begin() {
  display.begin();
  display.setRotation(1);
  display.fillScreen(COL_BG);
  canvas.setColorDepth(16);
  canvas.createSprite(W, H);
  canvas.setFont(&fonts::Font0);
}

void addChar(bool rx, char c) { (rx ? rxText : txText).add(c); }

void clearText() {
  rxText.clear();
  txText.clear();
}

Button pollButtons(uint32_t nowMs) {
  bool raw[2] = {digitalRead(KEY1) == LOW, digitalRead(KEY2) == LOW};
  Button result = Button::None;
  for (int i = 0; i < 2; i++) {
    BtnState& b = btn[i];
    if (raw[i] && !b.down) {
      b.down = true;
      b.since = nowMs;
      b.longFired = false;
    } else if (raw[i] && b.down && !b.longFired && nowMs - b.since >= LONG_PRESS_MS) {
      b.longFired = true;
      result = i == 0 ? Button::Key1Long : Button::Key2Long;
    } else if (!raw[i] && b.down) {
      b.down = false;
      if (!b.longFired) result = i == 0 ? Button::Key1Short : Button::Key2Short;
    }
  }
  return result;
}

void draw(const Settings& s, const Control& c, const Snapshot& snap, Item selected) {
  char tmp[48];
  canvas.fillScreen(COL_BG);
  canvas.setTextSize(1);

  // 1 行目: 設定
  int x = 0;
  const char* modeName = cw::keyModeName((cw::KeyMode)s.mode);
  field(modeName, x, 0, selected == Item::Mode);
  x += 6 * strlen(modeName) + 6;
  snprintf(tmp, sizeof(tmp), "%dwpm", s.wpm);
  field(tmp, x, 0, selected == Item::Wpm);
  x += 6 * strlen(tmp) + 6;
  snprintf(tmp, sizeof(tmp), "%dHz", s.toneHz);
  field(tmp, x, 0, selected == Item::Tone);
  x += 6 * strlen(tmp) + 6;
  snprintf(tmp, sizeof(tmp), "vol%d", s.volume);
  field(tmp, x, 0, selected == Item::Volume);
  if (c.testOn || c.lossPct || c.dropUp) {
    canvas.setTextColor(COL_WARN, COL_BG);
    snprintf(tmp, sizeof(tmp), "%s%s", c.testOn ? "T" : "", (c.lossPct || c.dropUp) ? "L" : "");
    canvas.drawString(tmp, W - 6 * strlen(tmp), 0);
  }

  // 2 行目: リンク状態
  uint32_t total = snap.rx.packets + snap.rx.lost;
  int lossPct = total ? (int)(100.0f * snap.rx.lost / total + 0.5f) : 0;
  if (snap.rx.packets == 0) {
    snprintf(tmp, sizeof(tmp), "no link  J%u", s.jitterMs);
  } else {
    float ago = (snap.nowMs - snap.rx.lastRxMs) / 1000.0f;
    snprintf(tmp, sizeof(tmp), "%s%.1fs %ddBm L%d%% J%u R%u W%u", snap.linkUp ? "" : "LOST ", ago, snap.rssi, lossPct,
             s.jitterMs, snap.rx.resyncs, snap.rx.watchdogs);
  }
  canvas.setTextColor(snap.linkUp ? COL_DIM : COL_WARN, COL_BG);
  canvas.drawString(tmp, 0, 10);

  // 受信テキスト
  canvas.setTextColor(COL_DIM, COL_BG);
  snprintf(tmp, sizeof(tmp), "RX ~%dwpm", snap.remoteWpm);
  canvas.drawString(tmp, 0, 22);
  canvas.fillRect(W - 14, 21, 12, 9, snap.remoteDown ? COL_RX : 0x2104);
  drawText(rxText, RX_ROWS, 31, COL_RX);

  // 送信テキスト
  canvas.setTextSize(1);
  canvas.setTextColor(COL_DIM, COL_BG);
  snprintf(tmp, sizeof(tmp), "TX ~%dwpm", snap.localWpm);
  canvas.drawString(tmp, 0, 82);
  canvas.fillRect(W - 14, 81, 12, 9, snap.localDown ? COL_TX : 0x2104);
  drawText(txText, TX_ROWS, 91, COL_TX);

  canvas.pushSprite(0, 0);
}

}  // namespace ui
