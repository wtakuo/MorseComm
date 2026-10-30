// hw.h — 画面まわりのハードウェア抽象 (画面・ボタン・タッチ・電池)
// 実装はボードごと: hw_nesso.cpp / hw_sticks3.cpp。I2C を使うので loop() 側からのみ呼ぶこと。
#pragma once
#include <stdint.h>
#include <M5GFX.h>

namespace hw {

struct Battery {
  bool valid = false;
  int percent = 0;
  bool charging = false;
};

// ボードの初期化 (画面を 240x135 横長にする)
void begin();
lgfx::LGFX_Device& display();
// 物理ボタン: 0 = 正面 (Nesso: KEY1 / StickS3: BtnA), 1 = 側面 (Nesso: KEY2 / StickS3: BtnB)。押下中なら true
bool button(int i);
bool hasTouch();
// タッチ中なら画面座標を返す
bool touch(int16_t& x, int16_t& y);
Battery battery();
// スリープ前に画面を消す
void displaySleep();

}  // namespace hw
