// MorseComm - CW (Morse) keying transmission over ESP-NOW / LoRa (Arduino Nesso
// N1 / M5StickS3 / M5StickC Plus2)
//
// Structure:
//   cwTask (engine.cpp, 1ms period, high priority): key input, keyer, transmit,
//   receive playback, tone, decode loop()  (this file, low priority) : display,
//   buttons, serial commands, saving settings
// See CLAUDE.md / README.md for details.
#include <Arduino.h>
#include "version.h"
#include <Preferences.h>
#include "board.h"
#include "config.h"
#include "shared.h"
#include "engine.h"
#include "ui.h"
#include "tone_out.h"
#include "power.h"

// ---- Shared data (shared.h) ----
static portMUX_TYPE sharedMux = portMUX_INITIALIZER_UNLOCKED;
void lockShared() { portENTER_CRITICAL(&sharedMux); }
void unlockShared() { portEXIT_CRITICAL(&sharedMux); }
Settings g_settings;
uint32_t g_settingsVer = 0;
Control g_control;
uint32_t g_controlVer = 0;
Snapshot g_snapshot;
bool g_logOn = false;
cw::TypeBuffer g_typeBuf;

namespace {

constexpr uint32_t UI_INTERVAL_MS = 100;
constexpr uint32_t BUTTON_INTERVAL_MS = 20;
constexpr uint32_t SAVE_DELAY_MS =
    2000;  // save this long after the last change
constexpr uint32_t STATS_INTERVAL_MS =
    5000;  // stats output interval when log is on
constexpr uint32_t BATTERY_INTERVAL_MS = 5000;
constexpr uint32_t SETTINGS_TIMEOUT_MS =
    30000;  // leave the settings screen after this much inactivity
// USB serial TX buffer. With the default (256), long output (help etc.) gets
// cut off (the TX timeout is 0 so that we never block without USB, so whatever
// does not fit is discarded)
constexpr size_t SERIAL_TX_BUFFER = 4096;

Preferences prefs;
ui::Item selected = ui::Item::Radio;
bool settingsScreen = false;
bool editing =
    false;  // boards without touch: editing the value of the selected item
uint32_t lastSettingsOpMs = 0;
ui::Battery battery;
bool saveDirty = false;
ui::DefaultsState defaultsState =
    ui::DefaultsState::Idle;  // DEFAULTS item on the settings screen
uint32_t defaultsArmedMs = 0;

// Whether the DEFAULTS confirmation has timed out. loop()'s now may be earlier
// than the press time, so compare signed (unsigned, now - defaultsArmedMs
// becomes huge and the press is cancelled right after it is made)
bool defaultsExpired(uint32_t now) {
    return (int32_t)(now - defaultsArmedMs) >=
           (int32_t)cfg::DEFAULTS_CONFIRM_MS;
}
uint32_t lastChangeMs = 0, lastUiMs = 0, lastBtnMs = 0, lastStatsMs = 0,
         lastBatteryMs = 0;
uint32_t chOverlayUntil = 0;  // show the channel number large until this time
uint32_t splashUntil =
    0;  // keep the splash screen until this time (main screen is not drawn)
uint32_t lastInputMs =
    0;  // button / touch / serial input, or while on USB power
bool usbPower = true;
char lineBuf[96];
int lineLen = 0;
// Command prompt. If a log etc. interrupts, break the line; once output is
// quiet, reprint the partially typed line
constexpr const char* PROMPT = "morse> ";
constexpr uint32_t PROMPT_QUIET_MS = 300;
bool promptShown = false;
bool lastWasCr = false;  // treat CR LF as a single newline
uint32_t lastAsyncMs =
    0;  // time of the last asynchronous output (log, stats, etc.)
// Terminal mode (send characters typed on the serial port and show received
// characters)
bool termMode = false;
char termDir = 0;  // last direction shown: '>' = typed characters, '<' =
                   // received characters
bool termLastCr =
    false;  // previous char was CR (treat CR LF as a single newline)

// ---- Console (prompt, echo) ----
void showPrompt() {
    Serial.print(PROMPT);
    Serial.write((const uint8_t*)lineBuf, lineLen);
    promptShown = true;
}

// Call before asynchronous output (log, stats, save notices, etc.): ends the
// prompt line with a newline
void asyncOut() {
    if (promptShown) Serial.print("\r\n");
    promptShown = false;
    lastAsyncMs = millis();
}

// ---- Settings ----
// Radio types selectable on this board (the settings screen cycles in this
// order)
bool radioSelectable(uint8_t r) {
    return r == cfg::RADIO_ESPNOW || r == cfg::RADIO_OFF ||
           (r == cfg::RADIO_LORA && net::loraAvailable());
}

uint8_t nextRadio(uint8_t r, int dir) {
    static const uint8_t ORDER[] = {cfg::RADIO_ESPNOW, cfg::RADIO_LORA,
                                    cfg::RADIO_OFF};
    const int n = sizeof(ORDER);
    int i = 0;
    while (i < n && ORDER[i] != r) i++;
    do {
        i = (i + n + dir) % n;
    } while (!radioSelectable(ORDER[i]));
    return ORDER[i];
}
Settings clampSettings(Settings s) {
    if (s.mode >= cw::KEY_MODE_COUNT) s.mode = (uint8_t)cw::KeyMode::IambicB;
    if (s.wpm < cfg::WPM_MIN) s.wpm = cfg::WPM_MIN;
    if (s.wpm > cfg::WPM_MAX) s.wpm = cfg::WPM_MAX;
    auto clampHz = [](uint16_t& hz) {
        if (hz < cfg::TONE_HZ_MIN) hz = cfg::TONE_HZ_MIN;
        if (hz > cfg::TONE_HZ_MAX) hz = cfg::TONE_HZ_MAX;
    };
    clampHz(s.sideHz);
    clampHz(s.rxHz);
    s.sideOn = s.sideOn ? 1 : 0;
    if (s.volume > ToneOut::VOLUME_MAX) s.volume = ToneOut::VOLUME_MAX;
    if (s.hpVolume > ToneOut::VOLUME_MAX) s.hpVolume = ToneOut::VOLUME_MAX;
    if (s.output != cfg::OUTPUT_PHONES || !hw::hasPhoneJack())
        s.output = cfg::OUTPUT_SPEAKER;
    if (s.jitterMs > 1000) s.jitterMs = 1000;
    if (s.channel < cfg::CH_MIN || s.channel > cfg::CH_MAX)
        s.channel = cfg::CH_DEFAULT;
    s.swap = s.swap ? 1 : 0;
    if (!radioSelectable(s.radio)) s.radio = cfg::RADIO_ESPNOW;
    return s;
}

// Jitter delay is saved per radio type (LoRa has a longer TX interval, so its
// default is also larger)
const char* jitterKey(uint8_t radio) {
    return radio == cfg::RADIO_LORA ? "ljitter" : "jitter";
}

uint16_t storedJitter(uint8_t radio) {
    prefs.begin("morse", true);
    uint16_t v = prefs.getUShort(
        jitterKey(radio), radio == cfg::RADIO_LORA ? cfg::LORA_JITTER_DELAY_MS
                                                   : cfg::JITTER_DELAY_MS);
    prefs.end();
    return v;
}

const char* radioName(uint8_t radio) {
    return radio == cfg::RADIO_LORA  ? "lora"
           : radio == cfg::RADIO_OFF ? "off"
                                     : "espnow";
}

// Stored message (cfg::MSG_DEFAULT unless set with "msg"). Used only by loop()
char g_message[cfg::MSG_MAX + 1];

// Normalize a message: uppercase, collapse runs of spaces, trim. Returns false
// if it contains a character without a Morse code (or is empty / too long)
bool normalizeMessage(const char* in, char* out) {
    int n = 0;
    for (const char* p = in; *p; p++) {
        char ch = *p;
        if (ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
        if (ch == ' ' || ch == '\t') {
            if (n > 0 && out[n - 1] != ' ') {
                if (n >= cfg::MSG_MAX) return false;
                out[n++] = ' ';
            }
            continue;
        }
        if (ch != '[' && ch != ']' && !cw::morseEncode(ch)) return false;
        if (n >= cfg::MSG_MAX) return false;
        out[n++] = ch;
    }
    while (n > 0 && out[n - 1] == ' ') n--;
    out[n] = 0;
    return n > 0;
}

void loadMessage() {
    prefs.begin("morse", true);
    String m = prefs.getString("msg", cfg::MSG_DEFAULT);
    prefs.end();
    if (!normalizeMessage(m.c_str(), g_message))
        normalizeMessage(cfg::MSG_DEFAULT, g_message);
}

// Queue the stored message once (after anything still waiting to be sent,
// separated by a word gap). Ignored if it does not fit in the buffer
void sendMessage() {
    int len = strlen(g_message);
    lockShared();
    bool gap = g_typeBuf.size() > 0 && g_typeBuf.back() != ' ';
    if (g_typeBuf.space() >= len + (gap ? 1 : 0)) {
        if (gap) g_typeBuf.push(' ');
        for (int i = 0; i < len; i++) g_typeBuf.push(g_message[i]);
    }
    unlockShared();
}

void loadSettings() {
    loadMessage();
    Settings s;
    prefs.begin("morse", true);
    s.mode = prefs.getUChar("mode", s.mode);
    s.wpm = prefs.getUChar("wpm", s.wpm);
    s.sideOn = prefs.getUChar("sideon", s.sideOn);
    s.sideHz = prefs.getUShort("sidehz", s.sideHz);
    s.rxHz = prefs.getUShort("rxhz", s.rxHz);
    s.volume = prefs.getUChar("vol", s.volume);
    s.hpVolume = prefs.getUChar("hpvol", s.hpVolume);
    s.output = prefs.getUChar("out", s.output);
    s.swap = prefs.getUChar("swap", s.swap);
    s.channel = prefs.getUChar("ch", s.channel);
    s.radio = prefs.getUChar("radio", s.radio);
    prefs.end();
    s = clampSettings(s);
    s.jitterMs = storedJitter(s.radio);
    g_settings = clampSettings(s);
    g_settingsVer++;
}

void saveSettings() {
    lockShared();
    Settings s = g_settings;
    unlockShared();
    prefs.begin("morse", false);
    prefs.putUChar("mode", s.mode);
    prefs.putUChar("wpm", s.wpm);
    prefs.putUChar("sideon", s.sideOn);
    prefs.putUShort("sidehz", s.sideHz);
    prefs.putUShort("rxhz", s.rxHz);
    prefs.putUChar("vol", s.volume);
    prefs.putUChar("hpvol", s.hpVolume);
    prefs.putUChar("out", s.output);
    prefs.putUChar("swap", s.swap);
    prefs.putUShort(jitterKey(s.radio), s.jitterMs);
    prefs.putUChar("ch", s.channel);
    prefs.putUChar("radio", s.radio);
    prefs.end();
    saveDirty = false;
    if (!termMode) {
        asyncOut();
        Serial.print("settings saved\r\n");
    }
}

template <typename F>
void editSettings(F f) {
    lockShared();
    Settings s = g_settings;
    f(s);
    g_settings = clampSettings(s);
    g_settingsVer++;
    unlockShared();
    saveDirty = true;
    lastChangeMs = millis();
}

template <typename F>
void editControl(F f) {
    lockShared();
    f(g_control);
    g_controlVer++;
    unlockShared();
}

void printSettings() {
    lockShared();
    Settings s = g_settings;
    Control c = g_control;
    unlockShared();
    Serial.printf(
        "radio=%s ch=%u mode=%s wpm=%u side=%s,%uHz rx=%uHz vol=%u swap=%u "
        "jitter=%ums | test=%s \"%s\" loss=%u%% dropup=%u "
        "log=%u\r\n",
        radioName(s.radio), s.channel, cw::keyModeName((cw::KeyMode)s.mode),
        s.wpm, s.sideOn ? "on" : "off", s.sideHz, s.rxHz, s.volume, s.swap,
        s.jitterMs, c.testOn ? "on" : "off", c.testText, c.lossPct, c.dropUp,
        g_logOn);
    if (hw::hasPhoneJack())
        Serial.printf("output=%s speaker vol=%u phones vol=%u\r\n",
                      s.output == cfg::OUTPUT_PHONES ? "phones" : "speaker",
                      s.volume, s.hpVolume);
}

void printStats() {
    asyncOut();
    lockShared();
    Snapshot sn = g_snapshot;
    unlockShared();
    uint32_t total = sn.rx.packets + sn.rx.lost;
    Serial.printf(
        "STATS rx=%u lost=%u (%.1f%%) stale=%u resync=%u late=%u wd=%u "
        "reassert=%u restart=%u rssi=%d offset=%ld "
        "link=%u stations=%d bat=%d%%%s heap=%u/%u | %s tx=%u injDrop=%u "
        "sent=%u sendErr=%u cbFail=%u rxQDrop=%u rxErr=%u "
        "csBusy=%u air=%ums\r\n",
        sn.rx.packets, sn.rx.lost, total ? 100.0 * sn.rx.lost / total : 0.0,
        sn.rx.stale, sn.rx.resyncs, sn.rx.late, sn.rx.watchdogs,
        sn.rx.reasserts, sn.rx.restarts, sn.rssi, (long)sn.offset, sn.linkUp,
        sn.stations, battery.valid ? battery.percent : -1,
        battery.charging ? "(chg)" : "", ESP.getFreeHeap(),
        ESP.getMinFreeHeap(), radioName(net::radio()), sn.txPackets,
        sn.injectedDrops, sn.net.sent, sn.net.sendErr, sn.net.cbFail,
        sn.net.rxDropped, sn.net.rxErr, sn.net.csBusy, sn.net.airMs);
}

// Serial output is US-ASCII only, with CR LF line endings (some terminals
// garble UTF-8 or LF-only newlines)
void printHelp() {
    struct Line {
        const char* cmd;
        const char* desc;
    };
    static const Line LINES[] = {
        {"ch N", "channel (CH_MIN-CH_MAX)"},
        {"radio RADIOS", "radio type (off = no transmission; restarts)"},
        {"mode straight|iambic_a|iambic_b", "key mode"},
        {"wpm N", "keyer speed (WPM)"},
        {"swap 0|1", "swap paddle DIT and DAH"},
        {"side N", "sidetone frequency (Hz)"},
        {"side on|off", "sidetone on / off"},
        {"rxtone N", "receive tone frequency (Hz)"},
        {"vol N", "volume (0-5) of the current output"},
        {"output speaker|phones", "sound output (Cardputer ADV only)"},
        {"jitter N", "jitter buffer delay (ms)"},
        {"term", "terminal mode: type to send"},
        {"msg [TEXT]", "show / set the message (BtnA long press)"},
        {"test on [TEXT]", "send TEXT repeatedly at the current WPM"},
        {"test off", "stop the test"},
        {"loss N", "drop N% of sent packets (test)"},
        {"dropup 0|1", "drop packets with key UP (test)"},
        {"log 0|1", "transition log and stats every 5 s"},
        {"stats", "show statistics"},
        {"show", "show settings and power status"},
        {"clear", "clear the text on the screen"},
        {"save", "save settings now"},
        {"sleep", "go to sleep now"},
        {"defaults", "restore all settings to defaults"},
        {"help", "show this help"},
    };
    Serial.print("commands:\r\n");
    for (const Line& l : LINES) {
        char cmd[40], desc[96];
        // fill in the parts that depend on board model and settings
        if (!strcmp(l.cmd, "radio RADIOS"))
            snprintf(cmd, sizeof(cmd), "radio %s",
                     net::loraAvailable() ? "espnow|lora|off" : "espnow|off");
        else
            snprintf(cmd, sizeof(cmd), "%s", l.cmd);
        if (!strcmp(l.desc, "channel (CH_MIN-CH_MAX)"))
            snprintf(desc, sizeof(desc), "channel (%d-%d)", cfg::CH_MIN,
                     cfg::CH_MAX);
        else
            snprintf(desc, sizeof(desc), "%s", l.desc);
        Serial.printf("  %-32s %s\r\n", cmd, desc);
    }
}

// ---- Sleep ----
void goToSleep() {
    asyncOut();
    Serial.print("going to sleep (wake: button)\r\n");
    if (saveDirty) saveSettings();
    net::end();
    ui::sleepDisplay();
    if (!power::enterDeepSleep()) {
        Serial.println("sleep aborted: button interrupt pending");
        ESP.restart();  // the display was turned off, so restart
    }
}

// Remaining time until sleep (ms). -1 when not going to sleep
int32_t sleepRemainingMs(uint32_t now, const Snapshot& sn) {
    if (usbPower) return -1;
    uint32_t last = lastInputMs;
    if ((int32_t)(sn.lastActivityMs - last) > 0) last = sn.lastActivityMs;
    int32_t idle = (int32_t)(now - last);
    return (int32_t)cfg::SLEEP_TIMEOUT_MS - idle;
}

// Change the radio type. jitter becomes the value saved for that radio (takes
// effect after restart)
void setRadio(uint8_t radio) {
    uint16_t j = storedJitter(radio);
    editSettings([&](Settings& s) {
        s.radio = radio;
        s.jitterMs = j;
    });
}

// If the configured radio type differs from the running one, save and restart
void restartIfRadioChanged() {
    lockShared();
    uint8_t r = g_settings.radio;
    unlockShared();
    if (r == net::radio()) return;
    asyncOut();
    Serial.printf("radio changed to %s: restarting\r\n", radioName(r));
    saveSettings();
    net::end();
    delay(50);
    ESP.restart();
}

// ---- Terminal mode ----
// Typed characters go into the TX queue and are shown immediately (echo).
// Received characters are shown in arrival order. When the direction changes,
// start a new line with "> " (typed characters) / "< " (received)
void termMark(char dir) {
    if (termDir == dir) return;
    Serial.printf("\r\n%c ", dir);
    termDir = dir;
}

void enterTerm(bool on) {
    termMode = on;
    termDir = 0;
    lockShared();
    g_typeBuf.clear();
    if (on) g_logOn = false;
    unlockShared();
    editControl([&](Control& c) {
        c.termOn = on;
        if (on) c.testOn = false;
    });
    if (on) {
        promptShown = false;
        lockShared();
        int wpm = g_settings.wpm;
        unlockShared();
        Serial.printf(
            "\r\n[terminal mode: %d WPM. Type to send; received text follows "
            "'<'. Ctrl-C to quit]\r\n",
            wpm);
    } else {
        Serial.print("\r\n[terminal mode off]\r\n");
        promptShown = false;
        lastAsyncMs = 0;  // show the prompt right away
    }
}

void handleTermChar(char ch) {
    if (ch == 0x03 || ch == 0x04) {  // Ctrl-C / Ctrl-D
        enterTerm(false);
        return;
    }
    bool lf = ch == '\n';
    if (lf && termLastCr) {  // CR LF
        termLastCr = false;
        return;
    }
    termLastCr = ch == '\r';
    bool newline = ch == '\r' || lf;
    if (newline) ch = ' ';  // newline is sent as a word gap
    if (ch == '\b' || ch == 0x7F) {
        lockShared();
        bool ok = g_typeBuf.popBack();
        unlockShared();
        if (ok && termDir == '>')
            Serial.print("\b \b");
        else
            Serial.write(
                '\a');  // a character already being sent cannot be cancelled
        return;
    }
    if (ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
    bool valid = ch == ' ' || ch == '[' || ch == ']' || cw::morseEncode(ch);
    bool ok = false;
    if (valid) {
        lockShared();
        ok = g_typeBuf.push(ch);
        unlockShared();
    }
    if (!ok) {
        Serial.write('\a');  // character without a Morse code, or buffer full
        return;
    }
    termMark('>');
    if (newline)
        Serial.print("\r\n> ");
    else
        Serial.write(ch);
}

// Restore all settings to defaults (also erases saved values). If the radio
// type changes, restart to switch: on leaving the settings screen, or
// immediately for the serial command
void restoreDefaults() {
    prefs.begin("morse", false);
    prefs.clear();
    prefs.end();
    lockShared();
    g_settings = Settings();  // defaults from shared.h: ESP-NOW, CH1, IAMBIC_B,
                              // NORMAL, 20WPM, SIDETONE ON, VOL 3, 700/600Hz
    g_settingsVer++;
    unlockShared();
    saveSettings();
    loadMessage();  // the namespace was cleared, so back to MSG_DEFAULT
    asyncOut();
    Serial.print("settings restored to defaults\r\n");
}

// ---- Serial commands ----
void handleCommand(char* line) {
    char* cmd = strtok(line, " \t");
    if (!cmd) return;
    char* arg = strtok(nullptr, "");
    while (arg && *arg == ' ') arg++;
    int n = arg ? atoi(arg) : 0;

    if (!strcmp(cmd, "mode") && arg) {
        int m = !strcasecmp(arg, "straight")   ? 0
                : !strcasecmp(arg, "iambic_a") ? 1
                : !strcasecmp(arg, "iambic_b") ? 2
                                               : -1;
        if (m < 0) {
            Serial.println("unknown mode");
            return;
        }
        editSettings([&](Settings& s) { s.mode = m; });
    } else if (!strcmp(cmd, "wpm") && arg) {
        editSettings([&](Settings& s) { s.wpm = n; });
    } else if (!strcmp(cmd, "side") && arg) {
        if (!strcasecmp(arg, "on") || !strcasecmp(arg, "off")) {
            bool on = !strcasecmp(arg, "on");
            editSettings([&](Settings& s) { s.sideOn = on; });
        } else {
            editSettings([&](Settings& s) { s.sideHz = n; });
        }
    } else if (!strcmp(cmd, "rxtone") && arg) {
        editSettings([&](Settings& s) { s.rxHz = n; });
    } else if (!strcmp(cmd, "vol") && arg) {
        // volume of the current output (speaker / headphones)
        editSettings([&](Settings& s) {
            (s.output == cfg::OUTPUT_PHONES ? s.hpVolume : s.volume) = n;
        });
    } else if (!strcmp(cmd, "output") && arg) {
        int o = !strcasecmp(arg, "speaker")  ? cfg::OUTPUT_SPEAKER
                : !strcasecmp(arg, "phones") ? cfg::OUTPUT_PHONES
                                             : -1;
        if (o < 0 || !hw::hasPhoneJack()) {
            Serial.print(o < 0 ? "unknown output\r\n"
                               : "no headphone jack on this board\r\n");
            return;
        }
        editSettings([&](Settings& s) { s.output = o; });
    } else if (!strcmp(cmd, "swap") && arg) {
        editSettings([&](Settings& s) { s.swap = n; });
    } else if (!strcmp(cmd, "ch") && arg) {
        editSettings([&](Settings& s) { s.channel = n; });
    } else if (!strcmp(cmd, "radio") && arg) {
        int r = !strcasecmp(arg, "espnow") ? cfg::RADIO_ESPNOW
                : !strcasecmp(arg, "lora") ? cfg::RADIO_LORA
                : !strcasecmp(arg, "off")  ? cfg::RADIO_OFF
                                           : -1;
        if (r < 0 || (r == cfg::RADIO_LORA && !net::loraAvailable())) {
            Serial.println(r < 0 ? "unknown radio"
                                 : "LoRa is not available on this board");
            return;
        }
        setRadio(r);
        restartIfRadioChanged();
    } else if (!strcmp(cmd, "jitter") && arg) {
        editSettings([&](Settings& s) { s.jitterMs = n; });
    } else if (!strcmp(cmd, "test") && arg) {
        char* sub = strtok(arg, " ");
        char* text = strtok(nullptr, "");
        bool on = sub && !strcmp(sub, "on");
        editControl([&](Control& c) {
            c.testOn = on;
            if (on && text) {
                strncpy(c.testText, text, sizeof(c.testText) - 1);
                c.testText[sizeof(c.testText) - 1] = 0;
            }
        });
    } else if (!strcmp(cmd, "loss") && arg) {
        editControl([&](Control& c) {
            c.lossPct = n < 0 ? 0 : n > 100 ? 100 : n;
        });
    } else if (!strcmp(cmd, "dropup") && arg) {
        editControl([&](Control& c) { c.dropUp = n != 0; });
    } else if (!strcmp(cmd, "term")) {
        enterTerm(true);
        return;
    } else if (!strcmp(cmd, "msg")) {
        if (arg && *arg) {
            char m[cfg::MSG_MAX + 1];
            if (!normalizeMessage(arg, m)) {
                Serial.printf(
                    "invalid message (Morse characters only, up to %d)\r\n",
                    cfg::MSG_MAX);
                return;
            }
            strcpy(g_message, m);
            prefs.begin("morse", false);
            prefs.putString("msg", g_message);
            prefs.end();
        }
        Serial.printf("msg=\"%s\"\r\n", g_message);
        return;
    } else if (!strcmp(cmd, "log") && arg) {
        lockShared();
        g_logOn = n != 0;
        unlockShared();
    } else if (!strcmp(cmd, "stats")) {
        printStats();
        return;
    } else if (!strcmp(cmd, "sleep")) {
        goToSleep();
        return;
    } else if (!strcmp(cmd, "show")) {
        power::printStatus();
        Serial.printf("version: %s\r\n", VERSION);
        Serial.printf("touch=%s\r\n",
                      ui::touchAvailable() ? "ok" : "not found");
        Serial.printf("msg=\"%s\"\r\n", g_message);
    } else if (!strcmp(cmd, "defaults")) {
        restoreDefaults();
        restartIfRadioChanged();
    } else if (!strcmp(cmd, "clear")) {
        ui::clearText();
    } else if (!strcmp(cmd, "save")) {
        saveSettings();
        return;
    } else {
        printHelp();
        return;
    }
    if (power::wokeFromSleep()) Serial.println("woke from sleep");
    printSettings();
}

void pollSerial() {
    while (Serial.available()) {
        char ch = Serial.read();
        if (termMode) {
            lastInputMs = millis();
            handleTermChar(ch);
            continue;
        }
        lastInputMs = millis();
        bool cr = ch == '\r';
        if (ch == '\n' && lastWasCr) {  // LF of CR LF
            lastWasCr = false;
            continue;
        }
        lastWasCr = cr;
        if (!promptShown)
            showPrompt();  // if the prompt was lost (e.g. after a log), reprint
                           // it before accepting input
        if (cr || ch == '\n') {
            Serial.print("\r\n");
            promptShown = false;
            if (lineLen) {
                lineBuf[lineLen] = 0;
                lineLen = 0;
                handleCommand(lineBuf);
                if (termMode)
                    continue;  // entered terminal mode via the term command
            }
            showPrompt();
        } else if (ch == '\b' || ch == 0x7F) {
            if (lineLen) {
                lineLen--;
                Serial.print("\b \b");
            }
        } else if (ch == 0x03) {  // Ctrl-C: discard the partially typed line
            lineLen = 0;
            Serial.print("^C\r\n");
            showPrompt();
        } else if (ch >= 0x20 && ch < 0x7F &&
                   lineLen < (int)sizeof(lineBuf) - 1) {
            lineBuf[lineLen++] = ch;
            Serial.write(ch);  // echo back
        }
    }
}

// ---- Buttons / touch ----
// BtnA = front button, BtnB = side button (ui::Button::Key1* / Key2*). The
// Cardputer has only BtnA, which takes the BtnB role; see handleKey
// Main screen:
//   BtnA short = channel +1 / BtnA long = send the stored message
//   BtnB short = settings screen / BtnB long = clear displayed text
// Settings screen (with touch: Nesso N1):
//   BtnA short = next item / BtnB short = previous item / long = exit (save)
//   Touch an item to select it; touch the up / down triangle on the right to
//   change the value (hold to repeat)
// Settings screen (no touch: StickS3, StickC Plus2, StickC Plus):
//   Selecting: BtnA short = next item / BtnB short = start editing /
//              long = exit (save)
//   Editing:   BtnA short = + / BtnB short = - / long = finish editing
void adjustSelected(int dir) {
    if (selected == ui::Item::Defaults) {
        // first press arms the confirmation, a second press within
        // DEFAULTS_CONFIRM_MS executes
        uint32_t now = millis();
        if (defaultsState == ui::DefaultsState::Armed &&
            !defaultsExpired(now)) {
            restoreDefaults();
            defaultsState = ui::DefaultsState::Done;
        } else {
            defaultsState = ui::DefaultsState::Armed;
            defaultsArmedMs = now;
        }
        return;
    }
    editSettings([&](Settings& s) {
        switch (selected) {
            case ui::Item::Mode:
                s.mode =
                    (s.mode + cw::KEY_MODE_COUNT + dir) % cw::KEY_MODE_COUNT;
                break;
            case ui::Item::Wpm:
                s.wpm += dir;
                break;
            case ui::Item::SideOn:
                s.sideOn = !s.sideOn;
                break;
            case ui::Item::SideHz:
                s.sideHz += dir * cfg::TONE_STEP_HZ;
                break;
            case ui::Item::RxHz:
                s.rxHz += dir * cfg::TONE_STEP_HZ;
                break;
            case ui::Item::Volume: {  // volume of the current output (speaker /
                                      // headphones)
                uint8_t& v =
                    s.output == cfg::OUTPUT_PHONES ? s.hpVolume : s.volume;
                if ((dir > 0 && v < ToneOut::VOLUME_MAX) || (dir < 0 && v > 0))
                    v += dir;
                break;
            }
            case ui::Item::Output:
                s.output = s.output == cfg::OUTPUT_PHONES ? cfg::OUTPUT_SPEAKER
                                                          : cfg::OUTPUT_PHONES;
                break;
            case ui::Item::Swap:
                s.swap = !s.swap;
                break;
            default:
                break;
        }
    });
    if (selected == ui::Item::Radio) setRadio(nextRadio(g_settings.radio, dir));
}

// Step the channel forward/back by 1 (wraps within CH_MIN..CH_MAX)
void stepChannel(int dir, uint32_t now) {
    const int n = cfg::CH_MAX - cfg::CH_MIN + 1;
    editSettings([&](Settings& s) {
        s.channel = cfg::CH_MIN + (s.channel - cfg::CH_MIN + n + dir) % n;
    });
    chOverlayUntil = now + cfg::CH_OVERLAY_MS;
}

void enterSettings(bool on, uint32_t now) {
    settingsScreen = on;
    editing = false;
    lastSettingsOpMs = now;
    if (!on && saveDirty) saveSettings();
    if (!on) restartIfRadioChanged();
    lastUiMs = 0;  // redraw right away
}

// ---- Keyboard (Cardputer) ----
// Main screen: character keys = send that character (in typed order at the
// configured speed), BS = cancel the last not-yet-sent character,
//             Enter / Tab / Space = word gap, fn+left / fn+right = channel -1 /
//             +1, fn+down = send the stored message, esc (fn+`) or ` =
//             settings screen
// Settings screen: up / down (or ; . without fn) = select item, left / right (,
// /) = decrease / increase value,
//             Enter (OK) = advance value (DEFAULTS executes on the 2nd press),
//             esc / ` = leave settings screen (save)
void handleKey(int k, uint32_t now) {
    lastInputMs = now;
    lastSettingsOpMs = now;
    lastUiMs = 0;
    bool esc = k == hw::KEY_ESC || k == '`';
    if (settingsScreen) {
        if (esc)
            enterSettings(false, now);
        else if (k == hw::KEY_UP || k == ';')
            selected = ui::stepItem(selected, -1);
        else if (k == hw::KEY_DOWN || k == '.')
            selected = ui::stepItem(selected, +1);
        else if (k == hw::KEY_LEFT || k == ',')
            adjustSelected(-1);
        else if (k == hw::KEY_RIGHT || k == '/' || k == hw::KEY_ENTER)
            adjustSelected(+1);
        return;
    }
    bool radioOn = net::radio() != cfg::RADIO_OFF;
    if (esc) {
        enterSettings(true, now);
    } else if (k == hw::KEY_LEFT || k == hw::KEY_RIGHT) {
        if (radioOn) stepChannel(k == hw::KEY_RIGHT ? +1 : -1, now);
    } else if (k == hw::KEY_DOWN) {
        sendMessage();
    } else if (k == hw::KEY_BACKSPACE) {
        lockShared();
        g_typeBuf.popBack();
        unlockShared();
    } else if (k == hw::KEY_ENTER || k == hw::KEY_TAB || k == ' ') {
        lockShared();
        g_typeBuf.push(' ');
        unlockShared();
    } else if (k > ' ' && k < 0x7F) {
        char ch = (k >= 'a' && k <= 'z') ? k - ('a' - 'A') : k;
        if (ch == '[' || ch == ']' ||
            cw::morseEncode(ch)) {  // ignore characters without a Morse code
            lockShared();
            g_typeBuf.push(ch);
            unlockShared();
        }
    }
}

void pollInput(uint32_t now) {
    for (int k; (k = hw::readKey()) != hw::KEY_NONE;) handleKey(k, now);
    ui::Button b = ui::pollButtons(now);
    ui::TouchEvent t = ui::pollTouch(settingsScreen);
    if (b != ui::Button::None || t.kind != ui::TouchEvent::None) {
        lastInputMs = now;
        lastSettingsOpMs = now;
        lastUiMs = 0;
    }
    if (g_logOn && t.kind != ui::TouchEvent::None) {
        asyncOut();
        Serial.printf("touch x=%d y=%d kind=%d\r\n", t.x, t.y, t.kind);
    }

    if (!settingsScreen) {
        bool radioOn =
            net::radio() != cfg::RADIO_OFF;  // with radio off, channels are
                                             // meaningless, so do not switch
        switch (b) {
            case ui::Button::Key1Short:
                if (radioOn) stepChannel(+1, now);
                break;
            case ui::Button::Key1Long:
                sendMessage();
                break;
            case ui::Button::Key2Short:
                enterSettings(true, now);
                break;
            case ui::Button::Key2Long:
                ui::clearText();
                break;
            default:
                break;
        }
        return;
    }
    if (now - lastSettingsOpMs >= SETTINGS_TIMEOUT_MS) {
        enterSettings(false, now);
        return;
    }
    bool longPress = b == ui::Button::Key1Long || b == ui::Button::Key2Long;
    if (hw::hasKeyboard()) {
        // The settings screen is operated with the keyboard. The button (BtnA),
        // short or long press, leaves the settings screen
        if (b != ui::Button::None) enterSettings(false, now);
        return;
    }
    if (!ui::touchAvailable()) {
        if (editing) {
            if (b == ui::Button::Key1Short)
                adjustSelected(+1);
            else if (b == ui::Button::Key2Short)
                adjustSelected(-1);
            else if (longPress)
                editing = false;
        } else {
            if (b == ui::Button::Key1Short)
                selected = ui::stepItem(selected, +1);
            else if (b == ui::Button::Key2Short)
                editing = true;
            else if (longPress)
                enterSettings(false, now);
        }
        return;
    }
    switch (b) {
        case ui::Button::Key1Short:
            selected = ui::stepItem(selected, +1);
            break;
        case ui::Button::Key2Short:
            selected = ui::stepItem(selected, -1);
            break;
        case ui::Button::Key1Long:
        case ui::Button::Key2Long:
            enterSettings(false, now);
            return;
        default:
            break;
    }
    switch (t.kind) {
        case ui::TouchEvent::Row:
            selected = ui::itemAt(t.row);
            break;
        // DEFAULTS is not executed on hold-to-repeat events (to avoid
        // accidentally restoring defaults)
        case ui::TouchEvent::Plus:
            if (!(t.repeat && selected == ui::Item::Defaults))
                adjustSelected(+1);
            break;
        case ui::TouchEvent::Minus:
            if (!(t.repeat && selected == ui::Item::Defaults))
                adjustSelected(-1);
            break;
        default:
            break;
    }
}

// ---- Output from cwTask ----
const char* eventKindName(uint8_t k) {
    switch ((cw::EventKind)k) {
        case cw::EventKind::Forced:
            return " WATCHDOG";
        case cw::EventKind::Reassert:
            return " REASSERT";
        default:
            return "";
    }
}

void drainOutput() {
    OutItem it;
    while (engine::popOut(it)) {
        switch (it.kind) {
            case OutItem::CharTx:
            case OutItem::CharRx:
                ui::addChar(it.kind == OutItem::CharRx, it.c);
                if (termMode) {
                    if (it.kind == OutItem::CharRx) {
                        termMark('<');
                        Serial.write(it.c == cw::CHAR_UNKNOWN ? '?' : it.c);
                    }
                } else if (g_logOn) {
                    asyncOut();
                    const char* tag =
                        it.kind == OutItem::CharRx ? "RXCHR" : "TXCHR";
                    if (it.c == cw::CHAR_UNKNOWN)
                        Serial.printf("%s <unknown>\r\n", tag);
                    else
                        Serial.printf("%s '%c'\r\n", tag, it.c);
                }
                break;
            case OutItem::LogLocal:
                // length of the previous state: went DOWN -> length of the
                // preceding UP
                asyncOut();
                Serial.printf("TX %c t=%lu prev%c=%lu\r\n",
                              it.state ? 'D' : 'U', (unsigned long)it.t,
                              it.state ? 'U' : 'D', (unsigned long)it.dur);
                break;
            case OutItem::LogRemote:
                asyncOut();
                Serial.printf("RX %c t=%lu prev%c=%lu%s p%u\r\n",
                              it.state ? 'D' : 'U', (unsigned long)it.t,
                              it.state ? 'U' : 'D', (unsigned long)it.dur,
                              eventKindName(it.eventKind), it.peer);
                break;
        }
    }
}

}  // namespace

void setup() {
#if ARDUINO_USB_CDC_ON_BOOT
    // USB CDC (Nesso N1 / StickS3)
    Serial.begin(115200);
    Serial.setTxBufferSize(SERIAL_TX_BUFFER);
    Serial.setTxTimeoutMs(0);  // do not block loop() when USB is not connected
#else
    // USB-UART bridge chip (CH9102 on StickC Plus2). The TX buffer must be set
    // before begin
    Serial.setTxBufferSize(SERIAL_TX_BUFFER);
    Serial.begin(115200);
#endif
    ui::begin();
    ui::drawSplash(VERSION);
    splashUntil = millis() + cfg::SPLASH_MS;
    loadSettings();  // after ui::begin (M5.begin), since board detection
                     // (Cardputer / ADV) is needed
    bool ok = engine::begin();
    uint8_t mac[6];
    net::ownMac(mac);
    Serial.printf(
        "MorseComm start (%s%s): MAC %02X:%02X:%02X:%02X:%02X:%02X radio=%s %s "
        "version=%s\r\n",
        board::NAME, hw::hasPhoneJack() ? " ADV" : "", mac[0], mac[1], mac[2],
        mac[3], mac[4], mac[5], radioName(net::radio()), ok ? "ok" : "FAILED",
        VERSION);
    printSettings();
}

void loop() {
    uint32_t now = millis();
    pollSerial();
    drainOutput();
    if (now - lastBtnMs >= BUTTON_INTERVAL_MS) {
        lastBtnMs = now;
        pollInput(now);
    }
    if (now - lastBatteryMs >= BATTERY_INTERVAL_MS || lastBatteryMs == 0) {
        lastBatteryMs = now;
        battery = ui::readBattery();
        usbPower = power::usbPowered();
    }
    if (usbPower)
        lastInputMs = now;  // never sleep on USB power (count from the moment
                            // it switches to battery)
    bool splash = (int32_t)(now - splashUntil) <
                  0;  // do not draw the screen during the splash (input and
                      // TX/RX keep running)
    if (!splash && now - lastUiMs >= UI_INTERVAL_MS) {
        lastUiMs = now;
        lockShared();
        Settings s = g_settings;
        Control c = g_control;
        Snapshot sn = g_snapshot;
        unlockShared();
        int32_t remain = sleepRemainingMs(now, sn);
        if (remain != -1 && remain <= 0) goToSleep();
        int sleepIn = (remain >= 0 && remain <= (int32_t)cfg::SLEEP_WARN_MS)
                          ? (remain + 999) / 1000
                          : -1;
        // the DEFAULTS confirmation is cancelled by moving to another item or
        // by timeout
        if (selected != ui::Item::Defaults || !settingsScreen ||
            (defaultsState == ui::DefaultsState::Armed && defaultsExpired(now)))
            defaultsState = ui::DefaultsState::Idle;
        if (settingsScreen)
            ui::drawSettings(s, selected, editing, battery, defaultsState);
        else {
            char pending[40];
            lockShared();
            g_typeBuf.copyTo(pending, sizeof(pending));
            unlockShared();
            ui::drawMain(s, c, sn, battery, sleepIn,
                         (int32_t)(now - chOverlayUntil) < 0, pending);
        }
    }
    // lastChangeMs may be updated partway through this loop() (later than now),
    // so compare signed
    if (saveDirty && !settingsScreen &&
        (int32_t)(now - lastChangeMs) >= (int32_t)SAVE_DELAY_MS)
        saveSettings();
    if (g_logOn && now - lastStatsMs >= STATS_INTERVAL_MS) {
        lastStatsMs = now;
        printStats();
    }
    // once log and other output is quiet, reprint the prompt
    // (lastAsyncMs may be updated partway through this loop(), so compare
    // against millis())
    if (!termMode && !promptShown && millis() - lastAsyncMs >= PROMPT_QUIET_MS)
        showPrompt();
    delay(2);
}
