// tone_out.h — トーン出力の抽象化 (内蔵ブザーを LEDC の PWM で駆動)
#pragma once
#include <stdint.h>

class ToneOut {
 public:
  bool begin(int pin, int hz);
  void on();
  void off();
  bool isOn() const { return on_; }
  void setFrequency(int hz);
  // 音量 0..VOLUME_MAX (0 は消音)。PWM のデューティ比で変える
  void setVolume(int vol);
  static constexpr int VOLUME_MAX = 5;

 private:
  uint32_t duty() const;
  int pin_ = -1;
  int vol_ = 3;
  bool on_ = false;
};
