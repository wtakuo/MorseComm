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
// 画面と共有している SPI バスの排他 (Nesso N1: 画面と LoRa が同じ SPI。別タスクから使うので必要)。
// 画面に描く側も LoRa 側も、SPI を使う間はこれで囲む。StickS3 では何もしない
void lockBus();
void unlockBus();

// キーボード (Cardputer)。押された瞬間のキーを 1 つずつ返す (無ければ KEY_NONE)。実装は keyboard_cardputer.cpp
enum : int {
  KEY_NONE = -1,
  KEY_ENTER = '\r',
  KEY_BACKSPACE = 0x08,
  KEY_TAB = '\t',
  KEY_ESC = 0x1B,
  KEY_UP = 0x100,  // fn + ;
  KEY_DOWN,        // fn + .
  KEY_LEFT,        // fn + ,
  KEY_RIGHT,       // fn + /
};
bool hasKeyboard();
int readKey();

}  // namespace hw
