#include "tone_out.h"
#include <Arduino.h>

namespace {
constexpr uint8_t RES_BITS = 10;  // デューティ 0..1023
// 音量段階ごとのデューティ (50% が最大音量)
constexpr uint16_t DUTY[ToneOut::VOLUME_MAX + 1] = {0, 8, 32, 96, 256, 512};
}  // namespace

bool ToneOut::begin(int pin, int hz) {
  pin_ = pin;
  bool ok = ledcAttach(pin_, hz, RES_BITS);
  ledcWrite(pin_, 0);
  return ok;
}

uint32_t ToneOut::duty() const { return DUTY[vol_]; }

void ToneOut::on() {
  if (on_ || pin_ < 0) return;
  on_ = true;
  ledcWrite(pin_, duty());
}

void ToneOut::off() {
  if (!on_ || pin_ < 0) return;
  on_ = false;
  ledcWrite(pin_, 0);
}

void ToneOut::setFrequency(int hz) {
  if (pin_ < 0) return;
  ledcChangeFrequency(pin_, hz, RES_BITS);
  if (on_) ledcWrite(pin_, duty());
}

void ToneOut::setVolume(int vol) {
  if (vol < 0) vol = 0;
  if (vol > VOLUME_MAX) vol = VOLUME_MAX;
  vol_ = vol;
  if (on_ && pin_ >= 0) ledcWrite(pin_, duty());
}
