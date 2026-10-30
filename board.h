// board.h — ビルド対象のボードの選択と、ボードごとのキー入力ピン
// ボードは FQBN が定義するマクロで判別する:
//   Arduino Nesso N1 : esp32:esp32:arduino_nesso_n1        → ARDUINO_ARDUINO_NESSO_N1
//   M5StickS3        : m5stack:esp32:m5stack_sticks3       → ARDUINO_M5STACK_STICKS3
#pragma once

#if defined(ARDUINO_ARDUINO_NESSO_N1)
#define BOARD_NESSO_N1 1
#elif defined(ARDUINO_M5STACK_STICKS3)
#define BOARD_STICKS3 1
#else
#error "未対応のボードです (Arduino Nesso N1 / M5StickS3)"
#endif

namespace board {

#if defined(BOARD_NESSO_N1)
constexpr const char* NAME = "Nesso N1";
// Grove ポート: GPIO5 = GROVE_IO_0, GPIO4 = GROVE_IO_1
constexpr int PIN_STRAIGHT = 4;
constexpr int PIN_PADDLE_DIT = 4;
constexpr int PIN_PADDLE_DAH = 5;
#elif defined(BOARD_STICKS3)
constexpr const char* NAME = "M5StickS3";
// Grove ポート (Port A): GPIO10 = 4 番ピン, GPIO9 = 3 番ピン
constexpr int PIN_STRAIGHT = 10;
constexpr int PIN_PADDLE_DIT = 10;
constexpr int PIN_PADDLE_DAH = 9;
#endif
// キー入力はアクティブ Low (INPUT_PULLUP, キー閉 = LOW)

}  // namespace board
