#include "ui.h"
#include "build_info.h"
BUILD_INFO_UNIT();
#include <Arduino.h>
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
constexpr int VISIBLE_ROWS = 7;           // 画面に収まる項目数 (それ以上はスクロール)
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
constexpr uint16_t COL_EDIT = 0xFB20;    // 変更中の項目の背景 (橙)
constexpr uint16_t COL_LORA = 0x8010;    // LoRa 使用中のチャンネル表示の背景 (紫)
constexpr uint16_t COL_OFF_BADGE = 0x52AA;  // 電波 OFF の表示の背景 (灰)

LGFX_Sprite canvas;
DefaultsState defaultsState = DefaultsState::Idle;  // drawSettings の引数 (itemValue で使う)
int listTop = 0;  // 設定画面: 先頭に表示している項目

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
    bool unknown = false;
    for (int i = 0; i < n; i++) {
      if (line[i] == cw::CHAR_UNKNOWN) {
        line[i] = ' ';
        unknown = true;
      }
    }
    canvas.setTextColor(color, COL_BG);
    canvas.drawString(line, 0, y + r * 16);
    // 解読できなかった符号は赤い '?'
    if (unknown) {
      canvas.setTextColor(COL_WARN, COL_BG);
      for (int i = 0; i < n; i++)
        if (t.buf[start + r * COLS + i] == cw::CHAR_UNKNOWN) canvas.drawString("?", i * 12, y + r * 16);
    }
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
    case Item::Radio: return "RADIO";
    case Item::Defaults: return "DEFAULTS";
    case Item::Output: return "OUTPUT";
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
    case Item::Defaults:
      snprintf(buf, n, "%s",
               defaultsState == DefaultsState::Armed  ? "PRESS AGAIN"
               : defaultsState == DefaultsState::Done ? "RESTORED"
                                                      : "RESTORE");
      break;
    case Item::Volume:  // 今の出口 (スピーカー / ヘッドフォン) の音量
      snprintf(buf, n, "%u / %d", s.output == cfg::OUTPUT_PHONES ? s.hpVolume : s.volume, 5);
      break;
    case Item::Output: snprintf(buf, n, "%s", s.output == cfg::OUTPUT_PHONES ? "PHONES" : "SPEAKER"); break;
    case Item::Swap: snprintf(buf, n, "%s", s.swap ? "SWAPPED" : "NORMAL"); break;
    case Item::Radio:
      // 動作中と違う場合は、設定画面を抜けると再起動して切り替わる
      snprintf(buf, n, "%s%s",
               s.radio == cfg::RADIO_LORA  ? "LoRa"
               : s.radio == cfg::RADIO_OFF ? "OFF"
                                           : "ESP-NOW",
               s.radio != net::radio() ? " *" : "");
      break;
    default: buf[0] = 0; break;
  }
}

// 値を増やす / 減らすタッチボタン。▲ / ▼ を図形で描く (フォントに記号が無いため)
void drawTouchButton(int y, bool up) {
  constexpr int TRI_W = 36, TRI_H = 26;
  canvas.fillRoundRect(BTN_X, y, BTN_W, BTN_H, 8, COL_BTN);
  int cx = BTN_X + BTN_W / 2, cy = y + BTN_H / 2;
  int top = cy - TRI_H / 2, bottom = cy + TRI_H / 2;
  if (up) canvas.fillTriangle(cx, top, cx - TRI_W / 2, bottom, cx + TRI_W / 2, bottom, TFT_WHITE);
  else canvas.fillTriangle(cx, bottom, cx - TRI_W / 2, top, cx + TRI_W / 2, top, TFT_WHITE);
}

void present() {
  hw::lockBus();  // Nesso N1 では LoRa と SPI を共有している
  canvas.pushSprite(&hw::display(), 0, 0);
  hw::unlockBus();
}

}  // namespace

void begin() {
  hw::begin();
  hw::display().fillScreen(COL_BG);
  canvas.setColorDepth(16);
  canvas.createSprite(W, H);
  // 起動時 (スリープからの復帰を含む) に押されているボタンは、離してもイベントにしない
  for (int i = 0; i < 2; i++) {
    bool raw = hw::button(i);
    btn[i].down = raw;
    btn[i].longFired = raw;
  }
}

void sleepDisplay() { hw::displaySleep(); }

void drawSplash(const char* buildStamp, const char* commit) {
  canvas.fillScreen(COL_BG);
  canvas.setTextDatum(textdatum_t::middle_center);
  canvas.setFont(&fonts::Font4);
  canvas.setTextSize(1);
  canvas.setTextColor(TFT_WHITE, COL_BG);
  canvas.drawString("MorseComm", W / 2, H / 2 - 18);
  useLabelFont();
  canvas.setTextDatum(textdatum_t::middle_center);
  canvas.setTextColor(COL_LABEL, COL_BG);
  canvas.drawString(buildStamp, W / 2, H / 2 + 10);
  canvas.drawString(commit, W / 2, H / 2 + 28);
  canvas.setTextDatum(textdatum_t::top_left);
  present();
}

