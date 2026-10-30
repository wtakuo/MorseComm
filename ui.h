// ui.h — 画面表示とボタン (Nesso N1: ST7789 240x135, KEY1/KEY2 は IO エキスパンダ経由)
#pragma once
#include <stdint.h>
#include "shared.h"

namespace ui {

// ボタンで変更する項目
enum class Item : uint8_t { Mode, Wpm, Tone, Volume, Count };

enum class Button : uint8_t { None, Key1Short, Key1Long, Key2Short, Key2Long };

void begin();
void addChar(bool rx, char c);
void clearText();
// ボタンを読む (20ms 程度ごとに呼ぶ)
Button pollButtons(uint32_t nowMs);
void draw(const Settings& s, const Control& c, const Snapshot& snap, Item selected);

}  // namespace ui
