// power_nesso.cpp — Arduino Nesso N1: USB 給電の検出とディープスリープ
#include "board.h"
#include "build_info.h"
BUILD_INFO_UNIT();
#if defined(BOARD_NESSO_N1)
#include "power.h"
#include <Arduino_Nesso_N1.h>
#include <Wire.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>

namespace power {
namespace {

// IO エキスパンダ PI4IOE5V6408 (0x43: KEY1=bit0, KEY2=bit1 / 0x44: VIN_DETECT=bit5 など)
constexpr uint8_t EXP_KEYS = 0x43;
constexpr uint8_t EXP_SYS = 0x44;
constexpr uint8_t REG_INT_MASK = 0x11;    // 1 = 割り込みを出さない
constexpr uint8_t REG_INT_STATUS = 0x13;  // 読むとクリア
constexpr uint8_t KEY_BITS = 0x03;
constexpr int PIN_IRQ = SYS_IRQ;          // GPIO3: エキスパンダの割り込み出力 (Low アクティブ)

void writeReg(uint8_t addr, uint8_t reg, uint8_t v) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(v);
  Wire.endTransmission();
}

uint8_t readReg(uint8_t addr, uint8_t reg) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom(addr, (uint8_t)1);
  return Wire.read();
}

}  // namespace

bool usbPowered() { return digitalRead(VIN_DETECT) == HIGH; }

bool wokeFromSleep() { return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1; }

void printStatus() {
  pinMode(PIN_IRQ, INPUT);
  Serial.printf("power: usb=%d irq(GPIO%d)=%d keyMask=%02X sysMask=%02X wake=%d\r\n", usbPowered(), PIN_IRQ,
                digitalRead(PIN_IRQ), readReg(EXP_KEYS, REG_INT_MASK), readReg(EXP_SYS, REG_INT_MASK),
                (int)esp_sleep_get_wakeup_cause());
}

bool enterDeepSleep() {
  // 割り込みはボタンだけにする
  writeReg(EXP_SYS, REG_INT_MASK, 0xFF);
  writeReg(EXP_KEYS, REG_INT_MASK, (uint8_t)~KEY_BITS);
  readReg(EXP_SYS, REG_INT_STATUS);
  readReg(EXP_KEYS, REG_INT_STATUS);
  delay(5);
  pinMode(PIN_IRQ, INPUT_PULLUP);
  if (digitalRead(PIN_IRQ) == LOW) {
    // 割り込みが消えない (ボタンが押されている等): 今回は眠らない
    writeReg(EXP_KEYS, REG_INT_MASK, 0xFF);
    return false;
  }
  // 画面を消す
  digitalWrite(LCD_BACKLIGHT, LOW);
  rtc_gpio_pullup_en((gpio_num_t)PIN_IRQ);
  rtc_gpio_pulldown_dis((gpio_num_t)PIN_IRQ);
  esp_sleep_enable_ext1_wakeup_io(1ULL << PIN_IRQ, ESP_EXT1_WAKEUP_ANY_LOW);
  Serial.flush();
  esp_deep_sleep_start();
  return true;  // ここには来ない
}

}  // namespace power
#endif
