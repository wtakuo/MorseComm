// power_cardputer.cpp - M5Stack Cardputer (original): USB connection detection
// and deep sleep
//
// There is no PMIC, so USB power itself cannot be detected. Instead, check
// whether the USB serial (USB Serial/JTAG) is talking to a PC
// (HWCDC::isPlugged). While connected to a PC it does not sleep. With only a
// charger connected it sleeps, same as on battery. During sleep the backlight
// (G38, PWM) is held LOW, and the G0 button (BtnA) wakes it. The hold is
// released in begin() of hw_m5stick.cpp.
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
constexpr gpio_num_t PIN_BTN = GPIO_NUM_0;  // BtnA, pressed = LOW
constexpr gpio_num_t PIN_BACKLIGHT = GPIO_NUM_38;
}  // namespace

bool usbPowered() { return HWCDC::isPlugged(); }

bool wokeFromSleep() {
    return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1;
}

void printStatus() {
    Serial.printf("power: usb(host)=%d bat=%dmV btnA=%d wake=%d\r\n",
                  usbPowered(), M5.Power.getBatteryVoltage(),
                  digitalRead(PIN_BTN), (int)esp_sleep_get_wakeup_cause());
}

bool enterDeepSleep() {
    if (digitalRead(PIN_BTN) == LOW)
        return false;  // If pressed, it would wake up immediately
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
    return true;  // Never reached
}

}  // namespace power
#endif
