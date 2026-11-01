// power_sticks3.cpp — M5StickS3: USB 給電の検出とディープスリープ
// ボタン (BtnA = GPIO11, BtnB = GPIO12) は ESP32-S3 の RTC GPIO に直結なので、EXT1 で直接起こせる。
#include "board.h"
#include "build_info.h"
BUILD_INFO_UNIT();
#if defined(BOARD_STICKS3)
#include "power.h"
#include <M5Unified.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>

namespace power {
namespace {
constexpr int PIN_BTN_A = 11, PIN_BTN_B = 12;
constexpr int16_t VBUS_PRESENT_MV = 4000;  // USB 接続時は約 5200mV (実機で確認)
}  // namespace

bool usbPowered() { return M5.Power.getVBUSVoltage() > VBUS_PRESENT_MV; }

bool wokeFromSleep() { return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1; }

void printStatus() {
  Serial.printf("power: usb=%d vbus=%dmV bat=%dmV btnA=%d btnB=%d wake=%d\r\n", usbPowered(), M5.Power.getVBUSVoltage(),
                M5.Power.getBatteryVoltage(), digitalRead(PIN_BTN_A), digitalRead(PIN_BTN_B),
                (int)esp_sleep_get_wakeup_cause());
}

bool enterDeepSleep() {
  if (digitalRead(PIN_BTN_A) == LOW || digitalRead(PIN_BTN_B) == LOW) return false;  // 押されていると即復帰してしまう
  M5.Speaker.end();
  for (int pin : {PIN_BTN_A, PIN_BTN_B}) {
    rtc_gpio_pullup_en((gpio_num_t)pin);
    rtc_gpio_pulldown_dis((gpio_num_t)pin);
  }
  esp_sleep_enable_ext1_wakeup_io((1ULL << PIN_BTN_A) | (1ULL << PIN_BTN_B), ESP_EXT1_WAKEUP_ANY_LOW);
  Serial.flush();
  esp_deep_sleep_start();
  return true;  // ここには来ない
}

}  // namespace power
#endif
