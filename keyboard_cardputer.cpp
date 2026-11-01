// keyboard_cardputer.cpp - Cardputer (original) keyboard: 4 x 14 = 56 key
// matrix
//
// Drive one of 8 rows LOW via the 74HC138 A0/A1/A2 = G8/G9/G11, and read 7
// columns G13/G15/G3/G4/G5/G6/G7 (pull-up, LOW when pressed). Coordinate
// mapping and key layout follow the M5Cardputer library (IOMatrix.cpp,
// Keyboard.h). Assumes readKey() is called from loop() every 20 ms; each call
// scans the whole matrix and returns keys at the moment they are pressed.
//
// Cardputer ADV (identified at startup by M5Unified, same build) uses the
// TCA8418 keyboard controller (I2C 0x34, internal I2C). Read the key event FIFO
// and convert row r / column c (event number = r*10 + c + 1, bit7 = pressed) to
// the same coordinates as the original (x = 2r + (c > 3), y = c % 4, same as
// M5Cardputer's TCA8418.cpp). Polled every 20 ms without using the interrupt.
#include "board.h"
#include "build_info.h"
BUILD_INFO_UNIT();
#include "hw.h"

#if defined(BOARD_CARDPUTER)
#include <Arduino.h>
#include <M5Unified.h>
#include <driver/gpio.h>

