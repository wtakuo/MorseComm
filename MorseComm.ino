// MorseComm — ESP-NOW / LoRa による CW (モールス) キーイング伝送 (Arduino Nesso N1 / M5StickS3 / M5StickC Plus2)
//
// 構成:
//   cwTask (engine.cpp, 1ms 周期, 高優先度): キー入力・キーヤー・送信・受信再生・トーン・デコード
//   loop()  (このファイル, 低優先度)      : 画面・ボタン・シリアルコマンド・設定保存
// 詳細は CLAUDE.md / README.md を参照。
#include <Arduino.h>
#include <Preferences.h>
#include "board.h"
#include "config.h"
#include "shared.h"
#include "engine.h"
#include "ui.h"
#include "tone_out.h"
#include "power.h"

// ---- 共有データ (shared.h) ----
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
constexpr uint32_t SAVE_DELAY_MS = 2000;      // 最後の変更からこの時間後に保存
constexpr uint32_t STATS_INTERVAL_MS = 5000;  // log on のときの統計出力間隔
constexpr uint32_t BATTERY_INTERVAL_MS = 5000;
constexpr uint32_t SETTINGS_TIMEOUT_MS = 30000;  // 設定画面を無操作で抜けるまで
// USB シリアルの送信バッファ。既定 (256) では長い出力 (help など) が途中で切れる
// (USB 未接続でも止まらないよう送信タイムアウトを 0 にしているため、入りきらない分は捨てられる)
constexpr size_t SERIAL_TX_BUFFER = 4096;

Preferences prefs;
ui::Item selected = ui::Item::Radio;
bool settingsScreen = false;
bool editing = false;  // タッチの無いボード: 選択中の項目の値を変更中
uint32_t lastSettingsOpMs = 0;
ui::Battery battery;
bool saveDirty = false;
ui::DefaultsState defaultsState = ui::DefaultsState::Idle;  // 設定画面の DEFAULTS 項目
uint32_t defaultsArmedMs = 0;

// DEFAULTS の確認待ちが時間切れか。loop() の now は押した時刻より前のことがあるので符号付きで比べる
// (符号無しだと now - defaultsArmedMs が巨大な値になり、押した直後に取り消されてしまう)
bool defaultsExpired(uint32_t now) { return (int32_t)(now - defaultsArmedMs) >= (int32_t)cfg::DEFAULTS_CONFIRM_MS; }
uint32_t lastChangeMs = 0, lastUiMs = 0, lastBtnMs = 0, lastStatsMs = 0, lastBatteryMs = 0;
uint32_t chOverlayUntil = 0;  // この時刻までチャンネル番号を大きく表示する
uint32_t lastInputMs = 0;  // ボタン・タッチ・シリアル入力、USB 給電中
bool usbPower = true;
char lineBuf[96];
int lineLen = 0;
// コマンド入力のプロンプト。ログなどが割り込んだら改行して区切り、出力が静まったら打ちかけの行ごと出し直す
constexpr const char* PROMPT = "morse> ";
constexpr uint32_t PROMPT_QUIET_MS = 300;
bool promptShown = false;
bool lastWasCr = false;     // CR LF を 1 回の改行として扱う
uint32_t lastAsyncMs = 0;   // 最後に非同期の出力 (ログ・統計など) をした時刻
// 端末モード (シリアルで打った文字を送り、受信した文字を表示する)
bool termMode = false;
char termDir = 0;        // 最後に表示した向き: '>' = 打った文字, '<' = 受信した文字
bool termLastCr = false;  // 直前が CR (CR LF を 1 回の改行として扱う)

// ---- コンソール (プロンプト・エコー) ----
void showPrompt() {
  Serial.print(PROMPT);
  Serial.write((const uint8_t*)lineBuf, lineLen);
  promptShown = true;
}

// 非同期の出力 (ログ・統計・保存の通知など) の前に呼ぶ: プロンプトの行を改行で区切る
void asyncOut() {
  if (promptShown) Serial.print("\r\n");
  promptShown = false;
  lastAsyncMs = millis();
}

// ---- 設定 ----
// このボードで選べる無線の種類 (設定画面ではこの順に切り替わる)
bool radioSelectable(uint8_t r) {
  return r == cfg::RADIO_ESPNOW || r == cfg::RADIO_OFF || (r == cfg::RADIO_LORA && net::loraAvailable());
}

