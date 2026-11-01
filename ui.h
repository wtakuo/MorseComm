// ui.h — 画面表示とボタン・タッチ入力 (240x135 の横長画面。ボードの違いは hw.h が吸収する)
// I2C を使うので loop() 側からのみ呼ぶこと。
#pragma once
#include <stdint.h>
#include "shared.h"
#include "hw.h"

namespace ui {

// 設定画面の項目。並び順がそのまま画面の表示順 (2026-11-01 ユーザー指定)
// Output = 音の出口 (スピーカー / ヘッドフォン。ヘッドフォン端子のある Cardputer ADV だけに出す)
// Defaults = すべての設定を既定値に戻す (2 回押して実行)
enum class Item : uint8_t { Radio, Mode, Swap, Wpm, SideOn, Output, Volume, SideHz, RxHz, Defaults, Count };

// DEFAULTS 項目の表示状態: 未操作 / 1 回押した (もう 1 回で実行) / 実行した
enum class DefaultsState : uint8_t { Idle, Armed, Done };
// このボードで表示する項目の数と、表示順の index ⇔ 項目の変換。項目の移動は stepItem を使うこと
int itemCount();
Item itemAt(int index);
int itemIndex(Item it);
Item stepItem(Item it, int dir);

enum class Button : uint8_t { None, Key1Short, Key1Long, Key2Short, Key2Long };

struct TouchEvent {
  enum Kind : uint8_t { None, Plus, Minus, Row } kind = None;
  int row = 0;      // kind == Row のとき、タッチされた設定項目
  bool repeat = false;  // +/- の押し続けによる連続イベント
  int16_t x = 0, y = 0;
};

using Battery = hw::Battery;

void begin();
// 画面を消す (スリープ前)
void sleepDisplay();
// 起動時のスプラッシュスクリーン: 中央に "MorseComm"、その下にビルド日時と git のコミットハッシュ
void drawSplash(const char* buildStamp, const char* commit);
void addChar(bool rx, char c);
void clearText();

Button pollButtons(uint32_t nowMs);
// 設定画面でのタッチ (押した瞬間だけイベントを返す)
TouchEvent pollTouch(bool settingsScreen);
Battery readBattery();
bool touchAvailable();

// sleepInSec >= 0 のとき、スリープまでの残り秒数を表示する。showChannel のときチャンネル番号を大きく表示する
// pending: 打ったがまだ送っていない文字 (TX の行に灰色で出す)
void drawMain(const Settings& s, const Control& c, const Snapshot& snap, const Battery& bat, int sleepInSec,
              bool showChannel, const char* pending = nullptr);
// editing: タッチの無いボードで、選択中の項目の値を変更中 (ボタンが +/- になる)
void drawSettings(const Settings& s, Item selected, bool editing, const Battery& bat,
                  DefaultsState defaults = DefaultsState::Idle);

}  // namespace ui
