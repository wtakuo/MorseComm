// tone_out.h - Tone output abstraction
// Implemented per board: tone_out_nesso.cpp (drives the built-in buzzer with
// LEDC PWM) /
//                   tone_out_sticks3.cpp (drives the built-in speaker with
//                   M5Unified's Speaker)
#pragma once
#include <stdint.h>

class ToneOut {
  public:
    bool begin(int hz);
    void on();
    void off();
    bool isOn() const { return on_; }
    void setFrequency(int hz);
    // Volume 0..VOLUME_MAX (0 mutes)
    void setVolume(int vol);
    static constexpr int VOLUME_MAX = 5;
    // Use the smaller volume steps for headphones (Cardputer ADV; other boards
    // keep the same volume steps)
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
