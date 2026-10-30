// config.h — 既定パラメータ (ボード共通)
// Arduino 非依存 (ホストテストからも include する) にしておくこと。
#pragma once
#include <stdint.h>

namespace cfg {

// ピンなどボード固有の定数は board.h とボードごとの .cpp にある

// ---- キー入力 ----
constexpr uint32_t DEBOUNCE_MS = 4;   // ロックアウト型デバウンス

// ---- キーヤー既定値 ----
constexpr int WPM_DEFAULT = 20;
constexpr int WPM_MIN = 5;
constexpr int WPM_MAX = 50;

// ---- トーン ----
constexpr int SIDE_HZ_DEFAULT = 700;   // サイドトーン (自局モニタ)
constexpr int RX_HZ_DEFAULT = 600;     // 受信トーン
constexpr int TONE_HZ_MIN = 300;
constexpr int TONE_HZ_MAX = 1500;
constexpr int TONE_STEP_HZ = 50;
constexpr int VOLUME_DEFAULT = 3;
constexpr uint32_t TONE_PREVIEW_MS = 150;  // 設定変更時の試聴音の長さ

// ---- 電源 ----
constexpr uint32_t SLEEP_TIMEOUT_MS = 180000;  // 電池運用時、無操作・無受信でこの時間経つとスリープ
constexpr uint32_t SLEEP_WARN_MS = 10000;      // スリープ前に予告を表示する時間

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
constexpr uint8_t ESPNOW_CHANNEL = 1;   // Wi-Fi のチャンネル (全局共通・固定)

// ---- 通信チャンネル (無線機の「チャンネル」に相当。パケットに載せ、同じ値の局どうしだけが通信する) ----
constexpr uint8_t CH_MIN = 1;
constexpr uint8_t CH_MAX = 10;
constexpr uint8_t CH_DEFAULT = 1;
constexpr uint32_t CH_OVERLAY_MS = 1200;  // チャンネル切り替え時に大きく表示する時間
constexpr int MAX_PEERS = 6;              // 同時に受信を追跡できる局の数

// ---- デコーダ ----
constexpr uint32_t DEC_DIT_INIT_MS = 60;    // 初期 dit 長 (20 WPM)
constexpr uint32_t DEC_DIT_MIN_MS = 20;     // 60 WPM
constexpr uint32_t DEC_DIT_MAX_MS = 240;    // 5 WPM

}  // namespace cfg
