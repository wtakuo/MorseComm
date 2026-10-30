// ui.h — 画面表示・ボタン・タッチ・電池 (Nesso N1: ST7789 240x135, KEY1/KEY2 と電池ゲージは I2C)
// I2C を使うので loop() 側からのみ呼ぶこと。
#pragma once
#include <stdint.h>
#include "shared.h"

namespace ui {

// 設定画面の項目
enum class Item : uint8_t { Mode, Wpm, SideOn, SideHz, RxHz, Volume, Swap, Count };

enum class Button : uint8_t { None, Key1Short, Key1Long, Key2Short, Key2Long };

struct TouchEvent {
  enum Kind : uint8_t { None, Plus, Minus, Row } kind = None;
  int row = 0;      // kind == Row のとき、タッチされた設定項目
  int16_t x = 0, y = 0;
};

struct Battery {
  bool valid = false;
  int percent = 0;
  bool charging = false;
};

void begin();
void addChar(bool rx, char c);
void clearText();

Button pollButtons(uint32_t nowMs);
// 設定画面でのタッチ (押した瞬間だけイベントを返す)
TouchEvent pollTouch(bool settingsScreen);
Battery readBattery();
bool touchAvailable();

void drawMain(const Control& c, const Snapshot& snap, const Battery& bat);
void drawSettings(const Settings& s, Item selected, const Battery& bat);

}  // namespace ui
