// power_cardputer.cpp — M5Stack Cardputer (初代): USB 接続の検出とディープスリープ
//
// PMIC が無いので USB 給電そのものは検出できない。代わりに USB シリアル (USB Serial/JTAG) が PC と通信しているか
// (HWCDC::isPlugged) で判定する。PC につないでいれば眠らない。充電器だけにつないだ場合は電池運用と同じく眠る。
// スリープ中はバックライト (G38, PWM) を LOW に保持し、G0 のボタン (BtnA) で起こす。保持の解除は hw_m5stick.cpp の begin()。
#include "board.h"
#include "build_info.h"
BUILD_INFO_UNIT();
#if defined(BOARD_CARDPUTER)
#include "power.h"
#include <M5Unified.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>

namespace power {
namespace {
constexpr gpio_num_t PIN_BTN = GPIO_NUM_0;        // BtnA, 押下 = LOW
constexpr gpio_num_t PIN_BACKLIGHT = GPIO_NUM_38;
}  // namespace

bool usbPowered() { return HWCDC::isPlugged(); }

bool wokeFromSleep() { return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1; }

void printStatus() {
  Serial.printf("power: usb(host)=%d bat=%dmV btnA=%d wake=%d\r\n", usbPowered(), M5.Power.getBatteryVoltage(),
                digitalRead(PIN_BTN), (int)esp_sleep_get_wakeup_cause());
}

bool enterDeepSleep() {
  if (digitalRead(PIN_BTN) == LOW) return false;  // 押されていると即復帰してしまう
  M5.Speaker.end();
  gpio_reset_pin(PIN_BACKLIGHT);
  gpio_set_direction(PIN_BACKLIGHT, GPIO_MODE_OUTPUT);
  gpio_set_level(PIN_BACKLIGHT, 0);
  gpio_hold_en(PIN_BACKLIGHT);
  rtc_gpio_pullup_en(PIN_BTN);
  rtc_gpio_pulldown_dis(PIN_BTN);
  esp_sleep_enable_ext1_wakeup_io(1ULL << PIN_BTN, ESP_EXT1_WAKEUP_ANY_LOW);
  Serial.flush();
  esp_deep_sleep_start();
  return true;  // ここには来ない
}

}  // namespace power
#endif
