// shared.h - data shared between the keying task (cwTask) and loop()
// (UI/serial) Always read/write the shared structures between
// lockShared()/unlockShared().
#pragma once
#include <stdint.h>
#include "jitter.h"
#include "keyer.h"
#include "net.h"

// Persistent settings (Preferences)
struct Settings {
    uint8_t mode = (uint8_t)cw::KeyMode::IambicB;
    uint8_t wpm = cfg::WPM_DEFAULT;
    uint8_t sideOn = 1;  // sidetone ON/OFF
    uint16_t sideHz = cfg::SIDE_HZ_DEFAULT;
    uint16_t rxHz = cfg::RX_HZ_DEFAULT;
    uint8_t volume = cfg::VOLUME_DEFAULT;  // speaker volume
    uint8_t output =
        cfg::OUTPUT_SPEAKER;  // audio output (meaningful only on Cardputer ADV)
    uint8_t hpVolume = cfg::HP_VOLUME_DEFAULT;  // headphone volume
    uint8_t swap = 0;                           // swap paddle DIT/DAH
    uint16_t jitterMs = cfg::JITTER_DELAY_MS;
    uint8_t channel = cfg::CH_DEFAULT;  // communication channel
    uint8_t radio =
        cfg::RADIO_ESPNOW;  // radio type (a change takes effect after restart)
};

// Runtime control (not saved): test mode, loss injection
struct Control {
    bool testOn = false;
    char testText[cw::TextKeyer::MAX_TEXT] = "PARIS";
    uint8_t lossPct = 0;  // skip transmissions with a given probability
    bool dropUp = false;  // skip packets with state==UP
    bool termOn = false;  // terminal mode: send characters typed on the serial
                          // port (g_typeBuf)
};

// cwTask -> UI state snapshot
struct Snapshot {
    cw::RxStats rx;
    net::Stats net;
    int32_t offset = 0;
    int8_t rssi = 0;
    bool linkUp = false;  // at least one station has a live link
    int stations = 0;     // number of stations with a live link
    bool localDown = false;
    bool remoteDown = false;
    int localWpm = 0;   // estimated WPM from the local decoder
    int remoteWpm = 0;  // estimated WPM from the decoder of the most recently
                        // received station
    uint32_t txPackets = 0;
    uint32_t injectedDrops = 0;  // packets not sent due to loss injection
    uint32_t lastActivityMs =
        0;  // time of the last key transition of the local or a remote station
    uint32_t nowMs = 0;
};

// cwTask -> UI output (decoded characters, logs)
struct OutItem {
    enum Kind : uint8_t { CharTx, CharRx, LogLocal, LogRemote } kind;
    char c;
    uint8_t state;
    uint8_t eventKind;  // cw::EventKind (LogRemote)
    uint32_t t;
    uint32_t dur;  // length of the previous state
    uint8_t peer =
        0;  // CharRx / LogRemote: source station (slot number in PeerTable)
};

void lockShared();
void unlockShared();

extern Settings g_settings;
extern uint32_t g_settingsVer;  // incremented on every change (cwTask detects
                                // it and applies the change)
extern Control g_control;
extern uint32_t g_controlVer;
extern Snapshot g_snapshot;
extern bool g_logOn;
extern cw::TypeBuffer g_typeBuf;  // characters waiting to be sent in terminal
                                  // mode (protected by lockShared)