void addChar(bool rx, char c) { (rx ? rxText : txText).add(c); }

void clearText() {
  rxText.clear();
  txText.clear();
}

Button pollButtons(uint32_t nowMs) {
  bool raw[2] = {hw::button(0), hw::button(1)};
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
  if (!hw::touch(ev.x, ev.y)) {
    touchState.down = false;
    return ev;
  }
  uint32_t now = millis();
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
    ev.repeat = true;
  } else {
    return ev;
  }
  ev.kind = kind;
  if (kind == TouchEvent::Row) {
    int row = listTop + (ev.y - ROW_Y0) / ROW_H;  // 表示順の index (itemAt で項目にする)
    if (ev.y < ROW_Y0 || row >= itemCount()) ev.kind = TouchEvent::None;
    ev.row = row;
  }
  return ev;
}

bool touchAvailable() { return hw::hasTouch(); }

bool itemShown(Item it) { return it != Item::Output || hw::hasPhoneJack(); }

int itemCount() {
  int n = 0;
  for (int i = 0; i < (int)Item::Count; i++) n += itemShown((Item)i);
  return n;
}

Item itemAt(int index) {
  for (int i = 0; i < (int)Item::Count; i++)
    if (itemShown((Item)i) && index-- == 0) return (Item)i;
  return Item::Radio;
}

int itemIndex(Item it) {
  int n = 0;
  for (int i = 0; i < (int)it; i++) n += itemShown((Item)i);
  return n;
}

Item stepItem(Item it, int dir) {
  int n = itemCount();
  return itemAt((itemIndex(it) + n + dir) % n);
}

Battery readBattery() { return hw::battery(); }

void drawMain(const Settings& s, const Control& c, const Snapshot& snap, const Battery& bat, int sleepInSec,
              bool showChannel, const char* pending) {
  char tmp[48];
  canvas.fillScreen(COL_BG);
  useLabelFont();

  // 1 行目: チャンネル (LoRa 使用中は "LoRa1" のように紫で、電波 OFF では灰色の "OFF")、リンク状態、電池
  bool lora = net::radio() == cfg::RADIO_LORA;
  bool radioOff = net::radio() == cfg::RADIO_OFF;
  if (radioOff) snprintf(tmp, sizeof(tmp), "OFF");
  else snprintf(tmp, sizeof(tmp), "%s%u", lora ? "LoRa" : "CH", s.channel);
  uint16_t chBg = radioOff ? COL_OFF_BADGE : lora ? COL_LORA : COL_SEL;
  canvas.fillRoundRect(0, Y_STATUS, 8 * strlen(tmp) + 6, 16, 3, chBg);
  canvas.setTextColor(TFT_WHITE, chBg);
  canvas.drawString(tmp, 3, Y_STATUS);
  int x = 8 * strlen(tmp) + 12;
  uint32_t total = snap.rx.packets + snap.rx.lost;
  int lossPct = total ? (int)(100.0f * snap.rx.lost / total + 0.5f) : 0;
  if (radioOff) {
    canvas.setTextColor(COL_LABEL, COL_BG);
    canvas.drawString("RADIO OFF", x, Y_STATUS);
  } else if (!snap.linkUp) {
    // 受信中の局がいない: 一度も受信していないか、全局が途絶えた
    canvas.setTextColor(COL_WARN, COL_BG);
    if (snap.rx.packets == 0) snprintf(tmp, sizeof(tmp), "NO SIGNAL");
    else snprintf(tmp, sizeof(tmp), "LOST %lus", (unsigned long)((snap.nowMs - snap.rx.lastRxMs) / 1000));
    canvas.drawString(tmp, x, Y_STATUS);
  } else {
    canvas.setTextColor(COL_LABEL, COL_BG);
    // 2 局以上いるときは局数を先頭に (x2 など)
    if (snap.stations > 1) {
      snprintf(tmp, sizeof(tmp), "x%d ", snap.stations);
      canvas.drawString(tmp, x, Y_STATUS);
      x += canvas.textWidth(tmp);
    }
    snprintf(tmp, sizeof(tmp), "%ddBm L%d%% R%lu W%lu", snap.rssi, lossPct, (unsigned long)snap.rx.resyncs,
             (unsigned long)snap.rx.watchdogs);
    canvas.drawString(tmp, x, Y_STATUS);
  }
  drawBattery(bat);
  if (sleepInSec >= 0) {
    canvas.fillRect(0, Y_STATUS, 170, 16, COL_BG);
    canvas.setTextColor(COL_WARN, COL_BG);
    snprintf(tmp, sizeof(tmp), "SLEEP in %ds", sleepInSec);
    canvas.drawString(tmp, 0, Y_STATUS);
  }

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
  // 打ったがまだ送っていない文字 (キーボード / 端末モード) を灰色で。収まらなければ末尾を表示する
  if (pending && *pending) {
    constexpr int PX = 92, PW = W - 22 - PX;
    const char* p = pending;
    while (*p && canvas.textWidth(p) > PW) p++;
    canvas.setTextColor(COL_DIM, COL_BG);
    canvas.drawString(p, PX, Y_TX_LABEL);
  }
  drawText(txText, TX_ROWS, Y_TX_TEXT, COL_TX);

  // チャンネル切り替え直後は番号を大きく表示する
  if (showChannel && !radioOff) {
    constexpr int BW = 150, BH = 70;
    int bx = (W - BW) / 2, by = (H - BH) / 2 + 8;
    canvas.fillRoundRect(bx, by, BW, BH, 10, chBg);
    canvas.drawRoundRect(bx, by, BW, BH, 10, TFT_WHITE);
    canvas.setFont(&fonts::Font4);
    canvas.setTextSize(2);
    canvas.setTextColor(TFT_WHITE, chBg);
    canvas.setTextDatum(textdatum_t::middle_center);
    snprintf(tmp, sizeof(tmp), "CH %u", s.channel);
    canvas.drawString(tmp, W / 2, by + BH / 2 + 2);
    canvas.setTextDatum(textdatum_t::top_left);
  }

  present();
}

