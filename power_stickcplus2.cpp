// power_stickcplus2.cpp — M5StickC Plus2: ディープスリープ (USB 給電は検出できない)
//
// Plus2 には PMIC が無く、電池運用時は電源保持ピン HOLD (G4) を HIGH にし続けないと電源が切れる。
// ディープスリープ中も G4 を HIGH に保持 (gpio_hold) し、BtnA (G37) / BtnB (G39) で起こす。
// ESP32 (無印) の EXT1 は ANY_LOW が使えないので、BtnA を EXT0、BtnB を EXT1 (ALL_LOW, 1 ピン) にする。
// 起動時の保持の解除は hw_m5stick.cpp の begin() で行う。
#include "board.h"
#if defined(BOARD_STICKC_PLUS2)
#include "power.h"
#include <M5Unified.h>
#include <esp_sleep.h>
#include <driver/gpio.h>

namespace power {
namespace {
constexpr gpio_num_t PIN_HOLD = GPIO_NUM_4;
constexpr gpio_num_t PIN_BACKLIGHT = GPIO_NUM_27;
constexpr gpio_num_t PIN_BTN_A = GPIO_NUM_37, PIN_BTN_B = GPIO_NUM_39;  // 外部プルアップ、押下 = LOW
}  // namespace

// USB 給電を検出する手段が無い (VBUS の検出線も PMIC も無い)。常に電池運用として扱う (ユーザー確認済み)
bool usbPowered() { return false; }

bool wokeFromSleep() {
  esp_sleep_wakeup_cause_t c = esp_sleep_get_wakeup_cause();
  return c == ESP_SLEEP_WAKEUP_EXT0 || c == ESP_SLEEP_WAKEUP_EXT1;
}

void printStatus() {
  Serial.printf("power: usb=unknown bat=%dmV btnA=%d btnB=%d wake=%d\r\n", M5.Power.getBatteryVoltage(),
                digitalRead(PIN_BTN_A), digitalRead(PIN_BTN_B), (int)esp_sleep_get_wakeup_cause());
}

bool enterDeepSleep() {
  if (digitalRead(PIN_BTN_A) == LOW || digitalRead(PIN_BTN_B) == LOW) return false;  // 押されていると即復帰してしまう
  // 電源保持を HIGH のまま、バックライトを消したまま固定する
  gpio_set_level(PIN_HOLD, 1);
  gpio_hold_en(PIN_HOLD);
  gpio_reset_pin(PIN_BACKLIGHT);
  gpio_set_direction(PIN_BACKLIGHT, GPIO_MODE_OUTPUT);
  gpio_set_level(PIN_BACKLIGHT, 0);
  gpio_hold_en(PIN_BACKLIGHT);
  esp_sleep_enable_ext0_wakeup(PIN_BTN_A, 0);
  esp_sleep_enable_ext1_wakeup_io(1ULL << PIN_BTN_B, ESP_EXT1_WAKEUP_ALL_LOW);
  Serial.flush();
  esp_deep_sleep_start();
  return true;  // ここには来ない
}

}  // namespace power
#endif
