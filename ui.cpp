#include "ui.h"
#include <Arduino_Nesso_N1.h>
#include <string.h>

namespace ui {
namespace {

constexpr int W = 240, H = 135;
constexpr int COLS = 20;  // 送受信テキスト: Font0 x2 (12x16) で 1 行 20 文字
constexpr int RX_ROWS = 3, TX_ROWS = 2;
constexpr uint32_t LONG_PRESS_MS = 600;
constexpr uint32_t TOUCH_REPEAT_DELAY_MS = 500;  // +/- 押し続けで連続変更
constexpr uint32_t TOUCH_REPEAT_MS = 120;

// メイン画面のレイアウト
constexpr int Y_STATUS = 0;
constexpr int Y_RX_LABEL = 18, Y_RX_TEXT = 35;
constexpr int Y_TX_LABEL = 85, Y_TX_TEXT = 102;
// 設定画面のレイアウト
constexpr int ROW_Y0 = 17, ROW_H = 17;
constexpr int LIST_W = 164;               // 項目リストの幅 (右側は +/- ボタン)
constexpr int BTN_X = 170, BTN_W = W - BTN_X;
constexpr int BTN_PLUS_Y = 17, BTN_MINUS_Y = 77, BTN_H = 57;

constexpr uint16_t COL_BG = TFT_BLACK;
constexpr uint16_t COL_RX = 0x07E8;      // 緑
constexpr uint16_t COL_TX = 0xFEA0;      // 黄
constexpr uint16_t COL_LABEL = 0xBDF7;   // 明るい灰
constexpr uint16_t COL_DIM = 0x8410;     // 灰
constexpr uint16_t COL_OFF = 0x2104;     // キー表示の消灯色
constexpr uint16_t COL_SEL = 0x04DF;     // 選択中の項目の背景
constexpr uint16_t COL_BTN = 0x31A6;     // タッチボタン
constexpr uint16_t COL_WARN = 0xF9E7;    // 赤

NessoDisplay display;
NessoTouch touch;
NessoBattery battery;
LGFX_Sprite canvas(&display);
bool touchOk = false;

// 末尾を保持するテキストバッファ (あふれたら 1 行ぶんまとめて捨てて折り返し位置を保つ)
struct TextBuf {
  static constexpr int CAP = COLS * RX_ROWS;
  char buf[CAP + 1] = {};
  int len = 0;
  void add(char c) {
    if (c == ' ' && (len == 0 || buf[len - 1] == ' ')) return;
    if (len == CAP) {
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

struct TouchState {
  bool down = false;
  TouchEvent::Kind kind = TouchEvent::None;
  uint32_t nextRepeat = 0;
} touchState;

// 末尾 rows 行を y から描く
void drawText(const TextBuf& t, int rows, int y, uint16_t color) {
  int lines = (t.len + COLS - 1) / COLS;
  int start = (lines > rows ? lines - rows : 0) * COLS;
  canvas.setFont(&fonts::Font0);
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

void useLabelFont() {
  canvas.setFont(&fonts::Font2);
  canvas.setTextSize(1);
}

// 右上の電池表示
void drawBattery(const Battery& bat) {
  useLabelFont();
  char tmp[16];
  if (!bat.valid) snprintf(tmp, sizeof(tmp), "--%%");
  else snprintf(tmp, sizeof(tmp), "%s%d%%", bat.charging ? "+" : "", bat.percent);
  canvas.setTextColor(bat.valid && bat.percent < 20 && !bat.charging ? COL_WARN : COL_LABEL, COL_BG);
  canvas.setTextDatum(textdatum_t::top_right);
  canvas.drawString(tmp, W - 1, Y_STATUS);
  canvas.setTextDatum(textdatum_t::top_left);
}

void drawKeyLamp(int y, bool on, uint16_t color) { canvas.fillRoundRect(W - 18, y + 2, 16, 12, 3, on ? color : COL_OFF); }

const char* itemLabel(Item it) {
  switch (it) {
    case Item::Mode: return "MODE";
    case Item::Wpm: return "SPEED";
    case Item::SideOn: return "SIDETONE";
    case Item::SideHz: return "SIDE FREQ";
    case Item::RxHz: return "RX FREQ";
    case Item::Volume: return "VOLUME";
    case Item::Swap: return "PADDLE";
    default: return "";
  }
}

void itemValue(const Settings& s, Item it, char* buf, size_t n) {
  switch (it) {
    case Item::Mode: snprintf(buf, n, "%s", cw::keyModeName((cw::KeyMode)s.mode)); break;
    case Item::Wpm: snprintf(buf, n, "%u WPM", s.wpm); break;
    case Item::SideOn: snprintf(buf, n, "%s", s.sideOn ? "ON" : "OFF"); break;
    case Item::SideHz: snprintf(buf, n, "%u Hz", s.sideHz); break;
    case Item::RxHz: snprintf(buf, n, "%u Hz", s.rxHz); break;
    case Item::Volume: snprintf(buf, n, "%u / %d", s.volume, 5); break;
    case Item::Swap: snprintf(buf, n, "%s", s.swap ? "SWAPPED" : "NORMAL"); break;
    default: buf[0] = 0; break;
  }
}

void drawTouchButton(int y, const char* sym) {
  canvas.fillRoundRect(BTN_X, y, BTN_W, BTN_H, 8, COL_BTN);
  canvas.setFont(&fonts::Font4);
  canvas.setTextSize(2);
  canvas.setTextColor(TFT_WHITE, COL_BTN);
  canvas.setTextDatum(textdatum_t::middle_center);
  canvas.drawString(sym, BTN_X + BTN_W / 2, y + BTN_H / 2);
  canvas.setTextDatum(textdatum_t::top_left);
}

// タッチの生座標 → 画面座標
void mapTouch(int16_t rx, int16_t ry, int16_t& x, int16_t& y) {
  int16_t a = rx, b = ry;
  int16_t aMax = 134, bMax = 239;  // パネル縦置きの範囲
  if (cfg::TOUCH_SWAP_XY) {
    int16_t t = a;
    a = b;
    b = t;
    t = aMax;
    aMax = bMax;
    bMax = t;
  }
  if (cfg::TOUCH_INVERT_X) a = aMax - a;
  if (cfg::TOUCH_INVERT_Y) b = bMax - b;
  x = a;
  y = b;
}

}  // namespace

void begin() {
  display.begin();
  display.setRotation(1);
  display.fillScreen(COL_BG);
  canvas.setColorDepth(16);
  canvas.createSprite(W, H);
  touchOk = touch.begin();
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

TouchEvent pollTouch(bool settingsScreen) {
  TouchEvent ev;
  if (!touchOk) return ev;
  int16_t rx, ry;
  if (!touch.read(rx, ry)) {
    touchState.down = false;
    return ev;
  }
  uint32_t now = millis();
  mapTouch(rx, ry, ev.x, ev.y);
  if (!settingsScreen) {
    touchState.down = true;
    return ev;
  }
  TouchEvent::Kind kind = TouchEvent::Row;
  if (ev.x >= BTN_X - 4) kind = ev.y < (BTN_PLUS_Y + BTN_H + BTN_MINUS_Y) / 2 ? TouchEvent::Plus : TouchEvent::Minus;

  if (!touchState.down) {
    touchState.down = true;
    touchState.kind = kind;
    touchState.nextRepeat = now + TOUCH_REPEAT_DELAY_MS;
  } else if ((kind == TouchEvent::Plus || kind == TouchEvent::Minus) && kind == touchState.kind &&
             (int32_t)(now - touchState.nextRepeat) >= 0) {
    touchState.nextRepeat = now + TOUCH_REPEAT_MS;  // 押し続けで連続変更
  } else {
    return ev;
  }
  ev.kind = kind;
  if (kind == TouchEvent::Row) {
    int row = (ev.y - ROW_Y0) / ROW_H;
    if (ev.y < ROW_Y0 || row >= (int)Item::Count) ev.kind = TouchEvent::None;
    ev.row = row;
  }
  return ev;
}

bool touchAvailable() { return touchOk; }

Battery readBattery() {
  Battery b;
  uint16_t pct = battery.getChargeLevel();
  if (pct <= 100) {
    b.valid = true;
    b.percent = pct;
    NessoBattery::ChargeStatus st = battery.getChargeStatus();
    b.charging = st == NessoBattery::PRE_CHARGE || st == NessoBattery::CHARGING;
  }
  return b;
}

void drawMain(const Control& c, const Snapshot& snap, const Battery& bat) {
  char tmp[48];
  canvas.fillScreen(COL_BG);
  useLabelFont();

  // 1 行目: リンク状態と電池
  uint32_t total = snap.rx.packets + snap.rx.lost;
  int lossPct = total ? (int)(100.0f * snap.rx.lost / total + 0.5f) : 0;
  if (snap.rx.packets == 0) {
    canvas.setTextColor(COL_WARN, COL_BG);
    canvas.drawString("NO LINK", 0, Y_STATUS);
  } else if (!snap.linkUp) {
    canvas.setTextColor(COL_WARN, COL_BG);
    snprintf(tmp, sizeof(tmp), "LOST %lus", (unsigned long)((snap.nowMs - snap.rx.lastRxMs) / 1000));
    canvas.drawString(tmp, 0, Y_STATUS);
  } else {
    canvas.setTextColor(COL_LABEL, COL_BG);
    snprintf(tmp, sizeof(tmp), "%ddBm  loss %d%%  R%lu W%lu", snap.rssi, lossPct, (unsigned long)snap.rx.resyncs,
             (unsigned long)snap.rx.watchdogs);
    canvas.drawString(tmp, 0, Y_STATUS);
  }
  drawBattery(bat);

  // 受信
  useLabelFont();
  canvas.setTextColor(COL_RX, COL_BG);
  canvas.drawString("RX", 0, Y_RX_LABEL);
  canvas.setTextColor(COL_LABEL, COL_BG);
  snprintf(tmp, sizeof(tmp), "%d wpm", snap.remoteWpm);
  canvas.drawString(tmp, 26, Y_RX_LABEL);
  if (c.testOn || c.lossPct || c.dropUp) {
    canvas.setTextColor(COL_WARN, COL_BG);
    snprintf(tmp, sizeof(tmp), "%s%s", c.testOn ? "TEST " : "", (c.lossPct || c.dropUp) ? "LOSS" : "");
    canvas.drawString(tmp, 110, Y_RX_LABEL);
  }
  drawKeyLamp(Y_RX_LABEL, snap.remoteDown, COL_RX);
  drawText(rxText, RX_ROWS, Y_RX_TEXT, COL_RX);

  // 送信
  useLabelFont();
  canvas.setTextColor(COL_TX, COL_BG);
  canvas.drawString("TX", 0, Y_TX_LABEL);
  canvas.setTextColor(COL_LABEL, COL_BG);
  snprintf(tmp, sizeof(tmp), "%d wpm", snap.localWpm);
  canvas.drawString(tmp, 26, Y_TX_LABEL);
  drawKeyLamp(Y_TX_LABEL, snap.localDown, COL_TX);
  drawText(txText, TX_ROWS, Y_TX_TEXT, COL_TX);

  canvas.pushSprite(0, 0);
}

void drawSettings(const Settings& s, Item selected, const Battery& bat) {
  char tmp[24];
  canvas.fillScreen(COL_BG);
  useLabelFont();
  canvas.setTextColor(TFT_WHITE, COL_BG);
  canvas.drawString("SETTINGS", 0, 0);
  canvas.setTextColor(COL_DIM, COL_BG);
  canvas.drawString("KEY1 hold: exit", 72, 0);
  drawBattery(bat);

  for (int i = 0; i < (int)Item::Count; i++) {
    Item it = (Item)i;
    int y = ROW_Y0 + i * ROW_H;
    bool sel = it == selected;
    uint16_t bg = sel ? COL_SEL : COL_BG;
    if (sel) canvas.fillRect(0, y, LIST_W, ROW_H, bg);
    canvas.setTextColor(sel ? TFT_WHITE : COL_LABEL, bg);
    canvas.drawString(itemLabel(it), 3, y + 1);
    itemValue(s, it, tmp, sizeof(tmp));
    canvas.setTextColor(TFT_WHITE, bg);
    canvas.setTextDatum(textdatum_t::top_right);
    canvas.drawString(tmp, LIST_W - 3, y + 1);
    canvas.setTextDatum(textdatum_t::top_left);
  }
  drawTouchButton(BTN_PLUS_Y, "+");
  drawTouchButton(BTN_MINUS_Y, "-");
  canvas.pushSprite(0, 0);
}

}  // namespace ui
