// hw_m5stick.cpp - Screen, buttons, battery for M5StickS3 / M5StickC Plus2 /
// M5StickC Plus / Cardputer (uses M5Unified; no touch; the Cardputer keyboard
// is in keyboard_cardputer.cpp)
#include "board.h"
#include "build_info.h"
BUILD_INFO_UNIT();
#if defined(BOARD_M5UNIFIED)
#include "hw.h"
#include <M5Unified.h>
#if defined(BOARD_STICKC_PLUS2) || defined(BOARD_CARDPUTER)
#include <driver/gpio.h>
#endif

namespace hw {
namespace {
#if defined(BOARD_STICKS3)
constexpr int PIN_BTN[2] = {11,
                            12};  // BtnA (front), BtnB (side). Pressed = LOW
#elif defined(BOARD_CARDPUTER)
// Only one button, BtnA (GPIO 0). It takes the role of BtnB on other boards
// (settings screen / long press clears text). BtnA's roles on other boards,
// such as channel switching, are done with the keyboard
constexpr int PIN_BTN[2] = {-1, 0};
#else
constexpr int PIN_BTN[2] = {37,
                            39};  // StickC Plus2 / Plus: BtnA (front), BtnB
                                  // (side). Pressed = LOW (external pull-up)
#endif
}  // namespace

void begin() {
#if defined(BOARD_STICKC_PLUS2)
    // Release pins held during deep sleep (power_stickcplus2.cpp).
    // Drive the power hold pin G4 HIGH first so power is not cut the moment it
    // is released
    gpio_set_direction(GPIO_NUM_4, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_4, 1);
    gpio_hold_dis(GPIO_NUM_4);
    gpio_hold_dis(GPIO_NUM_27);  // Backlight
#elif defined(BOARD_CARDPUTER)
    gpio_hold_dis(
        GPIO_NUM_38);  // Backlight held LOW during sleep (power_cardputer.cpp)
#endif
    auto cfg = M5.config();
    cfg.serial_baudrate = 0;  // Serial is already initialized in setup()
    cfg.clear_display = true;
#if defined(BOARD_STICKS3) || defined(BOARD_CARDPUTER)
    cfg.internal_spk = true;  // StickS3: ES8311 / Cardputer: NS4168 speaker
                              // (tone_out_m5speaker.cpp)
#else
    cfg.internal_spk = false;  // The StickC Plus2 / Plus buzzer (G2) is driven
                               // directly via LEDC by tone_out_buzzer.cpp
#endif
    cfg.internal_mic = false;
    // Grove is used for key input, so do not probe for external I2C devices
    cfg.external_imu = false;
    cfg.external_rtc = false;
    cfg.external_display.module_display = false;
    M5.begin(cfg);
    M5.Display.setRotation(1);
}

lgfx::LGFX_Device& display() { return M5.Display; }

bool button(int i) { return PIN_BTN[i] >= 0 && digitalRead(PIN_BTN[i]) == LOW; }

bool hasTouch() { return false; }

bool touch(int16_t&, int16_t&) { return false; }

Battery battery() {
    Battery b;
    int pct = M5.Power.getBatteryLevel();
    if (pct >= 0 && pct <= 100) {
        b.valid = true;
        b.percent = pct;
        // StickC Plus2 cannot detect the charging state (charge_unknown), so
        // always false
        b.charging = M5.Power.isCharging() == m5::Power_Class::is_charging;
    }
    return b;
}

void displaySleep() {
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setBrightness(0);
    M5.Display.sleep();
}

bool hasPhoneJack() {
#if defined(BOARD_CARDPUTER)
    return M5.getBoard() ==
           m5::board_t::board_M5CardputerADV;  // The original and ADV share the
                                               // same build; M5Unified
                                               // identifies the board at
                                               // startup
#else
    return false;
#endif
}

void lockBus() {}
void unlockBus() {}

}  // namespace hw
#endif
