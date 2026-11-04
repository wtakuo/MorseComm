// ui.h - Screen display and button/touch input (240x135 landscape screen; board
// differences are absorbed by hw.h) Uses I2C, so call only from the loop()
// side.
#pragma once
#include <stdint.h>
#include "shared.h"
#include "hw.h"

namespace ui {

// Settings screen items. The order is the display order on screen (specified by
// the user, 2026-11-01) Output = audio output (speaker / headphones; shown only
// on Cardputer ADV, which has a headphone jack) Defaults = restore all settings
// to defaults (press twice to execute)
enum class Item : uint8_t {
    Radio,
    Mode,
    Swap,
    Wpm,
    SideOn,
    Output,
    Volume,
    SideHz,
    RxHz,
    Defaults,
    Count
};

// Display state of the DEFAULTS item: idle / pressed once (press again to
// execute) / executed
enum class DefaultsState : uint8_t { Idle, Armed, Done };
// Number of items shown on this board, and conversion between display index <->
// item. Use stepItem to move between items
int itemCount();
Item itemAt(int index);
int itemIndex(Item it);
Item stepItem(Item it, int dir);

// Key1 = BtnA (front), Key2 = BtnB (side)
enum class Button : uint8_t { None, Key1Short, Key1Long, Key2Short, Key2Long };

struct TouchEvent {
    enum Kind : uint8_t { None, Plus, Minus, Row } kind = None;
    int row = 0;          // When kind == Row, the touched settings item
    bool repeat = false;  // Repeated event from holding down +/-
    int16_t x = 0, y = 0;
};

using Battery = hw::Battery;

void begin();
// Turn off the screen (before sleep)
void sleepDisplay();
// Splash screen at startup: "MorseComm" in the center, with the version and
// the author below it
void drawSplash(const char* version);
void addChar(bool rx, char c);
void clearText();

Button pollButtons(uint32_t nowMs);
// Touch on the settings screen (returns an event only at the moment of
// pressing)
TouchEvent pollTouch(bool settingsScreen);
Battery readBattery();
bool touchAvailable();

// When sleepInSec >= 0, show the remaining seconds until sleep. When
// showChannel, show the channel number large pending: characters typed but not
// yet sent (shown in gray on the TX line)
void drawMain(const Settings& s, const Control& c, const Snapshot& snap,
              const Battery& bat, int sleepInSec, bool showChannel,
              const char* pending = nullptr);
// editing: on boards without touch, the selected item's value is being changed
// (buttons act as +/-)
void drawSettings(const Settings& s, Item selected, bool editing,
                  const Battery& bat,
                  DefaultsState defaults = DefaultsState::Idle);

}  // namespace ui
