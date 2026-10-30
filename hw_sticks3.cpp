// hw_sticks3.cpp — M5StickS3 の画面・ボタン・電池 (M5Unified を使う。タッチなし)
#include "board.h"
#if defined(BOARD_STICKS3)
#include "hw.h"
#include <M5Unified.h>

namespace hw {
namespace {
constexpr int PIN_BTN[2] = {11, 12};  // BtnA (正面), BtnB (側面)。押下 = LOW
}

void begin() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 0;  // Serial は setup() で初期化済み
  cfg.clear_display = true;
  cfg.internal_spk = true;
  cfg.internal_mic = false;
  // Grove (Port A, GPIO9/10) はキー入力に使うので、外部 I2C 機器を探しに行かせない
  cfg.external_imu = false;
  cfg.external_rtc = false;
  cfg.external_display.module_display = false;
  M5.begin(cfg);
  M5.Display.setRotation(1);
}

lgfx::LGFX_Device& display() { return M5.Display; }

bool button(int i) { return digitalRead(PIN_BTN[i]) == LOW; }

bool hasTouch() { return false; }

bool touch(int16_t&, int16_t&) { return false; }

Battery battery() {
  Battery b;
  int pct = M5.Power.getBatteryLevel();
  if (pct >= 0 && pct <= 100) {
    b.valid = true;
    b.percent = pct;
    b.charging = M5.Power.isCharging() == m5::Power_Class::is_charging;
  }
  return b;
}

void displaySleep() {
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setBrightness(0);
  M5.Display.sleep();
}

void lockBus() {}
void unlockBus() {}

}  // namespace hw
#endif
