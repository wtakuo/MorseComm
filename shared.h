// shared.h — キーイングタスク (cwTask) と loop() (UI/シリアル) の間で共有するデータ
// 共有構造体の読み書きは必ず lockShared()/unlockShared() の間で行う。
#pragma once
#include <stdint.h>
#include "jitter.h"
#include "keyer.h"
#include "net.h"

// 永続化する設定 (Preferences)
struct Settings {
  uint8_t mode = (uint8_t)cw::KeyMode::IambicB;
  uint8_t wpm = cfg::WPM_DEFAULT;
  uint8_t sideOn = 1;  // サイドトーン ON/OFF
  uint16_t sideHz = cfg::SIDE_HZ_DEFAULT;
  uint16_t rxHz = cfg::RX_HZ_DEFAULT;
  uint8_t volume = cfg::VOLUME_DEFAULT;      // スピーカーの音量
  uint8_t output = cfg::OUTPUT_SPEAKER;      // 音の出口 (Cardputer ADV のみ意味がある)
  uint8_t hpVolume = cfg::HP_VOLUME_DEFAULT;  // ヘッドフォンの音量
  uint8_t swap = 0;  // パドルの DIT/DAH 入れ替え
  uint16_t jitterMs = cfg::JITTER_DELAY_MS;
  uint8_t channel = cfg::CH_DEFAULT;  // 通信チャンネル
  uint8_t radio = cfg::RADIO_ESPNOW;  // 無線の種類 (切り替えは再起動で反映する)
};

// 実行時の制御 (保存しない): テストモード・ロス注入
struct Control {
  bool testOn = false;
  char testText[cw::TextKeyer::MAX_TEXT] = "PARIS";
  uint8_t lossPct = 0;   // 送信を一定確率で省略
  bool dropUp = false;   // state==UP のパケットを省略
  bool termOn = false;   // 端末モード: シリアルで打った文字 (g_typeBuf) を送る
};

// cwTask → UI の状態スナップショット
struct Snapshot {
  cw::RxStats rx;
  net::Stats net;
  int32_t offset = 0;
  int8_t rssi = 0;
  bool linkUp = false;  // リンクが生きている局が 1 つ以上ある
  int stations = 0;      // リンクが生きている局の数
  bool localDown = false;
  bool remoteDown = false;
  int localWpm = 0;    // 自局デコーダの推定 WPM
  int remoteWpm = 0;   // 最後に受信した局のデコーダの推定 WPM
  uint32_t txPackets = 0;
  uint32_t injectedDrops = 0;  // ロス注入で送らなかったパケット
  uint32_t lastActivityMs = 0;  // 最後に自局・相手局のキー遷移があった時刻
  uint32_t nowMs = 0;
};

// cwTask → UI の出力 (デコード文字・ログ)
struct OutItem {
  enum Kind : uint8_t { CharTx, CharRx, LogLocal, LogRemote } kind;
  char c;
  uint8_t state;
  uint8_t eventKind;   // cw::EventKind (LogRemote)
  uint32_t t;
  uint32_t dur;        // 直前の状態の長さ
  uint8_t peer = 0;    // CharRx / LogRemote: 送信元の局 (PeerTable のスロット番号)
};

void lockShared();
void unlockShared();

extern Settings g_settings;
extern uint32_t g_settingsVer;  // 変更するたびに +1 (cwTask が検知して反映する)
extern Control g_control;
extern uint32_t g_controlVer;
extern Snapshot g_snapshot;
extern bool g_logOn;
extern cw::TypeBuffer g_typeBuf;  // 端末モードの送信待ち文字 (lockShared で保護)
