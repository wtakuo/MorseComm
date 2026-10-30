// tone_out_sticks3.cpp — M5StickS3: 内蔵スピーカー (ES8311 + アンプ) を M5Unified の Speaker で駆動
// サイドトーンの遅れを小さくするため、I2S の DMA バッファを既定 (256 x 8 ≒ 93ms @22kHz) より大幅に短くする。
#include "board.h"
#if defined(BOARD_STICKS3)
#include "tone_out.h"
#include <M5Unified.h>

namespace {
constexpr size_t DMA_BUF_LEN = 64;    // サンプル数
constexpr size_t DMA_BUF_COUNT = 4;   // → 64 x 4 / 22050Hz ≒ 12ms
constexpr uint8_t SPK_TASK_PRIORITY = 10;
constexpr int CHANNEL = 0;
// 音量段階ごとの M5.Speaker の音量 (0..255)
constexpr uint8_t VOLUME[ToneOut::VOLUME_MAX + 1] = {0, 24, 56, 100, 160, 255};
}  // namespace

bool ToneOut::begin(int hz) {
  hz_ = hz;
  auto sc = M5.Speaker.config();
  sc.dma_buf_len = DMA_BUF_LEN;
  sc.dma_buf_count = DMA_BUF_COUNT;
  sc.task_priority = SPK_TASK_PRIORITY;
  M5.Speaker.config(sc);
  ready_ = M5.Speaker.begin();
  M5.Speaker.setVolume(VOLUME[vol_]);
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
  if (ready_) M5.Speaker.setVolume(VOLUME[vol_]);
}
#endif
