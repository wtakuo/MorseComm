// engine.h - keying task: runs key input, keyer, transmit, receive playback,
// tone and decoding every 1ms
#pragma once
#include "shared.h"

namespace engine {

// Call after setting g_settings / g_control
bool begin();
// Fetch decoded characters and logs (called from loop())
bool popOut(OutItem& item);

}  // namespace engine
