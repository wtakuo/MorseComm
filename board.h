// board.h — ビルド対象のボードの選択と、ボードごとのキー入力ピン
// ボードは FQBN が定義するマクロで判別する:
//   Arduino Nesso N1 : esp32:esp32:arduino_nesso_n1        → ARDUINO_ARDUINO_NESSO_N1
//   M5StickS3        : m5stack:esp32:m5stack_sticks3       → ARDUINO_M5STACK_STICKS3
//   M5StickC Plus2   : m5stack:esp32:m5stack_stickc_plus2  → ARDUINO_M5STACK_STICKC_PLUS2
//   M5StickC Plus    : m5stack:esp32:m5stack_stickc_plus   → ARDUINO_M5STACK_STICKC_PLUS
#pragma once

#if defined(ARDUINO_ARDUINO_NESSO_N1)
#define BOARD_NESSO_N1 1
#elif defined(ARDUINO_M5STACK_STICKS3)
#define BOARD_STICKS3 1
#define BOARD_M5UNIFIED 1  // 画面・ボタン・電池を M5Unified で扱う (hw_m5stick.cpp)
#elif defined(ARDUINO_M5STACK_STICKC_PLUS2)
#define BOARD_STICKC_PLUS2 1
#define BOARD_M5UNIFIED 1
#elif defined(ARDUINO_M5STACK_STICKC_PLUS)
#define BOARD_STICKC_PLUS 1
#define BOARD_M5UNIFIED 1
#else
#error "Unsupported board (Arduino Nesso N1 / M5StickS3 / M5StickC Plus2 / M5StickC Plus)"
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
#elif defined(BOARD_STICKC_PLUS2)
constexpr const char* NAME = "M5StickC Plus2";
// Grove ポート: G33 (白, ボード定義の SCL) = ストレート / DIT, G32 (黄, SDA) = DAH (ユーザー確認済み)
constexpr int PIN_STRAIGHT = 33;
constexpr int PIN_PADDLE_DIT = 33;
constexpr int PIN_PADDLE_DAH = 32;
#elif defined(BOARD_STICKC_PLUS)
constexpr const char* NAME = "M5StickC Plus";
// Grove ポート: Plus2 と同じ (G33 = ストレート / DIT, G32 = DAH。ユーザー確認済み)
constexpr int PIN_STRAIGHT = 33;
constexpr int PIN_PADDLE_DIT = 33;
constexpr int PIN_PADDLE_DAH = 32;
#endif
// キー入力はアクティブ Low (INPUT_PULLUP, キー閉 = LOW)

}  // namespace board
