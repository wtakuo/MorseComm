// tone_out_m5speaker.cpp - Drives the built-in speaker with M5Unified's Speaker
// M5StickS3: ES8311 + amplifier / Cardputer: NS4168 (I2S amplifier)
// To reduce sidetone latency, make the I2S DMA buffer much shorter than the
// default (256 x 8 ~= 93ms @22kHz).
#include "board.h"
#include "build_info.h"
BUILD_INFO_UNIT();
#if defined(BOARD_STICKS3) || defined(BOARD_CARDPUTER)
#include "tone_out.h"
#include <M5Unified.h>

namespace {
constexpr size_t DMA_BUF_LEN = 64;   // Number of samples
constexpr size_t DMA_BUF_COUNT = 4;  // -> 64 x 4 / 22050Hz ~= 12ms
constexpr uint8_t SPK_TASK_PRIORITY = 10;
constexpr int CHANNEL = 0;
// M5.Speaker volume for each volume step (0..255). Headphones need a much lower
// level than the speaker
constexpr uint8_t VOLUME[ToneOut::VOLUME_MAX + 1] = {0, 24, 56, 100, 160, 255};
constexpr uint8_t VOLUME_PHONES[ToneOut::VOLUME_MAX + 1] = {0,  3,  6,
                                                            12, 24, 48};
}  // namespace

bool ToneOut::begin(int hz) {
    hz_ = hz;
    auto sc = M5.Speaker.config();
    sc.dma_buf_len = DMA_BUF_LEN;
    sc.dma_buf_count = DMA_BUF_COUNT;
    sc.task_priority = SPK_TASK_PRIORITY;
    M5.Speaker.config(sc);
    ready_ = M5.Speaker.begin();
    M5.Speaker.setVolume((phones_ ? VOLUME_PHONES : VOLUME)[vol_]);
    return ready_;
}

void ToneOut::on() {
    if (on_ || !ready_) return;
    on_ = true;
    M5.Speaker.tone(hz_, UINT32_MAX, CHANNEL, true);
}

void ToneOut::off() {
    if (!on_ || !ready_) return;
    on_ = false;
    M5.Speaker.stop(CHANNEL);
}

void ToneOut::setFrequency(int hz) {
    hz_ = hz;
    if (on_ && ready_) M5.Speaker.tone(hz_, UINT32_MAX, CHANNEL, true);
}

void ToneOut::setVolume(int vol) {
    if (vol < 0) vol = 0;
    if (vol > VOLUME_MAX) vol = VOLUME_MAX;
    vol_ = vol;
    if (ready_) M5.Speaker.setVolume((phones_ ? VOLUME_PHONES : VOLUME)[vol_]);
}
#endif
