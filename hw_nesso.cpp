// hw_nesso.cpp — Arduino Nesso N1 の画面・ボタン・タッチ・電池
#include "board.h"
#include "build_info.h"
BUILD_INFO_UNIT();
#if defined(BOARD_NESSO_N1)
#include "hw.h"
#include <Arduino_Nesso_N1.h>

namespace hw {
namespace {

// タッチパネル (FT6336, 生座標はパネル縦置き 135x240) → 画面 (setRotation(1), 240x135)
// 「xy 入れ替え + y 反転」で正しいことを実機で確認済み
constexpr bool TOUCH_SWAP_XY = true;
constexpr bool TOUCH_INVERT_X = false;
constexpr bool TOUCH_INVERT_Y = true;

NessoDisplay nessoDisplay;
NessoTouch nessoTouch;
NessoBattery nessoBattery;
bool touchOk = false;
SemaphoreHandle_t busMutex = nullptr;

}  // namespace

void begin() {
  busMutex = xSemaphoreCreateMutex();
  nessoDisplay.begin();
  nessoDisplay.setRotation(1);
  touchOk = nessoTouch.begin();
}

lgfx::LGFX_Device& display() { return nessoDisplay; }

bool button(int i) { return digitalRead(i == 0 ? KEY1 : KEY2) == LOW; }

bool hasTouch() { return touchOk; }

bool touch(int16_t& x, int16_t& y) {
  int16_t a, b;
  if (!touchOk || !nessoTouch.read(a, b)) return false;
  int16_t aMax = 134, bMax = 239;  // パネル縦置きの範囲
  if (TOUCH_SWAP_XY) {
    int16_t t = a;
    a = b;
    b = t;
    t = aMax;
    aMax = bMax;
    bMax = t;
  }
  if (TOUCH_INVERT_X) a = aMax - a;
  if (TOUCH_INVERT_Y) b = bMax - b;
  x = a;
  y = b;
  return true;
}

Battery battery() {
  Battery b;
  uint16_t pct = nessoBattery.getChargeLevel();
  if (pct <= 100) {
    b.valid = true;
    b.percent = pct;
    NessoBattery::ChargeStatus st = nessoBattery.getChargeStatus();
    b.charging = st == NessoBattery::PRE_CHARGE || st == NessoBattery::CHARGING;
  }
  return b;
}

void displaySleep() {
  lockBus();
  nessoDisplay.fillScreen(TFT_BLACK);
  nessoDisplay.sleep();
  unlockBus();
}

bool hasPhoneJack() { return false; }

void lockBus() {
  if (busMutex) xSemaphoreTake(busMutex, portMAX_DELAY);
}

void unlockBus() {
  if (busMutex) xSemaphoreGive(busMutex);
}

}  // namespace hw
#endif