uint8_t nextRadio(uint8_t r, int dir) {
  static const uint8_t ORDER[] = {cfg::RADIO_ESPNOW, cfg::RADIO_LORA, cfg::RADIO_OFF};
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
  if (s.jitterMs > 1000) s.jitterMs = 1000;
  if (s.channel < cfg::CH_MIN || s.channel > cfg::CH_MAX) s.channel = cfg::CH_DEFAULT;
  s.swap = s.swap ? 1 : 0;
  if (!radioSelectable(s.radio)) s.radio = cfg::RADIO_ESPNOW;
  return s;
}

// ジッタ遅延は無線の種類ごとに保存する (LoRa は送信間隔が長いので既定値も大きい)
const char* jitterKey(uint8_t radio) { return radio == cfg::RADIO_LORA ? "ljitter" : "jitter"; }

uint16_t storedJitter(uint8_t radio) {
  prefs.begin("morse", true);
  uint16_t v = prefs.getUShort(jitterKey(radio),
                               radio == cfg::RADIO_LORA ? cfg::LORA_JITTER_DELAY_MS : cfg::JITTER_DELAY_MS);
  prefs.end();
  return v;
}

const char* radioName(uint8_t radio) {
  return radio == cfg::RADIO_LORA ? "lora" : radio == cfg::RADIO_OFF ? "off" : "espnow";
}

void loadSettings() {
  Settings s;
  prefs.begin("morse", true);
  s.mode = prefs.getUChar("mode", s.mode);
  s.wpm = prefs.getUChar("wpm", s.wpm);
  s.sideOn = prefs.getUChar("sideon", s.sideOn);
  s.sideHz = prefs.getUShort("sidehz", s.sideHz);
  s.rxHz = prefs.getUShort("rxhz", s.rxHz);
  s.volume = prefs.getUChar("vol", s.volume);
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
      "radio=%s ch=%u mode=%s wpm=%u side=%s,%uHz rx=%uHz vol=%u swap=%u jitter=%ums | test=%s \"%s\" loss=%u%% dropup=%u "
      "log=%u\r\n",
      radioName(s.radio), s.channel, cw::keyModeName((cw::KeyMode)s.mode), s.wpm, s.sideOn ? "on" : "off", s.sideHz,
      s.rxHz, s.volume, s.swap, s.jitterMs, c.testOn ? "on" : "off", c.testText, c.lossPct, c.dropUp, g_logOn);
}

void printStats() {
  asyncOut();
  lockShared();
  Snapshot sn = g_snapshot;
  unlockShared();
  uint32_t total = sn.rx.packets + sn.rx.lost;
  Serial.printf(
      "STATS rx=%u lost=%u (%.1f%%) stale=%u resync=%u late=%u wd=%u reassert=%u restart=%u rssi=%d offset=%ld "
      "link=%u stations=%d bat=%d%%%s heap=%u/%u | %s tx=%u injDrop=%u sent=%u sendErr=%u cbFail=%u rxQDrop=%u rxErr=%u "
      "csBusy=%u air=%ums\r\n",
      sn.rx.packets, sn.rx.lost, total ? 100.0 * sn.rx.lost / total : 0.0, sn.rx.stale, sn.rx.resyncs, sn.rx.late,
      sn.rx.watchdogs, sn.rx.reasserts, sn.rx.restarts, sn.rssi, (long)sn.offset, sn.linkUp, sn.stations,
      battery.valid ? battery.percent : -1, battery.charging ? "(chg)" : "", ESP.getFreeHeap(), ESP.getMinFreeHeap(),
      radioName(net::radio()), sn.txPackets, sn.injectedDrops, sn.net.sent, sn.net.sendErr, sn.net.cbFail,
      sn.net.rxDropped, sn.net.rxErr, sn.net.csBusy, sn.net.airMs);
}

