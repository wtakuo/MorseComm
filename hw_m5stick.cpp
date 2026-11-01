// hw_m5stick.cpp — M5StickS3 / M5StickC Plus2 / M5StickC Plus / Cardputer の画面・ボタン・電池
// (M5Unified を使う。タッチなし。Cardputer のキーボードは keyboard_cardputer.cpp)
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
constexpr int PIN_BTN[2] = {11, 12};  // BtnA (正面), BtnB (側面)。押下 = LOW
#elif defined(BOARD_CARDPUTER)
// ボタンは G0 (BtnA) の 1 つだけ。他機種の KEY2 (側面: 設定画面 / 長押しでテキスト消去) の役にする。
// チャンネル切り替えなど KEY1 の役はキーボードで行う
constexpr int PIN_BTN[2] = {-1, 0};
#else
constexpr int PIN_BTN[2] = {37, 39};  // StickC Plus2 / Plus: BtnA (正面), BtnB (側面)。押下 = LOW (外部プルアップ)
#endif
}  // namespace

void begin() {
#if defined(BOARD_STICKC_PLUS2)
  // ディープスリープ中に保持していたピンを解放する (power_stickcplus2.cpp)。
  // 電源保持 G4 は、解放した瞬間に電源が切れないよう先に HIGH を出力しておく
  gpio_set_direction(GPIO_NUM_4, GPIO_MODE_OUTPUT);
  gpio_set_level(GPIO_NUM_4, 1);
  gpio_hold_dis(GPIO_NUM_4);
  gpio_hold_dis(GPIO_NUM_27);  // バックライト
#elif defined(BOARD_CARDPUTER)
  gpio_hold_dis(GPIO_NUM_38);  // スリープ中に LOW で保持したバックライト (power_cardputer.cpp)
#endif
  auto cfg = M5.config();
  cfg.serial_baudrate = 0;  // Serial は setup() で初期化済み
  cfg.clear_display = true;
#if defined(BOARD_STICKS3) || defined(BOARD_CARDPUTER)
  cfg.internal_spk = true;   // StickS3: ES8311 / Cardputer: NS4168 のスピーカー (tone_out_m5speaker.cpp)
#else
  cfg.internal_spk = false;  // StickC Plus2 / Plus のブザー (G2) は tone_out_buzzer.cpp が LEDC で直接鳴らす
#endif
  cfg.internal_mic = false;
  // Grove はキー入力に使うので、外部 I2C 機器を探しに行かせない
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
    // StickC Plus2 は充電状態を検出できない (charge_unknown) ので常に false
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
  return M5.getBoard() == m5::board_t::board_M5CardputerADV;  // 初代と ADV は同じビルドで、M5Unified が起動時に判別する
#else
  return false;
#endif
}

void lockBus() {}
void unlockBus() {}

}  // namespace hw
#endif
