// power_stickc.cpp - M5StickC Plus2 / M5StickC Plus: USB power detection and
// deep sleep
//
// Both wake with BtnA (G37) / BtnB (G39). The ESP32 (plain) EXT1 cannot use
// ANY_LOW, so BtnA uses EXT0 and BtnB uses EXT1 (ALL_LOW, 1 pin).
//
// Plus2: no PMIC; on battery, the power hold pin HOLD (G4) must stay HIGH or
// power is cut.
//   G4 is held HIGH (gpio_hold) during deep sleep too. The hold is released at
//   startup in begin() of hw_m5stick.cpp. USB power cannot be detected.
// Plus: the AXP192 PMIC keeps power (unchanged during ESP32 deep sleep). USB
// power is detected via the AXP192 VBUS voltage.
#include "board.h"
#if defined(BOARD_STICKC_PLUS2) || defined(BOARD_STICKC_PLUS)
#include "power.h"
#include <M5Unified.h>
#include <esp_sleep.h>
#include <driver/gpio.h>

namespace power {
namespace {
constexpr gpio_num_t PIN_BTN_A = GPIO_NUM_37,
                     PIN_BTN_B =
                         GPIO_NUM_39;  // External pull-up, pressed = LOW
#if defined(BOARD_STICKC_PLUS2)
constexpr gpio_num_t PIN_HOLD = GPIO_NUM_4;
constexpr gpio_num_t PIN_BACKLIGHT = GPIO_NUM_27;
#else
constexpr int16_t VBUS_PRESENT_MV = 4000;
#endif
}  // namespace

#if defined(BOARD_STICKC_PLUS2)
// No means to detect USB power (no VBUS detect line, no PMIC). Always treated
// as battery operation (confirmed by the user)
bool usbPowered() { return false; }
#else
bool usbPowered() { return M5.Power.getVBUSVoltage() > VBUS_PRESENT_MV; }
#endif

bool wokeFromSleep() {
    esp_sleep_wakeup_cause_t c = esp_sleep_get_wakeup_cause();
    return c == ESP_SLEEP_WAKEUP_EXT0 || c == ESP_SLEEP_WAKEUP_EXT1;
}

void printStatus() {
#if defined(BOARD_STICKC_PLUS2)
    Serial.printf("power: usb=unknown bat=%dmV btnA=%d btnB=%d wake=%d\r\n",
                  M5.Power.getBatteryVoltage(), digitalRead(PIN_BTN_A),
                  digitalRead(PIN_BTN_B), (int)esp_sleep_get_wakeup_cause());
#else
    Serial.printf(
        "power: usb=%d vbus=%dmV bat=%dmV charging=%d btnA=%d btnB=%d "
        "wake=%d\r\n",
        usbPowered(), M5.Power.getVBUSVoltage(), M5.Power.getBatteryVoltage(),
        (int)M5.Power.isCharging(), digitalRead(PIN_BTN_A),
        digitalRead(PIN_BTN_B), (int)esp_sleep_get_wakeup_cause());
#endif
}

bool enterDeepSleep() {
    if (digitalRead(PIN_BTN_A) == LOW || digitalRead(PIN_BTN_B) == LOW)
        return false;  // If pressed, it would wake up immediately
#if defined(BOARD_STICKC_PLUS2)
    // Lock the power hold HIGH and the backlight off
    gpio_set_level(PIN_HOLD, 1);
    gpio_hold_en(PIN_HOLD);
    gpio_reset_pin(PIN_BACKLIGHT);
    gpio_set_direction(PIN_BACKLIGHT, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_BACKLIGHT, 0);
    gpio_hold_en(PIN_BACKLIGHT);
#endif
    // (the Plus backlight is powered by the AXP192 and has been turned off by
    // hw::displaySleep())
    esp_sleep_enable_ext0_wakeup(PIN_BTN_A, 0);
    esp_sleep_enable_ext1_wakeup_io(1ULL << PIN_BTN_B, ESP_EXT1_WAKEUP_ALL_LOW);
    Serial.flush();
    esp_deep_sleep_start();
    return true;  // Never reached
}

}  // namespace power
#endif