// シリアルへの出力は US-ASCII だけにし、改行は CR LF にする (端末によっては UTF-8 や LF だけの改行で表示が乱れるため)
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
      {"vol N", "volume (0-5)"},
      {"jitter N", "jitter buffer delay (ms)"},
      {"term", "terminal mode: type to send"},
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
    // 機種・設定によって変わる部分を埋める
    if (!strcmp(l.cmd, "radio RADIOS"))
      snprintf(cmd, sizeof(cmd), "radio %s", net::loraAvailable() ? "espnow|lora|off" : "espnow|off");
    else snprintf(cmd, sizeof(cmd), "%s", l.cmd);
    if (!strcmp(l.desc, "channel (CH_MIN-CH_MAX)")) snprintf(desc, sizeof(desc), "channel (%d-%d)", cfg::CH_MIN, cfg::CH_MAX);
    else snprintf(desc, sizeof(desc), "%s", l.desc);
    Serial.printf("  %-32s %s\r\n", cmd, desc);
  }
}

// ---- スリープ ----
void goToSleep() {
  asyncOut();
  Serial.print("going to sleep (wake: button)\r\n");
  if (saveDirty) saveSettings();
  net::end();
  ui::sleepDisplay();
  if (!power::enterDeepSleep()) {
    Serial.println("sleep aborted: button interrupt pending");
    ESP.restart();  // 画面を消してしまったので起動し直す
  }
}

// スリープまでの残り時間 (ms)。スリープしない状況では -1
int32_t sleepRemainingMs(uint32_t now, const Snapshot& sn) {
  if (usbPower) return -1;
  uint32_t last = lastInputMs;
  if ((int32_t)(sn.lastActivityMs - last) > 0) last = sn.lastActivityMs;
  int32_t idle = (int32_t)(now - last);
  return (int32_t)cfg::SLEEP_TIMEOUT_MS - idle;
}

// 無線の種類を変える。jitter はその無線用に保存してあった値にする (反映は再起動で)
void setRadio(uint8_t radio) {
  uint16_t j = storedJitter(radio);
  editSettings([&](Settings& s) {
    s.radio = radio;
    s.jitterMs = j;
  });
}

// 設定された無線の種類が動作中のものと違えば、保存して再起動する
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

// ---- 端末モード ----
// 打った文字は送信待ちに入れてすぐ表示する (エコー)。受信した文字は届いた順に表示する。
// 向きが変わったら改行して "> " (打った文字) / "< " (受信) を付ける
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
    Serial.printf("\r\n[terminal mode: %d WPM. Type to send; received text follows '<'. Ctrl-C to quit]\r\n", wpm);
  } else {
    Serial.print("\r\n[terminal mode off]\r\n");
    promptShown = false;
    lastAsyncMs = 0;  // すぐプロンプトを出す
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
  if (newline) ch = ' ';  // 改行は語間として送る
  if (ch == '\b' || ch == 0x7F) {
    lockShared();
    bool ok = g_typeBuf.popBack();
    unlockShared();
    if (ok && termDir == '>') Serial.print("\b \b");
    else Serial.write('\a');  // 送り始めた文字は取り消せない
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
    Serial.write('\a');  // 符号の無い文字、またはバッファが一杯
    return;
  }
  termMark('>');
  if (newline) Serial.print("\r\n> ");
  else Serial.write(ch);
}

// すべての設定を既定値に戻す (保存済みの値も消す)。無線の種類が変わる場合は、
// 設定画面ならそれを抜けたとき、シリアルコマンドならすぐに再起動して切り替える
void restoreDefaults() {
  prefs.begin("morse", false);
  prefs.clear();
  prefs.end();
  lockShared();
  g_settings = Settings();  // shared.h の既定値: ESP-NOW, CH1, IAMBIC_B, NORMAL, 20WPM, SIDETONE ON, VOL 3, 700/600Hz
  g_settingsVer++;
  unlockShared();
  saveSettings();
  asyncOut();
  Serial.print("settings restored to defaults\r\n");
}

