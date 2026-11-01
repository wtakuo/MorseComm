// board.h - selection of the target board and the key input pins for each board
// The board is identified by the macro defined by the FQBN:
//   Arduino Nesso N1 : esp32:esp32:arduino_nesso_n1        ->
//   ARDUINO_ARDUINO_NESSO_N1 M5StickS3        : m5stack:esp32:m5stack_sticks3
//   -> ARDUINO_M5STACK_STICKS3 M5StickC Plus2   :
//   m5stack:esp32:m5stack_stickc_plus2  -> ARDUINO_M5STACK_STICKC_PLUS2
//   M5StickC Plus    : m5stack:esp32:m5stack_stickc_plus   ->
//   ARDUINO_M5STACK_STICKC_PLUS M5Stack Cardputer:
//   m5stack:esp32:m5stack_cardputer     -> ARDUINO_M5STACK_CARDPUTER (original
//   model. v1.1 / ADV not supported)
#pragma once

#if defined(ARDUINO_ARDUINO_NESSO_N1)
#define BOARD_NESSO_N1 1
#elif defined(ARDUINO_M5STACK_STICKS3)
#define BOARD_STICKS3 1
// Display, buttons and battery handled with M5Unified (hw_m5stick.cpp)
#define BOARD_M5UNIFIED 1
#elif defined(ARDUINO_M5STACK_STICKC_PLUS2)
#define BOARD_STICKC_PLUS2 1
#define BOARD_M5UNIFIED 1
#elif defined(ARDUINO_M5STACK_STICKC_PLUS)
#define BOARD_STICKC_PLUS 1
#define BOARD_M5UNIFIED 1
#elif defined(ARDUINO_M5STACK_CARDPUTER)
#define BOARD_CARDPUTER 1
#define BOARD_M5UNIFIED 1
// Text can be typed on the keyboard and sent (keyboard_cardputer.cpp)
#define BOARD_HAS_KEYBOARD 1
#else
// Supported: Nesso N1 / M5StickS3 / M5StickC Plus2 / M5StickC Plus / Cardputer
#error "Unsupported board (see board.h)"
#endif

namespace board {

#if defined(BOARD_NESSO_N1)
constexpr const char* NAME = "Nesso N1";
// Grove port: GPIO5 = GROVE_IO_0, GPIO4 = GROVE_IO_1
constexpr int PIN_STRAIGHT = 4;
constexpr int PIN_PADDLE_DIT = 4;
constexpr int PIN_PADDLE_DAH = 5;
#elif defined(BOARD_STICKS3)
constexpr const char* NAME = "M5StickS3";
// Grove port (Port A): GPIO10 = pin 4, GPIO9 = pin 3
constexpr int PIN_STRAIGHT = 10;
constexpr int PIN_PADDLE_DIT = 10;
constexpr int PIN_PADDLE_DAH = 9;
#elif defined(BOARD_STICKC_PLUS2)
constexpr const char* NAME = "M5StickC Plus2";
// Grove port: G33 (white, SCL in the board definition) = straight / DIT, G32
// (yellow, SDA) = DAH (confirmed by the user)
constexpr int PIN_STRAIGHT = 33;
constexpr int PIN_PADDLE_DIT = 33;
constexpr int PIN_PADDLE_DAH = 32;
#elif defined(BOARD_CARDPUTER)
constexpr const char* NAME = "Cardputer";
// Grove port: G1 = straight / DIT, G2 = DAH (specified by the user)
constexpr int PIN_STRAIGHT = 1;
constexpr int PIN_PADDLE_DIT = 1;
constexpr int PIN_PADDLE_DAH = 2;
#elif defined(BOARD_STICKC_PLUS)
constexpr const char* NAME = "M5StickC Plus";
// Grove port: same as Plus2 (G33 = straight / DIT, G32 = DAH. Confirmed by the
// user)
constexpr int PIN_STRAIGHT = 33;
constexpr int PIN_PADDLE_DIT = 33;
constexpr int PIN_PADDLE_DAH = 32;
#endif
// Key input is active Low (INPUT_PULLUP, key closed = LOW)

}  // namespace board
