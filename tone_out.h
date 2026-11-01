// tone_out.h — トーン出力の抽象化
// 実装はボードごと: tone_out_nesso.cpp (内蔵ブザーを LEDC の PWM で駆動) /
//                   tone_out_sticks3.cpp (内蔵スピーカーを M5Unified の Speaker で駆動)
#pragma once
#include <stdint.h>

class ToneOut {
 public:
  bool begin(int hz);
  void on();
  void off();
  bool isOn() const { return on_; }
  void setFrequency(int hz);
  // 音量 0..VOLUME_MAX (0 は消音)
  void setVolume(int vol);
  static constexpr int VOLUME_MAX = 5;
  // ヘッドフォン向けの小さい音量段階にする (Cardputer ADV。ほかのボードでは音量の段階は変わらない)
  void setPhones(bool phones) {
    phones_ = phones;
    setVolume(vol_);
  }

 private:
  bool phones_ = false;
  int vol_ = 3;
  int hz_ = 0;
  bool on_ = false;
  bool ready_ = false;
};