// ---- シリアルコマンド ----
void handleCommand(char* line) {
  char* cmd = strtok(line, " \t");
  if (!cmd) return;
  char* arg = strtok(nullptr, "");
  while (arg && *arg == ' ') arg++;
  int n = arg ? atoi(arg) : 0;

  if (!strcmp(cmd, "mode") && arg) {
    int m = !strcasecmp(arg, "straight") ? 0 : !strcasecmp(arg, "iambic_a") ? 1 : !strcasecmp(arg, "iambic_b") ? 2 : -1;
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
    editSettings([&](Settings& s) { s.volume = n; });
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
      Serial.println(r < 0 ? "unknown radio" : "LoRa is not available on this board");
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
    editControl([&](Control& c) { c.lossPct = n < 0 ? 0 : n > 100 ? 100 : n; });
  } else if (!strcmp(cmd, "dropup") && arg) {
    editControl([&](Control& c) { c.dropUp = n != 0; });
  } else if (!strcmp(cmd, "term")) {
    enterTerm(true);
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
    Serial.printf("touch=%s\r\n", ui::touchAvailable() ? "ok" : "not found");
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
    if (ch == '\n' && lastWasCr) {  // CR LF の LF
      lastWasCr = false;
      continue;
    }
    lastWasCr = cr;
    if (!promptShown) showPrompt();  // ログの後などでプロンプトが消えていたら出し直してから入力を受ける
    if (cr || ch == '\n') {
      Serial.print("\r\n");
      promptShown = false;
      if (lineLen) {
        lineBuf[lineLen] = 0;
        lineLen = 0;
        handleCommand(lineBuf);
        if (termMode) continue;  // term コマンドで端末モードに入った
      }
      showPrompt();
    } else if (ch == '\b' || ch == 0x7F) {
      if (lineLen) {
        lineLen--;
        Serial.print("\b \b");
      }
    } else if (ch == 0x03) {  // Ctrl-C: 打ちかけの行を捨てる
      lineLen = 0;
      Serial.print("^C\r\n");
      showPrompt();
    } else if (ch >= 0x20 && ch < 0x7F && lineLen < (int)sizeof(lineBuf) - 1) {
      lineBuf[lineLen++] = ch;
      Serial.write(ch);  // エコーバック
    }
  }
}

// ---- ボタン・タッチ ----
// KEY1 = 正面ボタン (StickS3: BtnA), KEY2 = 側面ボタン (StickS3: BtnB)
// メイン画面:  KEY1 短押し = チャンネル +1 / KEY1 長押し = チャンネル -1
//              KEY2 短押し = 設定画面へ / KEY2 長押し = 表示テキストを消去
// 設定画面 (タッチあり: Nesso N1):
//              KEY1 短押し = 次の項目 / KEY2 短押し = 前の項目 / 長押し = 終了 (保存)
//              項目をタッチして選択、右側の [▲] [▼] をタッチして値を変更 (押し続けで連続変更)
// 設定画面 (タッチなし: StickS3):
//   選択中:    KEY1 短押し = 次の項目 / KEY2 短押し = 変更を始める / 長押し = 終了 (保存)
//   変更中:    KEY1 短押し = + / KEY2 短押し = - / 長押し = 変更を終える
void adjustSelected(int dir) {
  if (selected == ui::Item::Defaults) {
    // 1 回目で確認待ち、DEFAULTS_CONFIRM_MS 以内の 2 回目で実行
    uint32_t now = millis();
    if (defaultsState == ui::DefaultsState::Armed && !defaultsExpired(now)) {
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
      case ui::Item::Mode: s.mode = (s.mode + cw::KEY_MODE_COUNT + dir) % cw::KEY_MODE_COUNT; break;
      case ui::Item::Wpm: s.wpm += dir; break;
      case ui::Item::SideOn: s.sideOn = !s.sideOn; break;
      case ui::Item::SideHz: s.sideHz += dir * cfg::TONE_STEP_HZ; break;
      case ui::Item::RxHz: s.rxHz += dir * cfg::TONE_STEP_HZ; break;
      case ui::Item::Volume:
        if ((dir > 0 && s.volume < ToneOut::VOLUME_MAX) || (dir < 0 && s.volume > 0)) s.volume += dir;
        break;
      case ui::Item::Swap: s.swap = !s.swap; break;
      default: break;
    }
  });
  if (selected == ui::Item::Radio) setRadio(nextRadio(g_settings.radio, dir));
}

// チャンネルを 1 つ進める/戻す (CH_MIN..CH_MAX で循環)
void stepChannel(int dir, uint32_t now) {
  const int n = cfg::CH_MAX - cfg::CH_MIN + 1;
  editSettings([&](Settings& s) { s.channel = cfg::CH_MIN + (s.channel - cfg::CH_MIN + n + dir) % n; });
  chOverlayUntil = now + cfg::CH_OVERLAY_MS;
}

