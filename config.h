// config.h - default parameters (common to all boards)
// Keep this Arduino-independent (it is also included by the host tests).
#pragma once
#include <stdint.h>

namespace cfg {

// Board-specific constants such as pins are in board.h and the per-board .cpp
// files

// ---- Key input ----
constexpr uint32_t DEBOUNCE_MS = 4;  // lockout-style debounce

// ---- Keyer defaults ----
constexpr int WPM_DEFAULT = 20;
constexpr int WPM_MIN = 5;
constexpr int WPM_MAX = 50;

// ---- Tone ----
constexpr int SIDE_HZ_DEFAULT = 700;  // sidetone (local monitor)
constexpr int RX_HZ_DEFAULT = 600;    // receive tone
constexpr int TONE_HZ_MIN = 300;
constexpr int TONE_HZ_MAX = 1500;
constexpr int TONE_STEP_HZ = 50;
constexpr int VOLUME_DEFAULT = 3;
// Audio output (Cardputer ADV, which has a headphone jack). Volume is kept
// separately per output
constexpr uint8_t OUTPUT_SPEAKER = 0;
constexpr uint8_t OUTPUT_PHONES = 1;
constexpr int HP_VOLUME_DEFAULT = 2;
constexpr uint32_t TONE_PREVIEW_MS =
    150;  // length of the preview tone played when a setting changes

// ---- Display ----
constexpr uint32_t SPLASH_MS =
    3000;  // how long the startup splash screen (MorseComm and build date/time)
           // is shown

// ---- Settings screen ----
constexpr uint32_t DEFAULTS_CONFIRM_MS =
    3000;  // DEFAULTS: pressing again within this time after the first press
           // restores the defaults

// ---- Power ----
constexpr uint32_t SLEEP_TIMEOUT_MS =
    180000;  // on battery, sleep after this long with no operation and no
             // reception
constexpr uint32_t SLEEP_WARN_MS =
    10000;  // how long before sleep the warning is shown

// ---- Transmit schedule (sec. 4.2) ----
constexpr uint32_t TX_INTERVAL_ACTIVE_MS =
    20;  // transmit interval while DOWN and for a while after UP
constexpr uint32_t TX_ACTIVE_HOLD_MS =
    500;  // how long the active interval continues after UP
constexpr uint32_t TX_INTERVAL_IDLE_MS = 500;  // idle heartbeat
constexpr int HIST_N = 8;     // number of transition history entries per packet
constexpr int HIST_MAX = 16;  // upper limit of the history count (maximum
                              // allowed by the packet format)

// ---- Receive (sec. 4.5) ----
constexpr uint32_t JITTER_DELAY_MS = 150;  // fixed jitter buffer delay
constexpr uint32_t WATCHDOG_MS = 100;  // time from loss of information on the
                                       // playback timeline until forced UP
constexpr uint32_t OFFSET_BUCKET_MS =
    1000;  // bucket width for estimating the minimum clock offset
constexpr int OFFSET_BUCKETS =
    4;  // number of recent buckets whose minimum is used
constexpr uint32_t LINK_TIMEOUT_MS =
    3000;  // if nothing is received for longer than this, reinitialize on the
           // next packet
constexpr int32_t SENDER_RESTART_BACKSTEP_MS =
    2000;  // if txMs goes back by this much or more, assume the sender
           // restarted

// ---- Radio type (switched in settings; switching causes a restart) ----
constexpr uint8_t RADIO_ESPNOW = 0;
constexpr uint8_t RADIO_LORA = 1;  // Nesso N1 only (SX1262)
constexpr uint8_t RADIO_OFF =
    2;  // no RF emission (for solo sending practice; does not receive either)

// ---- ESP-NOW ----
constexpr uint8_t ESPNOW_CHANNEL =
    1;  // Wi-Fi channel (fixed, common to all stations)

// ---- LoRa (Japan 920MHz band specified low-power radio station, 20mW or less.
// ARIB STD-T108 / Radio Equipment Regulations) ---- Uses the carrier-sense >=
// 5ms category: one transmission <= 4 s, pause after transmission >= 50ms, no
// limit on the total per hour. The 500kHz bandwidth is a bundle of 3 unit
// channels (200kHz). Center frequency of communication channel n = BASE + STEP
// * (n - 1) (n=1: bundle of 920.6/920.8/921.0 MHz -> center 920.8MHz, n=8:
// 925.0MHz)
constexpr float LORA_FREQ_BASE_MHZ = 920.8f;
constexpr float LORA_FREQ_STEP_MHZ = 0.6f;
constexpr float LORA_BW_KHZ = 500.0f;
constexpr uint8_t LORA_SF = 5;
constexpr uint8_t LORA_CR = 5;            // 4/5
constexpr uint8_t LORA_SYNC_WORD = 0x12;  // private
constexpr int8_t LORA_POWER_DBM = 13;     // 20mW (upper limit of antenna power)
constexpr uint16_t LORA_PREAMBLE = 8;
constexpr float LORA_TCXO_V = 3.0f;
constexpr uint32_t LORA_CS_US =
    5200;  // carrier sense time (regulation: 5ms or more)
constexpr uint32_t LORA_PAUSE_MS =
    52;  // pause after transmission (regulation: 50ms or more)
constexpr int LORA_CS_LEVEL_DBM =
    -80;  // carrier sense level (at the feed line input point)
// Gain of the receive-path LNA (SGM13005L4, about 21dB). The SX1262 RSSI reads
// high by this amount, so it is subtracted. Set slightly smaller than the
// datasheet value so that carrier sense does not become too lenient
constexpr int LORA_LNA_GAIN_DB = 20;
constexpr uint32_t LORA_TX_TIMEOUT_MS =
    100;  // recovery when the TX-done interrupt does not arrive
constexpr uint32_t LORA_TX_INTERVAL_IDLE_MS =
    1000;  // idle heartbeat (longer, to leave the channel free)
constexpr uint32_t LORA_JITTER_DELAY_MS =
    250;  // the transmit interval is long (about 60ms), so the jitter delay is
          // also longer

// ---- Communication channel (like a radio's "channel". Carried in the packet;
// only stations with the same value communicate) ----
constexpr uint8_t CH_MIN = 1;
constexpr uint8_t CH_MAX = 8;
constexpr uint8_t CH_DEFAULT = 1;
constexpr uint32_t CH_OVERLAY_MS =
    1200;  // how long the channel is shown in large text when switched
constexpr int MAX_PEERS =
    6;  // number of stations whose reception can be tracked simultaneously

// ---- Decoder ----
constexpr uint32_t DEC_DIT_INIT_MS = 60;  // initial dit length (20 WPM)
constexpr uint32_t DEC_DIT_MIN_MS = 20;   // 60 WPM
constexpr uint32_t DEC_DIT_MAX_MS = 240;  // 5 WPM

}  // namespace cfg
