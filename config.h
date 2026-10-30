// config.h — ピン割り当てと既定パラメータ
// Arduino 非依存 (ホストテストからも include する) にしておくこと。
#pragma once
#include <stdint.h>

namespace cfg {

// ---- ピン (Arduino Nesso N1) ----
// Grove ポート: GPIO5 = GROVE_IO_0, GPIO4 = GROVE_IO_1 (variants/arduino_nesso_n1/pins_arduino.h)
constexpr int PIN_STRAIGHT = 4;     // ストレートキー
constexpr int PIN_PADDLE_DIT = 4;   // パドル DIT (swap 設定で DAH と入れ替え可能)
constexpr int PIN_PADDLE_DAH = 5;   // パドル DAH
constexpr int PIN_BUZZER = 11;      // 内蔵ブザー (BEEP_PIN)
// キー入力はアクティブ Low (INPUT_PULLUP, キー閉 = LOW)

// ---- キー入力 ----
constexpr uint32_t DEBOUNCE_MS = 4;   // ロックアウト型デバウンス

// ---- キーヤー既定値 ----
constexpr int WPM_DEFAULT = 20;
constexpr int WPM_MIN = 5;
constexpr int WPM_MAX = 50;

// ---- トーン ----
constexpr int TONE_HZ_DEFAULT = 700;
constexpr int TONE_HZ_MIN = 300;
constexpr int TONE_HZ_MAX = 1500;

// ---- 送信スケジュール (§4.2) ----
constexpr uint32_t TX_INTERVAL_ACTIVE_MS = 20;   // DOWN 中と UP 後しばらくの送信間隔
constexpr uint32_t TX_ACTIVE_HOLD_MS = 500;      // UP 後にアクティブ間隔を続ける時間
constexpr uint32_t TX_INTERVAL_IDLE_MS = 500;    // アイドル時ハートビート
constexpr int HIST_N = 8;                        // 1パケットに載せる遷移履歴数
constexpr int HIST_MAX = 16;                     // 履歴数の上限 (パケット形式上の最大)

// ---- 受信 (§4.5) ----
constexpr uint32_t JITTER_DELAY_MS = 150;        // ジッタバッファ固定遅延
constexpr uint32_t WATCHDOG_MS = 100;            // 再生時間軸上で情報が途絶えてから強制 UP まで
constexpr uint32_t OFFSET_BUCKET_MS = 1000;      // 時刻オフセット最小値推定のバケット幅
constexpr int OFFSET_BUCKETS = 4;                // 直近 何バケット の最小値を使うか
constexpr uint32_t LINK_TIMEOUT_MS = 3000;       // これ以上無受信なら次のパケットで再初期化
constexpr int32_t SENDER_RESTART_BACKSTEP_MS = 2000;  // txMs がこれ以上戻ったら相手の再起動とみなす

// ---- ESP-NOW ----
constexpr uint8_t ESPNOW_CHANNEL = 1;
constexpr uint8_t GROUP_ID = 0x37;

// ---- デコーダ ----
constexpr uint32_t DEC_DIT_INIT_MS = 60;    // 初期 dit 長 (20 WPM)
constexpr uint32_t DEC_DIT_MIN_MS = 20;     // 60 WPM
constexpr uint32_t DEC_DIT_MAX_MS = 240;    // 5 WPM

}  // namespace cfg
