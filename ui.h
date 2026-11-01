// ui.h — 画面表示とボタン・タッチ入力 (240x135 の横長画面。ボードの違いは hw.h が吸収する)
// I2C を使うので loop() 側からのみ呼ぶこと。
#pragma once
#include <stdint.h>
#include "shared.h"
#include "hw.h"

namespace ui {

// 設定画面の項目 (項目数は itemCount() を使うこと)
// 並び順がそのまま画面の表示順 (2026-11-01 ユーザー指定)
enum class Item : uint8_t { Radio, Mode, Swap, Wpm, SideOn, Volume, SideHz, RxHz, Count };
int itemCount();

enum class Button : uint8_t { None, Key1Short, Key1Long, Key2Short, Key2Long };

struct TouchEvent {
  enum Kind : uint8_t { None, Plus, Minus, Row } kind = None;
  int row = 0;      // kind == Row のとき、タッチされた設定項目
  int16_t x = 0, y = 0;
};

using Battery = hw::Battery;

void begin();
// 画面を消す (スリープ前)
void sleepDisplay();
void addChar(bool rx, char c);
void clearText();

Button pollButtons(uint32_t nowMs);
// 設定画面でのタッチ (押した瞬間だけイベントを返す)
TouchEvent pollTouch(bool settingsScreen);
Battery readBattery();
bool touchAvailable();

// sleepInSec >= 0 のとき、スリープまでの残り秒数を表示する。showChannel のときチャンネル番号を大きく表示する
void drawMain(const Settings& s, const Control& c, const Snapshot& snap, const Battery& bat, int sleepInSec,
              bool showChannel);
// editing: タッチの無いボードで、選択中の項目の値を変更中 (ボタンが +/- になる)
void drawSettings(const Settings& s, Item selected, bool editing, const Battery& bat);

}  // namespace ui
