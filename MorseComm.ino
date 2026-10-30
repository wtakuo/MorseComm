// MorseComm — ESP-NOW による CW (モールス) キーイング伝送 (Arduino Nesso N1 x2)
//
// 構成:
//   cwTask (engine.cpp, 1ms 周期, 高優先度): キー入力・キーヤー・送信・受信再生・トーン・デコード
//   loop()  (このファイル, 低優先度)      : 画面・ボタン・シリアルコマンド・設定保存
// 詳細は CLAUDE.md / README.md を参照。
#include <Arduino.h>
#include <Preferences.h>
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

namespace {

constexpr uint32_t UI_INTERVAL_MS = 100;
constexpr uint32_t BUTTON_INTERVAL_MS = 20;
constexpr uint32_t SAVE_DELAY_MS = 2000;      // 最後の変更からこの時間後に保存
constexpr uint32_t STATS_INTERVAL_MS = 5000;  // log on のときの統計出力間隔
constexpr uint32_t BATTERY_INTERVAL_MS = 5000;
constexpr uint32_t SETTINGS_TIMEOUT_MS = 30000;  // 設定画面を無操作で抜けるまで

Preferences prefs;
ui::Item selected = ui::Item::Mode;
bool settingsScreen = false;
uint32_t lastSettingsOpMs = 0;
ui::Battery battery;
bool saveDirty = false;
uint32_t lastChangeMs = 0, lastUiMs = 0, lastBtnMs = 0, lastStatsMs = 0, lastBatteryMs = 0;
uint32_t lastInputMs = 0;  // ボタン・タッチ・シリアル入力、USB 給電中
bool usbPower = true;
char lineBuf[96];
int lineLen = 0;

// ---- 設定 ----
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
  s.swap = s.swap ? 1 : 0;
  return s;
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
  s.jitterMs = prefs.getUShort("jitter", s.jitterMs);
  prefs.end();
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
  prefs.putUShort("jitter", s.jitterMs);
  prefs.end();
  saveDirty = false;
  Serial.println("settings saved");
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
      "mode=%s wpm=%u side=%s,%uHz rx=%uHz vol=%u swap=%u jitter=%ums | test=%s \"%s\" loss=%u%% dropup=%u log=%u\n",
      cw::keyModeName((cw::KeyMode)s.mode), s.wpm, s.sideOn ? "on" : "off", s.sideHz, s.rxHz, s.volume, s.swap,
      s.jitterMs,
                c.testOn ? "on" : "off", c.testText, c.lossPct, c.dropUp, g_logOn);
}

void printStats() {
  lockShared();
  Snapshot sn = g_snapshot;
  unlockShared();
  uint32_t total = sn.rx.packets + sn.rx.lost;
  Serial.printf(
      "STATS rx=%u lost=%u (%.1f%%) stale=%u resync=%u late=%u wd=%u reassert=%u restart=%u rssi=%d offset=%ld "
      "link=%u bat=%d%%%s heap=%u/%u | tx=%u injDrop=%u sent=%u sendErr=%u cbFail=%u rxQDrop=%u\n",
      sn.rx.packets, sn.rx.lost, total ? 100.0 * sn.rx.lost / total : 0.0, sn.rx.stale, sn.rx.resyncs, sn.rx.late,
      sn.rx.watchdogs, sn.rx.reasserts, sn.rx.restarts, sn.rssi, (long)sn.offset, sn.linkUp, battery.valid ? battery.percent : -1,
      battery.charging ? "(chg)" : "", ESP.getFreeHeap(), ESP.getMinFreeHeap(), sn.txPackets,
      sn.injectedDrops, sn.net.sent, sn.net.sendErr, sn.net.cbFail, sn.net.rxDropped);
}

void printHelp() {
  Serial.println(
      "commands:\n"
      "  mode straight|iambic_a|iambic_b   wpm N   vol 0-5   swap 0|1   jitter N\n"
      "  side N|on|off  (サイドトーン周波数 / ON・OFF)   rxtone N  (受信トーン周波数)\n"
      "  test on [TEXT] | test off          (TEXT を現在の WPM で自動キーイング)\n"
      "  loss N   (送信を N% 省略)   dropup 0|1 (UP のパケットを省略)\n"
      "  log 0|1  (遷移ログと定期統計)   stats   show   clear   help");
}

