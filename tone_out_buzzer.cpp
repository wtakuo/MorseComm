// tone_out_buzzer.cpp — 内蔵の圧電ブザーを LEDC の PWM で駆動
// Arduino Nesso N1: GPIO11 (BEEP_PIN) / M5StickC Plus2・Plus: G2
#include "board.h"
#if defined(BOARD_NESSO_N1) || defined(BOARD_STICKC_PLUS2) || defined(BOARD_STICKC_PLUS)
#include "tone_out.h"
#include <Arduino.h>

namespace {
#if defined(BOARD_NESSO_N1)
constexpr int PIN_BUZZER = 11;    // BEEP_PIN
#else
constexpr int PIN_BUZZER = 2;
#endif
constexpr uint8_t RES_BITS = 10;  // デューティ 0..1023
// 音量段階ごとのデューティ (50% が最大音量)
constexpr uint16_t DUTY[ToneOut::VOLUME_MAX + 1] = {0, 8, 32, 96, 256, 512};
}  // namespace

bool ToneOut::begin(int hz) {
  hz_ = hz;
  ready_ = ledcAttach(PIN_BUZZER, hz, RES_BITS);
  ledcWrite(PIN_BUZZER, 0);
  return ready_;
}

void ToneOut::on() {
  if (on_ || !ready_) return;
  on_ = true;
  ledcWrite(PIN_BUZZER, DUTY[vol_]);
}

void ToneOut::off() {
  if (!on_ || !ready_) return;
  on_ = false;
  ledcWrite(PIN_BUZZER, 0);
}

void ToneOut::setFrequency(int hz) {
  if (!ready_) return;
  hz_ = hz;
  ledcChangeFrequency(PIN_BUZZER, hz, RES_BITS);
  if (on_) ledcWrite(PIN_BUZZER, DUTY[vol_]);
}

void ToneOut::setVolume(int vol) {
  if (vol < 0) vol = 0;
  if (vol > VOLUME_MAX) vol = VOLUME_MAX;
  vol_ = vol;
  if (on_ && ready_) ledcWrite(PIN_BUZZER, DUTY[vol_]);
}
#endif