namespace hw {
namespace {

constexpr int PIN_SELECT[3] = {8, 9, 11};  // 74HC138 A0, A1, A2
constexpr int PIN_INPUT[7] = {13, 15, 3, 4,
                              5,  6,  7};  // Columns (pressed = LOW)
constexpr int ROWS = 4, COLS = 14;

// Special keys (used in the table)
constexpr int K_FN = 0x200, K_SHIFT = 0x201, K_CTRL = 0x202, K_OPT = 0x203,
              K_ALT = 0x204;

struct KeyDef {
    int normal, shifted, fn;
};

// [y][x]. Same order as M5Cardputer's _key_value_map
const KeyDef KEYMAP[ROWS][COLS] = {
    {{'`', '~', KEY_ESC},
     {'1', '!', KEY_NONE},
     {'2', '@', KEY_NONE},
     {'3', '#', KEY_NONE},
     {'4', '$', KEY_NONE},
     {'5', '%', KEY_NONE},
     {'6', '^', KEY_NONE},
     {'7', '&', KEY_NONE},
     {'8', '*', KEY_NONE},
     {'9', '(', KEY_NONE},
     {'0', ')', KEY_NONE},
     {'-', '_', KEY_NONE},
     {'=', '+', KEY_NONE},
     {KEY_BACKSPACE, KEY_BACKSPACE, KEY_BACKSPACE}},
    {{KEY_TAB, KEY_TAB, KEY_TAB},
     {'q', 'Q', KEY_NONE},
     {'w', 'W', KEY_NONE},
     {'e', 'E', KEY_NONE},
     {'r', 'R', KEY_NONE},
     {'t', 'T', KEY_NONE},
     {'y', 'Y', KEY_NONE},
     {'u', 'U', KEY_NONE},
     {'i', 'I', KEY_NONE},
     {'o', 'O', KEY_NONE},
     {'p', 'P', KEY_NONE},
     {'[', '{', KEY_NONE},
     {']', '}', KEY_NONE},
     {'\\', '|', KEY_NONE}},
    {{K_FN, K_FN, K_FN},
     {K_SHIFT, K_SHIFT, K_SHIFT},
     {'a', 'A', KEY_NONE},
     {'s', 'S', KEY_NONE},
     {'d', 'D', KEY_NONE},
     {'f', 'F', KEY_NONE},
     {'g', 'G', KEY_NONE},
     {'h', 'H', KEY_NONE},
     {'j', 'J', KEY_NONE},
     {'k', 'K', KEY_NONE},
     {'l', 'L', KEY_NONE},
     {';', ':', KEY_UP},
     {'\'', '"', KEY_NONE},
     {KEY_ENTER, KEY_ENTER, KEY_ENTER}},
    {{K_CTRL, K_CTRL, K_CTRL},
     {K_OPT, K_OPT, K_OPT},
     {K_ALT, K_ALT, K_ALT},
     {'z', 'Z', KEY_NONE},
     {'x', 'X', KEY_NONE},
     {'c', 'C', KEY_NONE},
     {'v', 'V', KEY_NONE},
     {'b', 'B', KEY_NONE},
     {'n', 'N', KEY_NONE},
     {'m', 'M', KEY_NONE},
     {',', '<', KEY_LEFT},
     {'.', '>', KEY_DOWN},
     {'/', '?', KEY_RIGHT},
     {' ', ' ', ' '}},
};

bool ready = false;
bool adv = false;  // Cardputer ADV (TCA8418)
bool prev[ROWS][COLS] = {};

// ---- ADV: TCA8418 ----
constexpr uint8_t TCA_ADDR = 0x34;
constexpr uint32_t TCA_FREQ = 400000;
constexpr uint8_t REG_CFG = 0x01, REG_INT_STAT = 0x02, REG_KEY_EVENT_A = 0x04,
                  REG_KP_GPIO_1 = 0x1D, REG_KP_GPIO_2 = 0x1E,
                  REG_KP_GPIO_3 = 0x1F;

void tcaWrite(uint8_t reg, uint8_t v) {
    M5.In_I2C.writeRegister8(TCA_ADDR, reg, v, TCA_FREQ);
}
uint8_t tcaRead(uint8_t reg) {
    return M5.In_I2C.readRegister8(TCA_ADDR, reg, TCA_FREQ);
}

void beginTca() {
    tcaWrite(REG_KP_GPIO_1, 0x7F);  // ROW0..6 as the key matrix
    tcaWrite(REG_KP_GPIO_2, 0xFF);  // COL0..7
    tcaWrite(REG_KP_GPIO_3, 0x00);
    for (int i = 0; i < 16 && tcaRead(REG_KEY_EVENT_A) != 0; i++) {
    }  // Discard pending events
    tcaWrite(REG_INT_STAT, 0x03);
}

constexpr int QUEUE = 16;
int queue[QUEUE];
int qHead = 0, qLen = 0;

void beginMatrix() {
    for (int p : PIN_SELECT) {
        gpio_reset_pin((gpio_num_t)p);
        pinMode(p, OUTPUT);
        digitalWrite(p, LOW);
    }
    for (int p : PIN_INPUT) {
        gpio_reset_pin((gpio_num_t)p);
        pinMode(p, INPUT_PULLUP);
    }
    ready = true;
}

// Read the pressed state of all keys
void scan(bool now[ROWS][COLS]) {
    for (int y = 0; y < ROWS; y++)
        for (int x = 0; x < COLS; x++) now[y][x] = false;
    for (int i = 0; i < 8; i++) {
        for (int b = 0; b < 3; b++) digitalWrite(PIN_SELECT[b], (i >> b) & 1);
        delayMicroseconds(2);
        for (int j = 0; j < 7; j++) {
            if (digitalRead(PIN_INPUT[j]) != LOW) continue;
            int x = i > 3 ? 2 * j : 2 * j + 1;
            int y = 3 - (i & 3);
            now[y][x] = true;
        }
    }
}

void push(int k) {
    if (qLen < QUEUE) queue[(qHead + qLen++) % QUEUE] = k;
}

// Whether a modifier key (fn / shift) is currently pressed
bool modifierDown(int which) {
    for (int y = 0; y < ROWS; y++)
        for (int x = 0; x < COLS; x++)
            if (prev[y][x] && KEYMAP[y][x].normal == which) return true;
    return false;
}

// A key was pressed: push the value according to the modifier state to the
// queue (modifiers alone produce nothing)
void pressed(int y, int x) {
    const KeyDef& d = KEYMAP[y][x];
    if (d.normal >= K_FN) return;
    int k = modifierDown(K_FN)      ? d.fn
            : modifierDown(K_SHIFT) ? d.shifted
                                    : d.normal;
    if (k != KEY_NONE) push(k);
}

// ADV: process FIFO events in order (emit a key per press event so short
// presses are not missed)
void readTca() {
    for (int i = 0; i < 16; i++) {
        uint8_t ev = tcaRead(REG_KEY_EVENT_A);
        if (ev == 0) break;
        int code = (ev & 0x7F) - 1;
        int r = code / 10, c = code % 10;
        if (code < 0 || r > 6 || c > 7)
            continue;  // Events outside the key matrix (GPI)
        int x = 2 * r + (c > 3 ? 1 : 0), y = c % 4;
        bool down = (ev & 0x80) != 0;
        if (down && !prev[y][x]) pressed(y, x);
        prev[y][x] = down;
    }
    tcaWrite(REG_INT_STAT, 0x03);
}

}  // namespace

bool hasKeyboard() { return true; }

int readKey() {
    if (!ready) {
        adv = M5.getBoard() == m5::board_t::board_M5CardputerADV;
        if (adv)
            beginTca();
        else
            beginMatrix();  // On ADV, G8/G9 are I2C, so the matrix must not be
                            // scanned
        ready = true;
    }
    if (qLen == 0 && adv) {
        readTca();
    } else if (qLen == 0) {
        // Original: scan all keys and emit keys at the moment they are pressed
        // (modifiers are checked in the current state, not the pre-scan state)
        bool now[ROWS][COLS];
        scan(now);
        bool was[ROWS][COLS];
        for (int y = 0; y < ROWS; y++)
            for (int x = 0; x < COLS; x++) {
                was[y][x] = prev[y][x];
                prev[y][x] = now[y][x];
            }
        for (int y = 0; y < ROWS; y++)
            for (int x = 0; x < COLS; x++)
                if (now[y][x] && !was[y][x]) pressed(y, x);
    }
    if (qLen == 0) return KEY_NONE;
    int k = queue[qHead];
    qHead = (qHead + 1) % QUEUE;
    qLen--;
    return k;
}

}  // namespace hw

#else  // Boards without a keyboard

namespace hw {
bool hasKeyboard() { return false; }
int readKey() { return KEY_NONE; }
}  // namespace hw

#endif