// ---- スリープ ----
void goToSleep() {
  Serial.println("going to sleep (wake: KEY1/KEY2)");
  if (saveDirty) saveSettings();
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
    Serial.printf("touch=%s\n", ui::touchAvailable() ? "ok" : "not found");
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
    if (ch == '\r' || ch == '\n') {
      if (lineLen) {
        lineBuf[lineLen] = 0;
        lastInputMs = millis();
        handleCommand(lineBuf);
        lineLen = 0;
      }
    } else if (lineLen < (int)sizeof(lineBuf) - 1) {
      lineBuf[lineLen++] = ch;
    }
  }
}

// ---- ボタン・タッチ ----
// メイン画面:  KEY1 短押し = 設定画面へ / KEY1 長押し = 表示テキストを消去
// 設定画面:    KEY1 短押し = 次の項目 / KEY2 短押し = 前の項目 / KEY1 長押し = 終了 (保存)
//              項目をタッチして選択、右側の [+] [-] をタッチして値を変更 (押し続けで連続変更)
void adjustSelected(int dir) {
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
}

void enterSettings(bool on, uint32_t now) {
  settingsScreen = on;
  lastSettingsOpMs = now;
  if (!on && saveDirty) saveSettings();
  lastUiMs = 0;  // すぐ描き直す
}

void pollInput(uint32_t now) {
  const int count = (int)ui::Item::Count;
  ui::Button b = ui::pollButtons(now);
  ui::TouchEvent t = ui::pollTouch(settingsScreen);
  if (b != ui::Button::None || t.kind != ui::TouchEvent::None) {
    lastInputMs = now;
    lastSettingsOpMs = now;
    lastUiMs = 0;
  }
  if (g_logOn && t.kind != ui::TouchEvent::None) Serial.printf("touch x=%d y=%d kind=%d\n", t.x, t.y, t.kind);

  if (!settingsScreen) {
    if (b == ui::Button::Key1Short) enterSettings(true, now);
    else if (b == ui::Button::Key1Long) ui::clearText();
    return;
  }
  switch (b) {
    case ui::Button::Key1Short: selected = (ui::Item)(((int)selected + 1) % count); break;
    case ui::Button::Key2Short: selected = (ui::Item)(((int)selected + count - 1) % count); break;
    case ui::Button::Key1Long: enterSettings(false, now); return;
    default: break;
  }
  switch (t.kind) {
    case ui::TouchEvent::Row: selected = (ui::Item)t.row; break;
    case ui::TouchEvent::Plus: adjustSelected(+1); break;
    case ui::TouchEvent::Minus: adjustSelected(-1); break;
    default: break;
  }
  if (now - lastSettingsOpMs >= SETTINGS_TIMEOUT_MS) enterSettings(false, now);
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
        if (g_logOn) {
          const char* tag = it.kind == OutItem::CharRx ? "RXCHR" : "TXCHR";
          if (it.c == cw::CHAR_UNKNOWN) Serial.printf("%s <unknown>\n", tag);
          else Serial.printf("%s '%c'\n", tag, it.c);
        }
        break;
      case OutItem::LogLocal:
        // 直前の状態の長さ: DOWN になった → 直前の UP の長さ
        Serial.printf("TX %c t=%lu prev%c=%lu\n", it.state ? 'D' : 'U', (unsigned long)it.t, it.state ? 'U' : 'D',
                      (unsigned long)it.dur);
        break;
      case OutItem::LogRemote:
        Serial.printf("RX %c t=%lu prev%c=%lu%s\n", it.state ? 'D' : 'U', (unsigned long)it.t, it.state ? 'U' : 'D',
                      (unsigned long)it.dur, eventKindName(it.eventKind));
        break;
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);  // USB 未接続でも loop() を止めない
  loadSettings();
  ui::begin();
  bool ok = engine::begin();
  uint8_t mac[6];
  net::ownMac(mac);
  Serial.printf("MorseComm start: MAC %02X:%02X:%02X:%02X:%02X:%02X espnow=%s\n", mac[0], mac[1], mac[2], mac[3],
                mac[4], mac[5], ok ? "ok" : "FAILED");
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
    if (settingsScreen) ui::drawSettings(s, selected, battery);
    else ui::drawMain(c, sn, battery, sleepIn);
  }
  if (saveDirty && !settingsScreen && now - lastChangeMs >= SAVE_DELAY_MS) saveSettings();
  if (g_logOn && now - lastStatsMs >= STATS_INTERVAL_MS) {
    lastStatsMs = now;
    printStats();
  }
  delay(2);
}
