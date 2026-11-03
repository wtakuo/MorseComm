// power.h - Power source detection and deep sleep (Nesso N1)
// Uses I2C (IO expander), so call only from the loop() side.
#pragma once
#include <stdint.h>

namespace power {

// Whether powered from USB (VIN)
bool usbPowered();
// Whether the wake-up cause was a button (call right after boot)
bool wokeFromSleep();
// State of the SYS_IRQ interrupt line etc. (for diagnostics)
void printStatus();
// Turn off the screen and enter deep sleep, woken by a button (BtnA/BtnB). Does
// not return (returns false only if it could not enter because the interrupt
// line was already Low)
bool enterDeepSleep();

}  // namespace power