void drawSettings(const Settings& s, Item selected, bool editing, const Battery& bat, DefaultsState defaults) {
  defaultsState = defaults;
  char tmp[32];
  bool touch = hw::hasTouch();
  int listW = touch ? LIST_W : W;  // タッチの無いボードは右側の +/- ボタンを描かない
  canvas.fillScreen(COL_BG);
  useLabelFont();
  if (touch) {
    canvas.setTextColor(TFT_WHITE, COL_BG);
    canvas.drawString("SETTINGS", 0, 0);
    canvas.setTextColor(COL_DIM, COL_BG);
    canvas.drawString("hold KEY: exit", 72, 0);
  } else if (hw::hasKeyboard()) {
    // キーボードの操作 (矢印は fn と ; . , / で、設定画面では fn なしでもよい)
    canvas.setTextColor(COL_DIM, COL_BG);
    canvas.drawString("\x18\x19:item \x1b\x1a/OK:change esc:exit", 0, 0);
  } else {
    // ボタンの役割を表示 (A = 正面, B = 側面)
    canvas.setTextColor(editing ? COL_EDIT : COL_DIM, COL_BG);
    canvas.drawString(editing ? "A:+  B:-  hold:done" : "A:next  B:edit  hold:exit", 0, 0);
  }
  drawBattery(bat);

  // 選択中の項目が見えるようにスクロールする
  int count = itemCount();
  int sel = itemIndex(selected);
  if (sel < listTop) listTop = sel;
  if (sel >= listTop + VISIBLE_ROWS) listTop = sel - VISIBLE_ROWS + 1;
  if (listTop > count - VISIBLE_ROWS) listTop = count > VISIBLE_ROWS ? count - VISIBLE_ROWS : 0;
  for (int i = listTop; i < count && i < listTop + VISIBLE_ROWS; i++) {
    Item it = itemAt(i);
    int y = ROW_Y0 + (i - listTop) * ROW_H;
    bool sel = it == selected;
    uint16_t bg = !sel ? COL_BG : editing ? COL_EDIT : COL_SEL;
    if (sel) canvas.fillRect(0, y, listW, ROW_H, bg);
    canvas.setTextColor(sel ? TFT_WHITE : COL_LABEL, bg);
    canvas.drawString(itemLabel(it), 3, y + 1);
    itemValue(s, it, tmp, sizeof(tmp));
    if (sel && editing) {
      char t2[28];
      snprintf(t2, sizeof(t2), "- %s +", tmp);
      strcpy(tmp, t2);
    }
    canvas.setTextColor(TFT_WHITE, bg);
    canvas.setTextDatum(textdatum_t::top_right);
    canvas.drawString(tmp, listW - 3, y + 1);
    canvas.setTextDatum(textdatum_t::top_left);
  }
  if (touch) {
    drawTouchButton(BTN_PLUS_Y, true);    // 増やす
    drawTouchButton(BTN_MINUS_Y, false);  // 減らす
  }
  present();
}

}  // namespace ui