void enterSettings(bool on, uint32_t now) {
  settingsScreen = on;
  editing = false;
  lastSettingsOpMs = now;
  if (!on && saveDirty) saveSettings();
  if (!on) restartIfRadioChanged();
  lastUiMs = 0;  // すぐ描き直す
}

// ---- キーボード (Cardputer) ----
// メイン画面: 文字キー = その文字を送る (打った順に設定の速度で)、BS = まだ送っていない最後の文字を取り消す、
//             Enter / Tab / Space = 語間、fn+← / fn+→ = チャンネル -1 / +1、esc (fn+`) または ` = 設定画面
// 設定画面:   ↑ / ↓ (fn なしの ; . でもよい) = 項目の選択、← / → (, /) = 値を減らす / 増やす、
//             Enter (OK) = 値を進める (DEFAULTS は 2 回で実行)、esc / ` = 設定画面を抜ける (保存)
void handleKey(int k, uint32_t now) {
  const int count = ui::itemCount();
  lastInputMs = now;
  lastSettingsOpMs = now;
  lastUiMs = 0;
  bool esc = k == hw::KEY_ESC || k == '`';
  if (settingsScreen) {
    if (esc) enterSettings(false, now);
    else if (k == hw::KEY_UP || k == ';') selected = (ui::Item)(((int)selected + count - 1) % count);
    else if (k == hw::KEY_DOWN || k == '.') selected = (ui::Item)(((int)selected + 1) % count);
    else if (k == hw::KEY_LEFT || k == ',') adjustSelected(-1);
    else if (k == hw::KEY_RIGHT || k == '/' || k == hw::KEY_ENTER) adjustSelected(+1);
    return;
  }
  bool radioOn = net::radio() != cfg::RADIO_OFF;
  if (esc) {
    enterSettings(true, now);
  } else if (k == hw::KEY_LEFT || k == hw::KEY_RIGHT) {
    if (radioOn) stepChannel(k == hw::KEY_RIGHT ? +1 : -1, now);
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
    if (ch == '[' || ch == ']' || cw::morseEncode(ch)) {  // 符号の無い文字は無視する
      lockShared();
      g_typeBuf.push(ch);
      unlockShared();
    }
  }
}

void pollInput(uint32_t now) {
  const int count = ui::itemCount();
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
    bool radioOn = net::radio() != cfg::RADIO_OFF;  // 電波 OFF ではチャンネルは意味が無いので切り替えない
    switch (b) {
      case ui::Button::Key1Short:
        if (radioOn) stepChannel(+1, now);
        break;
      case ui::Button::Key1Long:
        if (radioOn) stepChannel(-1, now);
        break;
      case ui::Button::Key2Short: enterSettings(true, now); break;
      case ui::Button::Key2Long: ui::clearText(); break;
      default: break;
    }
    return;
  }
  if (now - lastSettingsOpMs >= SETTINGS_TIMEOUT_MS) {
    enterSettings(false, now);
    return;
  }
  bool longPress = b == ui::Button::Key1Long || b == ui::Button::Key2Long;
  if (hw::hasKeyboard()) {
    // 設定画面はキーボードで操作する。ボタン (BtnA) は短押し・長押しとも設定画面を抜ける
    if (b != ui::Button::None) enterSettings(false, now);
    return;
  }
  if (!ui::touchAvailable()) {
    if (editing) {
      if (b == ui::Button::Key1Short) adjustSelected(+1);
      else if (b == ui::Button::Key2Short) adjustSelected(-1);
      else if (longPress) editing = false;
    } else {
      if (b == ui::Button::Key1Short) selected = (ui::Item)(((int)selected + 1) % count);
      else if (b == ui::Button::Key2Short) editing = true;
      else if (longPress) enterSettings(false, now);
    }
    return;
  }
  switch (b) {
    case ui::Button::Key1Short: selected = (ui::Item)(((int)selected + 1) % count); break;
    case ui::Button::Key2Short: selected = (ui::Item)(((int)selected + count - 1) % count); break;
    case ui::Button::Key1Long:
    case ui::Button::Key2Long: enterSettings(false, now); return;
    default: break;
  }
  switch (t.kind) {
    case ui::TouchEvent::Row: selected = (ui::Item)t.row; break;
    // DEFAULTS は押し続けの連続イベントでは実行しない (誤って既定値に戻さないため)
    case ui::TouchEvent::Plus:
      if (!(t.repeat && selected == ui::Item::Defaults)) adjustSelected(+1);
      break;
    case ui::TouchEvent::Minus:
      if (!(t.repeat && selected == ui::Item::Defaults)) adjustSelected(-1);
      break;
    default: break;
  }
}

