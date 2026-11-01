// hw.h - Hardware abstraction around the display (screen, buttons, touch,
// battery) Implemented per board: hw_nesso.cpp / hw_sticks3.cpp. Uses I2C, so
// call only from the loop() side.
#pragma once
#include <stdint.h>
#include <M5GFX.h>

namespace hw {

struct Battery {
    bool valid = false;
    int percent = 0;
    bool charging = false;
};

// Board initialization (sets the screen to 240x135 landscape)
void begin();
lgfx::LGFX_Device& display();
// Physical buttons: 0 = front (Nesso: KEY1 / StickS3: BtnA), 1 = side (Nesso:
// KEY2 / StickS3: BtnB). true while pressed
bool button(int i);
bool hasTouch();
// Returns screen coordinates while touched
bool touch(int16_t& x, int16_t& y);
Battery battery();
// Turn off the screen before sleep
void displaySleep();
// Mutual exclusion for the SPI bus shared with the screen (Nesso N1: screen and
// LoRa use the same SPI; needed because they are used from different tasks).
// Both the drawing side and the LoRa side wrap their SPI use with this. Does
// nothing on StickS3
void lockBus();
void unlockBus();

// Keyboard (Cardputer). Returns keys one at a time at the moment they are
// pressed (KEY_NONE if none). Implemented in keyboard_cardputer.cpp
enum : int {
    KEY_NONE = -1,
    KEY_ENTER = '\r',
    KEY_BACKSPACE = 0x08,
    KEY_TAB = '\t',
    KEY_ESC = 0x1B,
    KEY_UP = 0x100,  // fn + ;
    KEY_DOWN,        // fn + .
    KEY_LEFT,        // fn + ,
    KEY_RIGHT,       // fn + /
};
bool hasKeyboard();
int readKey();
// Has a headphone jack (Cardputer ADV). Plugging in stops the speaker in
// hardware, but the plug-in cannot be detected
bool hasPhoneJack();

}  // namespace hw