// ---- cwTask からの出力 ----
const char* eventKindName(uint8_t k) {
  switch ((cw::EventKind)k) {
    case cw::EventKind::Forced: return " WATCHDOG";
    case cw::EventKind::Reassert: return " REASSERT";
    default: return "";
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
          const char* tag = it.kind == OutItem::CharRx ? "RXCHR" : "TXCHR";
          if (it.c == cw::CHAR_UNKNOWN) Serial.printf("%s <unknown>\r\n", tag);
          else Serial.printf("%s '%c'\r\n", tag, it.c);
        }
        break;
      case OutItem::LogLocal:
        // 直前の状態の長さ: DOWN になった → 直前の UP の長さ
        asyncOut();
        Serial.printf("TX %c t=%lu prev%c=%lu\r\n", it.state ? 'D' : 'U', (unsigned long)it.t, it.state ? 'U' : 'D',
                      (unsigned long)it.dur);
        break;
      case OutItem::LogRemote:
        asyncOut();
        Serial.printf("RX %c t=%lu prev%c=%lu%s p%u\r\n", it.state ? 'D' : 'U', (unsigned long)it.t, it.state ? 'U' : 'D',
                      (unsigned long)it.dur, eventKindName(it.eventKind), it.peer);
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
  Serial.setTxTimeoutMs(0);  // USB 未接続でも loop() を止めない
#else
  // USB-UART 変換チップ (StickC Plus2 の CH9102)。送信バッファは begin の前に設定する
  Serial.setTxBufferSize(SERIAL_TX_BUFFER);
  Serial.begin(115200);
#endif
  loadSettings();
  ui::begin();
  bool ok = engine::begin();
  uint8_t mac[6];
  net::ownMac(mac);
  Serial.printf("MorseComm start (%s): MAC %02X:%02X:%02X:%02X:%02X:%02X radio=%s %s\r\n", board::NAME, mac[0], mac[1],
                mac[2], mac[3], mac[4], mac[5], radioName(net::radio()), ok ? "ok" : "FAILED");
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
  if (usbPower) lastInputMs = now;  // USB 給電中は眠らない (電池に切り替わった時点から数える)
  if (now - lastUiMs >= UI_INTERVAL_MS) {
    lastUiMs = now;
    lockShared();
    Settings s = g_settings;
    Control c = g_control;
    Snapshot sn = g_snapshot;
    unlockShared();
    int32_t remain = sleepRemainingMs(now, sn);
    if (remain != -1 && remain <= 0) goToSleep();
    int sleepIn = (remain >= 0 && remain <= (int32_t)cfg::SLEEP_WARN_MS) ? (remain + 999) / 1000 : -1;
    // DEFAULTS の確認待ちは、別の項目に移るか時間切れで取り消す
    if (selected != ui::Item::Defaults || !settingsScreen ||
        (defaultsState == ui::DefaultsState::Armed && defaultsExpired(now)))
      defaultsState = ui::DefaultsState::Idle;
    if (settingsScreen) ui::drawSettings(s, selected, editing, battery, defaultsState);
    else {
      char pending[40];
      lockShared();
      g_typeBuf.copyTo(pending, sizeof(pending));
      unlockShared();
      ui::drawMain(s, c, sn, battery, sleepIn, (int32_t)(now - chOverlayUntil) < 0, pending);
    }
  }
  // lastChangeMs はこの loop() の途中 (now より後) に更新されることがあるので符号付きで比べる
  if (saveDirty && !settingsScreen && (int32_t)(now - lastChangeMs) >= (int32_t)SAVE_DELAY_MS) saveSettings();
  if (g_logOn && now - lastStatsMs >= STATS_INTERVAL_MS) {
    lastStatsMs = now;
    printStats();
  }
  // ログなどの出力が静まったらプロンプトを出し直す
  // (lastAsyncMs はこの loop() の途中で更新されることがあるので millis() で比べる)
  if (!termMode && !promptShown && millis() - lastAsyncMs >= PROMPT_QUIET_MS) showPrompt();
  delay(2);
}
