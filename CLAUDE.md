# CLAUDE.md — ESP-NOW / LoRa CW (モールス) キーイング伝送

## 1. プロジェクト概要

ESP32-C6 搭載の Arduino Nesso N1 2台の間で、ESP-NOW (Wi-Fiルータ不要) を使って
モールス符号のキーイング (A1A 相当: キーを押している間だけ「出る」) を双方向に伝送する。
(当初の計画。その後 M5StickS3 / M5StickC Plus2 / M5StickC Plus / Cardputer 対応・LoRa・電波 OFF モード・端末モードを追加。現状は §13)

- 受信側は相手のキー状態に合わせてトーンを鳴らす
- 送信側も自分用のモニタ(サイドトーン)を鳴らす
- 受信したキーイングをデコードして画面に文字表示する
- キーは **ストレートキー** と **パドル (アイアンビック)** の両対応
- 2台とも送信・受信の両方を行う (往復通信)
- 無線は ESP-NOW が基本。Nesso N1 どうしは LoRa (SX1262, 920MHz 帯) も選べる (設定の RADIO。2026-10-30 追加, §4.6)
- RADIO = OFF: 電波を出さない単独練習モード (ユーザー要望で追加)。Wi-Fi も LoRa も初期化せず、net::send は何もしない。
  キー・サイドトーン・送信デコード・スリープは通常どおり。メイン画面は灰色の "OFF" / "RADIO OFF"、KEY1 (チャンネル) は無効。
  設定画面の RADIO は Nesso: ESP-NOW → LoRa → OFF、StickS3 / StickC Plus2: ESP-NOW ↔ OFF
- 端末モード (`term`): PC のターミナルで打った文字をモールスで送り、受信文字を表示する (2026-10-31 追加)
- 対応ボード: Arduino Nesso N1 / M5StickS3 / M5StickC Plus2 / M5StickC Plus / M5Stack Cardputer (初代) / Cardputer ADV
  (機種が混在しても ESP-NOW で通信できる)。Cardputer はキーボードで打った文字も送れる

最大の設計課題は「キーを離したパケットが落ちると押しっぱなしになる」問題。
**イベント(押す/離す)ではなく、状態+遷移履歴を定期送信し、受信側でウォッチドッグを持つ**
ことで解決する (§4)。この方針を変更する場合は、先にユーザーに相談すること。

## 2. 環境

- ターゲット: Arduino Nesso N1 (ESP32-C6)。その後 M5StickS3 (ESP32-S3)・M5StickC Plus2 (ESP32) を追加 (§10)
  https://docs.arduino.cc/hardware/nesso-n1
- フレームワーク: Arduino (C++)
- ビルド: `/opt/homebrew/bin/arduino-cli`
- 実機: Nesso 2 台・StickS3・StickC Plus2 のうち、その時々につないだものが Mac に接続される。シリアルポートと `show` で区別する
- **FQBN・必要なボードパッケージ・ディスプレイ/ブザー等のライブラリは、推測せず調べて確認すること。**
  `arduino-cli board list` / `core list` / `core search` / `lib search` を使い、
  確認できた FQBN と手順はこのファイルの §10 に追記する
- Arduino ESP32 コアのバージョンによって ESP-NOW のコールバックのシグネチャが異なる
  (3.x 系は受信コールバックが `esp_now_recv_info_t*` を取る)。インストール済みバージョンに合わせること

### Nesso N1 のハードウェアについて (要確認)
この計画書の作成時点では、次の点を**確認できていない**。最初に公式ドキュメントとボード定義で調べ、
結果を §10 に記録すること。

- ブザー/スピーカーの有無と接続ピン (無ければ外付け圧電ブザーを GPIO に接続する想定)
- 画面の種類・解像度・使用ライブラリ
- 内蔵ボタンの数とピン
- キー/パドルを接続できる空き GPIO (Grove / 拡張ヘッダ等)

キー用ピンは `config.h` に定数として分離し、**配線をユーザーに確認してから**確定する。

## 3. ディレクトリ構成 (実装済み)

```
(リポジトリ直下がスケッチフォルダ。ユーザー指示により .ino はルートに置く)
  MorseComm.ino         # setup()/loop()、UI・シリアルコマンド・設定保存
  engine.h/.cpp         # 1ms 周期のキーイングタスク (キー入力〜送信、受信再生、トーン、デコード)
  shared.h              # タスク間の共有データ
  morse_table.h         # 符号表
  board.h               # ボード判別 (ARDUINO_ARDUINO_NESSO_N1 / ARDUINO_M5STACK_STICKS3 / _STICKC_PLUS2 / _STICKC_PLUS) とキーのピン
  config.h              # 既定パラメータ (ボード共通、Arduino非依存)
  protocol.h            # パケット構造体、エンコード/デコード (Arduino非依存)
  keyer.h               # ストレートキー処理、イアンビックキーヤー (Arduino非依存の状態機械)
  jitter.h              # 受信側ジッタバッファ・再生スケジューラ (Arduino非依存)
  morse_decoder.h       # タイミングからの文字デコード (Arduino非依存)
  tone_out.h + tone_out_buzzer.cpp (Nesso, Plus2, Plus: LEDC) / tone_out_m5speaker.cpp (StickS3, Cardputer)  # トーン出力
  net.h / net.cpp / net_internal.h   # 無線の共通部 (受信キュー・統計・ロス注入・無線の種類の振り分け)
  net_espnow.cpp        # ESP-NOW 送受信 (ボード共通)
  net_lora.cpp          # LoRa 送受信 (Nesso N1 のみ。専用タスク、キャリアセンス・休止、送信時刻の付け替え)
  ui.h/.cpp             # 画面 (メイン/設定) の描画とボタン・タッチ入力の解釈 (ボード共通)
  hw.h + hw_nesso.cpp / hw_m5stick.cpp (StickS3, Plus2, Plus, Cardputer: M5Unified)   # 画面デバイス・ボタン・タッチ・電池
  keyboard_cardputer.cpp  # Cardputer のキーボード (hw::readKey。他のボードでは空の実装)
  power.h + power_nesso.cpp / power_sticks3.cpp / power_stickc.cpp (Plus2, Plus) / power_cardputer.cpp  # USB 給電検出・スリープ
  (ボードごとの .cpp は全体を `#if defined(BOARD_xxx)` で囲む。Arduino はフォルダ内の全 .cpp をビルドするため)
  peers.h               # 複数局の受信: 送信元 MAC ごとの Receiver + デコーダ、全局 OR (Arduino非依存)
  tools/ser.sh          # シリアルへのコマンド送信と出力取得
  README.md             # 使い方・シリアルコマンド・仕組み (ユーザー向け)
test/
  host/                 # Mac上で g++ でビルドするユニットテスト・損失シミュレーション
```

**重要:** プロトコル、キーヤー、ジッタバッファ、デコーダは Arduino の API を直接呼ばず、
時刻は引数で渡す (`uint32_t nowMs`) 純粋なロジックとして書く。
こうすると `test/host/` でPC上のテストができ、パケットロスやジッタを再現可能な形で検証できる。

## 4. 通信方式 (確定方針)

### 4.1 基本方式
1. 送信側は、キー状態が変化したときだけでなく、**一定間隔で現在状態を送り続ける**
2. 各パケットに **直近の遷移履歴** (相対時刻つき) を載せ、取りこぼしを後続パケットで補完する
3. 受信側は **ジッタバッファ** (固定遅延) でタイミングを再現して再生する
4. 受信側は **ウォッチドッグ**: DOWN 中に一定時間パケットが来なければ強制的に UP にする

### 4.2 送信タイミング (初期値。すべて config.h に定数化)
| 状況 | 送信間隔 |
|---|---|
| キー DOWN 中、および UP になってから約 500 ms 間 | 20 ms |
| それ以降のアイドル | 500 ms (ハートビート、リンク監視用) |
| 状態遷移が発生した瞬間 | 即時に追加送信 |

- 遷移履歴数 N = 8 (最大 16 まで拡張可能な設計にする)
- ジッタバッファ遅延 = 150 ms (調整可能。実機計測後に見直す)
- ウォッチドッグタイムアウト = 100 ms (送信間隔 20 ms の約5倍)

### 4.3 パケット形式 (案。リトルエンディアン、packed)

```
offset size field
 0     1   magic      = 0xC7
 1     1   version    = 1
 2     1   channel    // 通信チャンネル 1..8。同じ値の局どうしだけが受理する (旧 group。2026-10-30 に変更)
 3     1   state      // 現在のキー状態: 1=DOWN, 0=UP
 4     2   pktSeq     // パケット通番 (統計用: ロス率測定)
 6     2   lastTid    // 最新の遷移ID (遷移ごとに +1、wrap あり)
 8     4   txMs       // 送信側 millis() (パケット生成時)
12     1   n          // 履歴数 0..N
13   2*n   age[i]     // i=0 が最新の遷移。txMs からの経過ms (uint16, 飽和)
```

- 遷移は DOWN/UP が必ず交互になるので、各履歴の状態は `state` から逆算できる
  (最新遷移の遷移後状態 = `state`、1つ前はその反対、…)
- 遷移 i の ID は `lastTid - i`
- 受信側は「最後に適用した tid より新しい遷移のみ」を古い順に適用する (重複・順序逆転に強い)
- 履歴数を超える連続ロスで遷移が欠けた場合は、現在の `state` に再同期し、再同期回数をログ/統計に記録する
- ペイロードは十分小さい (ESP-NOW の上限を大きく下回る)

### 4.4 ESP-NOW の扱い
- Wi-Fi は STA モード、**チャンネルは固定** (両機で同一。既定 1)。ルータには接続しない
- **ブロードキャスト (FF:FF:FF:FF:FF:FF) + パケット内の `channel`** で実装済み。受信側は送信元 MAC ごとに
  Receiver/デコーダを持つ (peers.h, 最大 6 局) ので、同じチャンネルなら 3 台以上でも通信できる
  (ブロードキャストは MAC レベルの ACK/再送がないため、本方式のロス耐性の検証に適している)
- 将来的にユニキャスト (MAC指定) やボタンによるペアリングへ拡張できる構造にしておく
- 送信コールバックの成否は統計として記録するが、**プロトコルの正しさは ACK に依存させない**
- 暗号化は初期実装では使わない (必要になったらユーザーに相談)

### 4.5 受信側の時刻合わせと再生
- 送受信機の時計は同期していないので、各遷移の送信側時刻 `txMs - age` を受信側の時間軸に写す
- 方式案: 受信パケットごとに `offset = rxLocalMs - txMs` を計算し、
  直近数秒の最小値 (最もネットワーク遅延が小さかったパケット) を採用して推定する。
  再生時刻 = `(遷移の送信側時刻) + offset + JITTER_DELAY_MS`
- 再生時刻が既に過去になっていた遷移 (遅延が大きすぎた) は、即時適用しつつ遅延超過としてカウントする
- 長時間運転でのクロックドリフトに耐えること (offset の緩やかな追従)

### 4.6 LoRa (Nesso N1 のみ, 2026-10-30 追加)
§4.1〜4.5 の方式 (状態+遷移履歴の定期送信、ジッタバッファ、ウォッチドッグ) はそのまま。送り方だけが違う。
- **法令:** 日本の 920MHz 帯 特定小電力無線局 (20mW 以下)。Nesso N1 は技適取得済み (本体裏に技適マーク、販売店
  スイッチサイエンスの説明。ユーザー確認済み)。ユーザーの指示は「920MHz 帯で 20mW なら問題ない」。
  総務省資料 (920MHz 帯の技術基準の見直し) で確認した条件 (20mW 以下, 920.5〜928.1MHz):
  | キャリアセンス時間 | 1 回の送信 | 送信後の休止 | 1 時間の総和 |
  |---|---|---|---|
  | 5ms 以上 | 4 秒以下 | 50ms 以上 | 制限なし |
  | 128µs〜5ms 未満 | 400ms 以下 | 2ms 以上 | 360 秒 (10%) 以下 |
  単位チャネル 200kHz (中心 920.6 + 0.2k MHz)、最大 5 チャネル結束。**5ms の区分を使う** (総和の管理が要らない)。
  キャリアセンスレベル -80dBm (給電線入力点) は記憶による値で、原文では未確認
- **パラメータ (config.h の LORA_*):** SF5 / BW500kHz (単位チャネル 3 つの結束) / CR4/5 / 13dBm / プリアンブル 8 /
  sync 0x12 / TCXO 3.0V。チャンネル n の中心 = 920.8 + 0.6(n-1) MHz (CH1..CH8 = 920.8..925.0)。
  キャリアセンス 5.2ms、休止 52ms (余裕を持たせた値)
- **送信:** engine は ESP-NOW と同じく 20ms ごと (遷移時は即時) にパケットを `net::send` に置く。LoRa タスクは
  休止とキャリアセンスが明けた時点の最新の 1 個だけを送り、送信直前に `restampPacket` で txMs と age を付け替え、
  `setPacketSeq` で通番を実際の送信順に振り直す (振り直さないと受信側が未送信分をロスと数える)。
  実効の送信間隔は約 62ms。アイドルのハートビートは 1000ms (`LORA_TX_INTERVAL_IDLE_MS`)
- **フレーム:** [送信元 MAC 6 バイト][cw パケット] (LoRa には送信元アドレスが無いため)。全局が同じ周波数で
  ブロードキャストする対等な関係 (ユーザーの要望)
- **受信:** ジッタ遅延の既定は 250ms (`LORA_JITTER_DELAY_MS`, Preferences のキーは "ljitter")。ウォッチドッグは
  ESP-NOW と同じ 100ms (発動条件は「最後のパケットから ジッタ遅延 + WD」なので、ジッタ遅延を延ばせば足りる)
- **切り替え:** 設定画面の RADIO / `radio espnow|lora`。保存して再起動する (実行中の切り替えはしない)
- ロス注入 (`loss` / `dropup`) は net 層で、LoRa は実際の送信直前に判定する (落とした場合も休止はする)

## 5. キー入力

- GPIO は `INPUT_PULLUP`、**アクティブ Low** (キー閉 = LOW) を既定とする
- ストレートキー: 1本。**デバウンスは数 ms** (CW の最短要素より十分短く。既定 3〜5 ms)
- パドル: DIT 用 / DAH 用の2本。ピンの入れ替え (swap) を設定で可能にする
- 動作モード (設定で切替、NVS/Preferences に保存):
  - `STRAIGHT`: ストレートキー
  - `IAMBIC_A` / `IAMBIC_B`: アイアンビックキーヤー (既定 B)
  - 必要なら `PADDLE_SINGLE` (単接点パドルとして扱う) も追加検討
- キーヤー速度 (WPM) は設定可能 (既定 20 WPM。初期 PARIS 基準: 1 dit = 1200/WPM ms)
- **どのモードでも、キーヤーの出力は最終的に「キー状態 (DOWN/UP) の時系列」となり、
  §4 の同一パケットで送る。** 通信路にはパドルかストレートかの区別を持ち込まない
- 注意: パドルのキーヤーは送信側で要素を生成する。受信側は WPM を知らなくても再現できる

## 6. トーン出力

- `ToneOut` クラスで抽象化: `begin()`, `on()`, `off()`, `setFrequency(hz)`
- 既定周波数 600〜700 Hz (設定可能)
- 送信側のモニタ音は、**キー操作に対する遅れが出ないよう、ネットワーク経由ではなく
  ローカルのキー状態から直接**鳴らす (ジッタバッファを通さない)
- 受信側の音は、ジッタバッファの再生タイムラインに従って鳴らす
- 自局と相手局が同時に DOWN の場合も、トーン出力は1系統で OR (どちらかが DOWN なら鳴らす)。
  ただし音色や周波数を変えて区別できるか (ハード的に可能なら) は後回し
- キークリックが気になる場合、振幅エンベロープ (数 ms の立上り/立下り) を検討する。
  使用するハードが振幅制御できない場合は割り切る
- 音量の変更手段は、ハードが対応している場合のみ

## 7. デコードと表示

- 受信側で、**ジッタバッファの再生タイムライン上の DOWN/UP 長**からデコードする
  (パケット到着時刻ではなく、再現されたタイミングを使う)
- デコーダ:
  - DOWN 長から dit/dah を判定 (適応的に dit 長を推定。閾値は dit の約2倍)
  - UP 長で、要素間 / 文字間 (約3 dit) / 語間 (約7 dit) を判定 (閾値はそれぞれ約2 dit, 約5 dit)
  - 推定 WPM を表示できるようにする
  - 不明な符号は `?` で表示
  - 欧文 (A–Z, 0–9, 主要な記号) をまず対応。和文モールスは今回のスコープ外
- 自局が送った符号もローカルでデコードして表示できる (送信/受信を表示上区別する)
- 画面表示 (画面サイズに依存するので、確認の上で調整):
  - 受信テキスト (スクロール)、送信テキスト
  - 状態: キーモード、WPM、トーン周波数
  - リンク状態: 最終受信からの経過、RSSI (取得できる場合)、推定ロス率、ジッタ遅延、再同期回数

## 8. 設定とUI

- 内蔵ボタン等で、最低限次を変更できるようにする (割り当ては実機のボタン構成を確認して決める):
  キーモード、WPM、トーン周波数
- 設定は `Preferences` で保存
- 設定が最小限しか操作できないハードの場合は、シリアルコマンド (`wpm 18`, `mode iambic_b` など) を併設してよい

## 9. テスト方針

### 9.1 ホスト側 (Mac, `test/host/`)
Arduino 非依存のロジック部分を `g++` でビルドして自動テストする。

- プロトコルのエンコード/デコード往復
- 損失模擬: ランダムロス (例: 10%, 30%, 50%)、**UP を含むパケットだけを落とす**、バースト損失、
  順序逆転、重複、ジッタ (例: 0〜80 ms の遅延変動) を与えたうえで、
  - **押しっぱなしにならない** (ウォッチドッグまたは再同期で必ず復帰する)
  - ロスが履歴数以内ならば、再現された遷移列が元の遷移列と一致する
  - 再現された各 DOWN/UP 長が元の長さに対して妥当な誤差 (例: ±数 ms〜10 ms 以内) に収まる
- キーヤー: 各モードで想定どおりの dit/dah 列が出ること (WPM に応じたタイミング)
- デコーダ: 既知文字列 (例: `PARIS`, `CQ CQ DE ...`) のタイミング列から正しくデコードできること、
  WPM を変えても動くこと

### 9.2 実機 (2台)
- 2台を Mac に接続し、`arduino-cli upload` で両方に書き込む。ポートは `board list` で確認して使い分ける
- **テストモード** (ビルドフラグまたはシリアルコマンドで有効化) を用意する:
  - 送信側が既知のパターン (例: `PARIS` を指定 WPM で) を自動でキーイングする
  - 受信側は、再現した遷移と推定した時間間隔・デコード結果をシリアルに出力する
  - 送信側にロス注入オプション (例: 一定確率で送信を省略、UP を含むパケットだけ省略) を持たせ、
    実機でも「押しっぱなしにならない」ことを確認する
- 確認項目: 通信の双方向動作、トーン、デコード表示、ロス注入時の復帰、
  数分間連続動作させたときの時間ずれ・メモリリークの有無
- 人手の操作が必要な確認 (実際にキーを叩く、音を聞く、画面を見る) は、
  何をしてほしいかを明示してユーザーに依頼すること

## 10. 実機・環境メモ (Claude Code が調査結果をここに追記する)

- FQBN: `esp32:esp32:arduino_nesso_n1` (`board list` では "ESP32 Family Device" と表示されるが、このFQBNを指定する)
- ボードパッケージ / コアバージョン: `esp32:esp32` 3.3.12 (ESP-IDF 5.5 系)。
  ESP-NOW の受信CBは `(const esp_now_recv_info_t*, const uint8_t*, int)`、
  送信CBは `(const esp_now_send_info_t* /*=wifi_tx_info_t*/, esp_now_send_status_t)`。
  FreeRTOS は 1000Hz ティック・シングルコア
- 使用ライブラリ: `Arduino_Nesso_N1` 1.0.0 (依存: M5GFX 0.2.35, Arduino_BMI270_BMM150)。
  画面は ST7789 135x240 (`NessoDisplay`, LovyanGFX 互換 API)。`setRotation(1)` で 240x135 横長
- ボタン: KEY1 / KEY2 は I2C の IO エキスパンダ (PI4IOE5V6408, 0x43) 経由。`digitalRead(KEY1)` で読める (押下=LOW)。
  `NessoDisplay::begin()` が INPUT_PULLUP に設定する。I2C なので loop() 側からのみ読む
- Grove: GPIO5 = `GROVE_IO_0`, GPIO4 = `GROVE_IO_1`。`GROVE_POWER_EN` (エキスパンダ) を触らなくても
  INPUT_PULLUP + キー接点 → GND で読めることを実機で確認
- 2台のシリアルポートと MAC アドレス:
  - `/dev/cu.usbmodem101`  : [Nesso-A] — アイアンビックパドル (GPIO4, GPIO5)
  - `/dev/cu.usbmodem1101` : [Nesso-B] — ストレートキー (GPIO4)
- キー・パドルの接続ピン: ストレート = GPIO4。パドル = DIT GPIO4 / DAH GPIO5 (ユーザー確認済み。設定画面の PADDLE で入れ替え可)
- トーン出力の方法とピン: 内蔵ブザー `BEEP_PIN` = GPIO11 を LEDC PWM で駆動 (音量はデューティ比で5段階)。サイドトーンと受信トーンは別周波数、自局 DOWN 時はサイドトーン優先
- タッチ: FT6x36 (0x38, `NessoTouch`)。生座標 (縦置き 135x240) → setRotation(1) の画面座標は「xy 入れ替え + y 反転」で正しいことを実機で確認 (config.h の TOUCH_*)
- 電源: USB 給電は `digitalRead(VIN_DETECT)` (エキスパンダ 0x44 bit5) が HIGH、電池のみで LOW (どちらも実機で確認)。
  ディープスリープからの復帰: KEY1/KEY2 (0x43 bit0/1) の割り込みを有効化 (0x11 = 0xFC) → エキスパンダの INT が
  `SYS_IRQ` = GPIO3 (通常 High) → `esp_sleep_enable_ext1_wakeup_io(1<<3, ANY_LOW)`。実機で復帰を確認 (wake cause = EXT1)
- 電池: `NessoBattery::getChargeLevel()` (BQ27220) で残量 %。`battery.begin()` は充電設定を書き換えるので呼んでいない
- シリアル: USB CDC (115200)。`arduino-cli monitor` はパイプ経由だと何も出ないため、
  ポートを開いた fd に対して `stty 115200 raw` してから `cat` で読む (tools/ser.sh)
- M5StickS3 (2026-10-30 調査・実機確認):
  - FQBN: `m5stack:esp32:m5stack_sticks3` (M5Stack のボードパッケージ 3.3.10。esp32:esp32 には無い)。
    コアのヘッダが NUM_DIGITAL_PINS 等の再定義警告を出すが無害
  - **複数機種を同じフォルダからビルドするので `--build-path build/<board>` を必ず分ける**
    (分けないとビルドキャッシュが共有され、別チップ向けのブートローダを書こうとして失敗する)
  - 画面: ST7789 135x240 を M5GFX が自動判別 (M5.Display)、`setRotation(1)` で 240x135。向きはユーザー確認済み
  - ボタン: BtnA = GPIO11 (正面), BtnB = GPIO12 (側面)、押下 = LOW。電源ボタンは読めなかった
  - キー: Grove (Port A) GPIO10 = ストレート / DIT、GPIO9 = DAH (実機で確認)。M5.begin で外部 I2C 機器の探索を無効化している
  - 音: ES8311 + アンプのスピーカー (M5.Speaker, 22050Hz)。既定の DMA バッファ 256x8 だと約 93ms 遅れるので
    64x4 (約 12ms) に短縮。サイドトーン・受信トーンともユーザー確認済み
  - 電源: PMIC は M5PM1。USB 給電は `M5.Power.getVBUSVoltage()` > 4000mV (USB 時 約 5200mV)。
    電池 % は `getBatteryLevel()`。スリープ復帰は BtnA/BtnB (RTC GPIO) の EXT1 ANY_LOW。電池でのスリープ/復帰をユーザー確認済み
  - ESP32-S3 でもタイミングは完全に再現 (Nesso ⇔ StickS3 双方向、PARIS / CQ で誤差 0ms)
- M5StickC Plus2 (2026-10-31 調査・実機確認):
  - FQBN: `m5stack:esp32:m5stack_stickc_plus2` (ビルドディレクトリは build/stickcplus2)。ESP32-PICO-V3-02 (無印 ESP32)
  - PMIC 無し。電池運用時は HOLD = G4 を HIGH にしないと電源が切れる (M5.begin が HIGH にする)。電池電圧は G38 の ADC
    (M5.Power.getBatteryLevel)。**USB 給電も充電状態も検出できない** → usbPowered() は常に false
    (USB 接続中も 3 分でスリープする。ユーザー了承済み)
  - スリープ: G4 を gpio_hold で HIGH のまま、バックライト G27 を LOW で保持してディープスリープ。BtnA (G37) を EXT0、
    BtnB (G39) を EXT1 ALL_LOW で起こす (無印 ESP32 は EXT1 の ANY_LOW が無い)。起動時に hw::begin で保持を解除
    (G4 は先に HIGH を出力してから解除)。電源ボタン C は G35
  - ボタン: BtnA = G37, BtnB = G39 (外部プルアップ, 押下 LOW)。ブザー: G2 (LEDC, M5.Speaker は使わない)。
    画面 ST7789V2 135x240, setRotation(1)
  - キー: Grove の G33 (白, ボード定義の SCL) = ストレート/DIT, G32 (黄, SDA) = DAH (ユーザー確認済み。StickS3 と同じ SCL/SDA 位置)
  - USB シリアルは CH9102 (USB-UART, /dev/cu.usbserial-*)。HWCDC と違い setTxTimeoutMs が無く、送信バッファは begin の前に
    設定する (ARDUINO_USB_CDC_ON_BOOT で分岐)。**macOS では USB-UART のポートを閉じると設定が 9600bps に戻る**ので、
    tools/ser.sh は開いた fd に対して stty する。**ポートを開くと本体が再起動する** (2026-11-01 に確認。
    以前は再起動しないように見えた) ので、ser.sh は usbserial のポートでは 2.5 秒待ってからコマンドを送る
  - 2 台目の Plus2 (MAC [Plus2-B], /dev/cu.usbserial-XXXX) も追加。1 台目は [Plus2-A]
    (/dev/cu.usbserial-XXXX)。2 台の Plus2 間でも PARIS 20WPM の再現・デコードを確認
  - 実機: Plus2 ⇔ StickS3 の ESP-NOW 双方向で PARIS 20WPM / CQ TEST 30WPM の DOWN/UP 長を再現 (±1ms)、デコード誤りなし
  - ユーザー確認済み (2026-10-31): 画面の向き・表示、サイドトーン・受信トーン、ボタンの役割 (他機種と同じ)、
    電池で 3 分後のスリープとボタンでの復帰。Grove へのキー・パドル接続 (G33/G32) も 2026-11-01 にユーザーが確認済み
- M5StickC Plus (2026-11-01 調査・実機確認, 2 台):
  - FQBN: `m5stack:esp32:m5stack_stickc_plus` (ビルドディレクトリ build/stickcplus)。ESP32-PICO-D4。電池 120mAh
  - **書き込み速度:** 既定 1500000bps で "The chip stopped responding" になる個体があった ([Plus-B])。750000 でも失敗、
    `:UploadSpeed=115200` で成功 (約 70 秒)。もう 1 台 ([Plus-A]) は既定の速度で書けた
  - PMIC AXP192 (電源・バックライトを管理。M5Unified が扱う)。USB は AXP192 の VBUS に入り、`M5.Power.getVBUSVoltage()` で
    約 5100mV → USB 給電の検出は StickS3 と同じく > 4000mV で判定 (実機で確認)。充電中は isCharging = 1
  - ボタン BtnA = G37, BtnB = G39、ブザー G2、Grove G32 (黄) / G33 (白) は Plus2 と同じ。キーも Plus2 と同じ
    G33 = ストレート/DIT, G32 = DAH (ユーザー確認済み)
  - スリープ: Plus2 と同じく BtnA を EXT0、BtnB を EXT1 ALL_LOW で起こす。電源保持ピンは無い (AXP192 が保つ)。
    バックライトは hw::displaySleep の setBrightness(0) で AXP192 側が消える (電池でのスリープ・消灯・復帰をユーザーが確認済み)
  - power_stickcplus2.cpp を power_stickc.cpp に改名して Plus2 と共用 (#if で違う所だけ分ける)。
    **ファイルを改名したら、そのボードのビルドディレクトリに古い .o が残るので `--clean` でビルドし直すこと**
  - USB シリアルは FTDI 系 (/dev/cu.usbserial-*)。ポートを開くと再起動する点は Plus2 と同じ
  - 実機: Plus ⇔ Plus の ESP-NOW で PARIS 20WPM を再現 (60/180/420ms)、デコード誤りなし、ロス 0.3%。
    MAC [Plus-A] (/dev/cu.usbserial-XXXX), [Plus-B] (/dev/cu.usbserial-XXXX)
  - ユーザー確認済み (2026-11-01): 画面の向き・表示、サイドトーン・受信トーン、ボタン (チャンネル・設定画面・DEFAULTS)、
    電池でのスリープ (画面消灯) とボタンでの復帰、Grove へのキー・パドル接続
- M5Stack Cardputer (初代, 2026-11-01 調査・実機確認, 1 台):
  - FQBN: `m5stack:esp32:m5stack_cardputer` (build/cardputer)。ESP32-S3 (StampS3, 8MB)。USB は USB CDC (HWCDC)。
    **パーティション (2026-11-01 ユーザー依頼で変更):** FQBN に `:FlashSize=8M,PartitionScheme=default_8MB` を付ける
    (アプリ 3.2MB x 2 の OTA 構成、現状 34% 使用)。初代・ADV とも実チップは 8MB (esptool の "Embedded Flash 8MB")。
    **注意:** ボード定義の FlashSize メニューの既定は 4MB なので、PartitionScheme だけ 8MB にするとブートローダの
    ヘッダが 4MB のままになり "partition ... exceeds flash chip size 0x400000" で起動しない (実際に起きた。必ず両方付ける)。
    nvs (0x9000) と otadata (0xE000) の位置は既定と同じなので、変更しても保存した設定は残る (実機で確認)
  - キー: Grove の G1 (白) = ストレート/DIT, G2 (黄) = DAH (ユーザー指定)
  - キーボード (keyboard_cardputer.cpp, 自前で走査。M5Cardputer ライブラリは使っていない): 74HC138 の A0/A1/A2 =
    G8/G9/G11 で 8 本の行を選び、7 本の列 G13/G15/G3/G4/G5/G6/G7 (INPUT_PULLUP, 押下 LOW) を読む。
    行 i, 列 j → x = (i > 3) ? 2j : 2j+1, y = 3 - (i & 3)。キー配列は M5Cardputer の _key_value_map と同じ
    (fn + ; . , / = ↑ ↓ ← →、fn + ` = esc)。loop() から 20ms ごとに走査し、押された瞬間だけ返す (リピートなし)。
    v1.1 / ADV は TCA8418 (I2C) なので非対応
  - キーボードの割り当て (README の表): メインは文字を g_typeBuf に積んで送る (端末モードと同じ StreamKeyer。engine は
    端末モードかどうかに関係なく常に g_typeBuf を送るようにした)、fn+←→ = チャンネル、esc / ` = 設定画面。
    設定画面は ↑↓ (; .) = 項目、←→ (, /) = 値、enter = 値を進める、esc = 抜ける。未送信の文字は TX の行に灰色で表示
  - ボタンは G0 (BtnA) だけで、他機種の KEY2 の役 (短押し = 設定画面 / 長押し = テキスト消去)。設定画面では抜ける
  - 音: NS4168 (I2S) を M5.Speaker で (StickS3 と同じ tone_out_m5speaker.cpp, DMA 64x4)
  - 電源: PMIC 無し。USB 給電は検出できないので `HWCDC::isPlugged()` (PC と USB シリアルで通信中) で代用。
    充電器だけなら電池運用と同じく 3 分で眠る。スリープはバックライト G38 を LOW で gpio_hold し、G0 の EXT1 ANY_LOW で
    復帰 (起動時に hw::begin で保持を解除)。電池電圧は G10 の ADC (M5.Power)
  - 実機: Cardputer ⇔ StickS3 の ESP-NOW 双方向で PARIS / CQ TEST を再現 (±1ms)、デコード誤りなし。MAC [Cardputer]
  - ユーザー確認済み (2026-11-01): 画面の向き・表示、サイドトーン・受信トーン、キーボードでの送信 (灰色の未送信表示・del・
    fn+←→ のチャンネル)、設定画面の矢印・enter・esc (DEFAULTS を含む)、Grove のキー・パドル、電池でのスリープと G0 での復帰
- M5Stack Cardputer ADV (2026-11-01 調査・実機確認, 1 台, MAC [Cardputer-ADV]):
  - ボードパッケージに ADV 専用の FQBN は無い。**初代と同じ `m5stack_cardputer` でビルドし、M5Unified が起動時に
    board_M5CardputerADV と判別する** (M5GFX の probe)。実行時に `M5.getBoard()` で分岐する (hw::hasPhoneJack)
  - キーボード: TCA8418 (内部 I2C = M5.In_I2C, SDA G8 / SCL G9, アドレス 0x34, INT G11 は使わず 20ms ごとに FIFO を読む)。
    KP_GPIO_1/2 = 0x7F/0xFF (7 行 x 8 列)。イベント = 行 r*10 + 列 c + 1、bit7 = 押下。座標 x = 2r + (c > 3), y = c % 4
    (M5Cardputer の TCA8418.cpp と同じ)。押下イベントごとにキーを出す。**ADV では G8/G9 が I2C なので初代のマトリクス走査をしない**
  - 音: ES8311 コーデック + NS4150B アンプ + 3.5mm 端子。M5Unified が ES8311 を設定する (internal_spk)。公式資料:
    "When a 3.5mm headphone jack is inserted, the speaker amplifier will be disabled" → 出口はハードウェアが切り替え、
    ソフトでは検出できない。ユーザーの選択で設定に OUTPUT (SPEAKER / PHONES) を追加し、PHONES はヘッドフォン向けの小さい
    音量テーブル (tone_out_m5speaker.cpp の VOLUME_PHONES)、音量は出口ごとに保存 (Preferences: out, hpvol)。
    OUTPUT 項目は ADV でだけ表示 (ui の itemShown / itemAt / stepItem で表示する項目を機種ごとに絞る)
  - loadSettings は M5.begin (ui::begin) の後に呼ぶようにした (ADV の判別が要るため)
  - Grove (G1 = ストレート/DIT, G2 = DAH)、G0 ボタン、スリープ (G0 で復帰、バックライト G38 を保持) は初代と同じ
  - 実機: ADV ⇔ StickS3 の ESP-NOW 双方向で PARIS / CQ TEST を再現 (±1ms)、デコード誤りなし。`output` コマンドを確認
  - ユーザー確認済み (2026-11-01): 画面、スピーカーの音、ヘッドフォン (差すとスピーカーが止まる・PHONES の音量段階は適切)、
    キーボードでの送信と設定画面の操作、Grove のキー・パドル、電池でのスリープと G0 での復帰
- LoRa (Nesso N1, 2026-10-30 調査・実機確認):
  - SX1262。SPI は画面と共有 (SCK=20, MOSI=21, MISO=22)。CS=GPIO23 (`LORA_CS`), BUSY=GPIO19, DIO1=GPIO15 (`LORA_IRQ`),
    NRST は無し (RADIOLIB_NC)。**画面 (loop) と LoRa タスクが同じ SPI を使うので `hw::lockBus()` で排他している**
  - 制御線は IO エキスパンダ 0x43: P7 = `LORA_ENABLE` (SX1262 のリセット, HIGH で動作), P6 = `LORA_ANTENNA_SWITCH`
    (RF スイッチ FM8625H の電源, HIGH), P5 = `LORA_LNA_ENABLE` (受信 LNA SGM13005L4, HIGH)。送受信の切り替えは DIO2
    (`setDio2AsRfSwitch(true)`)。出典: Nesso N1 データシート §6.2 (docs.arduino.cc/resources/datasheets/TPX00227-datasheet.pdf)
  - TCXO 3.0V (M5Stack のサンプルの値。`radio.begin` が成功することを確認)。ライブラリ RadioLib 7.8.1
  - 受信 LNA の利得は約 21dB (SG Micro の資料, 850MHz)。LNA を切ると雑音の読み値が約 22dB 下がることを実機で確認。
    SX1262 の RSSI はこの分高く出るので、表示とキャリアセンスでは 20dB を差し引く (`LORA_LNA_GAIN_DB`)
  - 机上の雑音レベル (SX1262 の読み値, BW125): -68〜-85 dBm (周波数で変わる。アンテナ端換算で -90〜-105dBm)
  - 実測: 送信時間 約 6〜7ms/パケット、往復 (ping/pong) 26.7ms、休止込みの最短送信間隔 62ms、机上で並べると RSSI は飽和
  - **注意:** 受信側の Serial を誰も読んでいないと Serial 出力が詰まり、試験スケッチの応答が遅れた (本体は
    `Serial.setTxTimeoutMs(0)` なので問題なし)
- 実測したネットワーク遅延・ロス: 2台を机上に並べた状態で RSSI -20〜-50 dBm、ロス 0.2% 程度。
  PARIS@20WPM の DOWN/UP 長は受信側で 60/180/420 ms ぴったりに再現された (ジッタ遅延 150ms, 遅延超過 0)
- 15 分間の双方向連続運転 (2026-10-30, 20WPM と 44WPM のテストモードを同時に送信、机上):
  - 再生された DOWN/UP 長は全遷移で送信側と完全一致 (44WPM: 27/81/189 ms, 20WPM: 60/180/420 ms)
  - デコード誤りはなし。ロス 0.8〜1.1%、再同期・ウォッチドッグ・遅延超過はいずれも 0
  - 時刻オフセットの変動はなし (1ms 未満)。空きヒープは約 236KB で一定 (リークなし)

## 11. 進め方 (推奨フェーズ)

1. 環境確認: ボード・FQBN・ライブラリの調査、`arduino-cli compile` でビルド確認、2台への書き込み確認
2. ホスト側ロジック: `protocol.h` / `jitter.h` / `keyer.h` / `morse_decoder.h` とホストテスト
3. 最小の実機通信: ESP-NOW でパケット送受信、シリアルに統計出力
4. キー入力とトーン (ストレートキー → パドル)
5. 受信再生 (ジッタバッファ、ウォッチドッグ) + ロス注入テスト
6. デコードと画面表示
7. 設定UI・永続化、パラメータ調整 (ジッタ遅延、送信間隔)、整理とREADME

各フェーズの終わりに、ビルドが通りテストが成功する状態でコミットする。

**2026-10-30 時点で全フェーズ完了**。その後の追加機能・移植は §13。

## 12. 作業上のルール

- 不明なハード仕様は推測せず、ドキュメント・ボード定義・実機で確認し、確認できない場合はユーザーに質問する
- 配線に関わる決定 (ピン割り当て) は、ユーザーの確認を取ってから確定する
- パラメータ (間隔、遅延、閾値) は `config.h` に集約し、マジックナンバーを散らさない
- ISR/コールバック内 (ESP-NOW の受信コールバックなど) では重い処理をせず、キューに積んで `loop()` で処理する
- 共有データは競合に注意する (コールバックと `loop()` の間は、キューまたはクリティカルセクションを使う)
- 破壊的操作 (大規模な削除・書き換え) の前にはユーザーに確認する
- コードコメントと README は日本語で良い

## 13. 作業状況と再開メモ (2026-11-01 時点)

### 作業の経緯 (コミット順)
1. Nesso N1 2 台で ESP-NOW の基本機能 (§11 の全フェーズ) → 画面・設定画面の改訂 → オートスリープ → 赤い '?' と [SK]
2. 通信チャンネルと複数局受信、Nesso のボタン割り当て変更 (KEY1 = チャンネル, KEY2 = 設定)
3. M5Stack Tab5 の調査 (ESP-NOW が使えず保留, §14)
4. M5StickS3 対応 (ボード依存部を hw_* / tone_out_* / power_* に分離)
5. LoRa 対応 (Nesso のみ, §4.6)。チャンネル数を 10 → 8 に変更 (ユーザー指示)
6. RADIO OFF (電波を出さない単独練習モード)
7. シリアル出力を US-ASCII のみに (2026-10-31)、端末モード `term` を追加 (ユーザー確認済み)
8. M5StickC Plus2 対応 (2026-10-31, §10)。hw_sticks3.cpp → hw_m5stick.cpp、tone_out_nesso.cpp → tone_out_buzzer.cpp に共通化。
   2026-11-01 にキー・パドル接続を含めてユーザーが全項目を確認済み
9. (2026-11-01) 設定画面の順番変更、シリアルの CR LF・プロンプト・エコーバック・help の 1 行 1 コマンド化
10. (2026-11-01) 設定を既定値に戻す DEFAULTS 項目と `defaults` コマンド、help の term の説明を短縮 (80 桁以内)
11. (2026-11-01) M5StickC Plus 対応 (power_stickcplus2.cpp → power_stickc.cpp に共通化)
12. (2026-11-01) Cardputer (初代) 対応、キーボードでの送信 (tone_out_sticks3.cpp → tone_out_m5speaker.cpp に共通化)
13. (2026-11-01) Cardputer ADV 対応 (TCA8418 キーボード、OUTPUT = SPEAKER / PHONES と出口ごとの音量)

### 現在の状態 (2026-11-01)
- 3 機種 (Nesso N1 ×2 / M5StickS3 / M5StickC Plus2 ×2) すべてに最新版 (10 まで) を書き込み、ユーザーが実機で動作確認済み
- M5StickC Plus (旧モデル, AXP192) へ移植済み (2026-11-01, 2 台。§10)。全項目をユーザーが確認済み
- 6 機種 (Nesso N1 / M5StickS3 / M5StickC Plus2 / M5StickC Plus / Cardputer / Cardputer ADV) すべてユーザーが実機で動作確認済み

### 次にやること (ユーザー予定)
- LoRa を距離のある場所で試験する (ユーザーが後日実施)。結果 (最上段の dBm / L / R / W、デコードの具合) を聞いて、
  必要ならジッタ遅延 (LoRa 既定 250ms)・SF・帯域などを調整する。SF/BW を変えると送信時間が変わるので、
  休止 50ms の条件と送信間隔・ジッタ遅延の関係 (§4.6) を見直すこと

### 実装済み (すべて実機で確認済み、ホストテスト 124 件合格、git にコミット済み)
- §4 の通信方式どおり: 状態+遷移履歴の定期送信、tid による重複/逆順対応、再同期、ジッタバッファ (150ms)、
  時刻オフセット = 直近 4 秒の (rx - tx) 最小値、ウォッチドッグ (再生時間軸で「最新パケットの送信時刻 + 100ms」を
  越えて DOWN なら強制 UP。同じ tid で DOWN のパケットが来たら REASSERT で再開)
- キー: ストレート / アイアンビック A・B (A=スペース終了時の状態で判定、B=要素中の反対パドルをラッチ)、
  ロックアウト型デバウンス 4ms、パドル入れ替え
- タスク構成: engine.cpp の 1ms 周期タスク (優先度 5) がキー〜送受信〜トーン〜デコード、loop() が画面・I2C・シリアル。
  共有データは shared.h + portMUX クリティカルセクション、文字/ログはキュー
- トーン: サイドトーン (ON/OFF・周波数) と受信トーン (周波数) を別設定。同時 DOWN はサイドトーン優先。
  設定変更時に 150ms の試聴音
- 画面 (ユーザー要望で改訂済み):
  - メイン画面: 最上段にリンク状態 (dBm・loss・R再同期・Wウォッチドッグ、Font2)、右端に電池 % (+充電中)。
    RX 3 行 (緑) / TX 2 行 (黄)、文字は Font0 x2
  - メイン画面のボタン (2026-10-30 にユーザー指示で変更): KEY1 短押し = チャンネル +1 (循環)、KEY1 長押し = チャンネル -1、
    KEY2 短押し = 設定画面、KEY2 長押し = テキスト消去。チャンネル切り替え時は中央に "CH n" を 1.2 秒大きく表示
    (ユーザーが実機で確認済み)
  - 最上段: [CHn] x局数(2局以上) dBm L% R W ... 電池
  - 設定画面: KEY2 短押しで入る。項目はタッチまたは KEY1(次)/KEY2(前) で選び、右側のタッチ [▲]/[▼] で変更 (2026-11-01 ユーザー要望で + / - から図形の三角に変更。ユーザー確認済み)
    (押し続けで連続)。KEY1/KEY2 長押し または 30 秒無操作で抜けて保存。
    項目 (表示順, 2026-11-01 ユーザー指定): RADIO / MODE / PADDLE / SPEED / SIDETONE / VOLUME / SIDE FREQ / RX FREQ /
    DEFAULTS (ui.h の enum Item の並び = 表示順。チャンネルは設定画面に無い)。
    DEFAULTS (2026-11-01 ユーザー要望): +/- を 1 回押すと "PRESS AGAIN"、3 秒 (DEFAULTS_CONFIRM_MS) 以内の 2 回目で
    restoreDefaults() (Preferences の名前空間 "morse" を clear して shared.h の Settings() 既定値を保存)。
    タッチの押し続け (TouchEvent::repeat) では実行しない。既定値はユーザー指定: ESP-NOW, CH1, IAMBIC_B, NORMAL, 20WPM,
    SIDETONE ON, VOL 3, 700Hz / 600Hz。シリアルは `defaults` (確認なし、無線が変われば再起動)
    ユーザーが Nesso で DEFAULTS (2 回押しでの実行・1 回押し後 3 秒での取り消し) と `defaults` コマンドを確認済み (2026-11-01)。
    StickS3・StickC Plus2 (2 台) でも設定画面の新しい順番と DEFAULTS をユーザーが確認済み (2026-11-01)。
    注意: loop() の `now` はループ先頭の時刻なので、ループ途中で millis() で記録した時刻との差は符号付きで比べること
    (符号無しだと負の差が巨大な値になる。DEFAULTS が押した直後に取り消される不具合の原因だった)
    8 項目は画面 (7 行) に収まらないので、選択中の項目が見えるようにスクロールする (ui.cpp の listTop)
  - RADIO: Nesso は ESP-NOW → LoRa → OFF、StickS3 / StickC Plus2 は ESP-NOW ↔ OFF。動作中と違う値には " *" を付け、
    設定画面を抜けたときに保存して再起動する (MorseComm.ino の restartIfRadioChanged)
  - 最上段の左端: ESP-NOW = 青地 "CHn"、LoRa = 紫地 "LoRan"、OFF = 灰地 "OFF" + "RADIO OFF"
- デコード: 解読できない符号は CHAR_UNKNOWN (0x01) として出し、画面では赤い '?'。
  略符号は morse_table.h の prosignTable (現在 `...-.-` → "[SK]" のみ。AR/BT は + / = のままで良いとユーザー確認済み)
- オートスリープ: 電池運用時 (VIN_DETECT=LOW)、キー・ボタン/タッチ・受信の遷移がどれも 3 分間なければ
  ディープスリープ (画面消灯)。10 秒前から "SLEEP in Ns" を表示。KEY1/KEY2 で復帰 (再起動、押したボタンは無視)。
  USB 給電中は眠らない (StickC Plus2 は USB 給電を検出できないので USB 接続中も眠る。BtnA/BtnB で復帰)。
  3 機種ともユーザーが電池で動作確認済み
- チャンネル: パケットの channel バイト (1..8, config.h の CH_*。2026-10-30 にユーザー指示で 10 → 8)。Preferences に保存。切り替えると受信中の全局を忘れる。
  実機 2 台で、同チャンネルのみ受信・別チャンネルは受信しないことを確認済み。3 局以上はホストテストのみ (実機 2 台しかない)
- シリアル出力 (2026-10-31, ユーザー要望): **US-ASCII のみ** (help も英語)。日本語を含む長い出力がターミナルで途中で
  切れていた原因は、USB CDC (HWCDC) の送信バッファ既定 256 バイト + `setTxTimeoutMs(0)` で入りきらない分が捨てられること。
  `Serial.setTxBufferSize(4096)` で対処 (SERIAL_TX_BUFFER)。シリアルに出す文字列に日本語を入れないこと。
  **改行は CR LF** (2026-11-01 ユーザー要望。文字列中は "\r\n" と書く。Serial.println は使わなくてよい)
- コマンド入力 (2026-11-01 ユーザー要望): プロンプト "morse> " を出し、打った文字をエコーバック。BS で 1 文字削除、
  Ctrl-C で行を捨てる、CR LF は 1 回の改行。ログ・統計などの非同期出力は asyncOut() を先に呼んで行を区切り、
  出力が 300ms 止んだら loop() がプロンプトと打ちかけの文字を出し直す。help は 1 行 1 コマンド (列をそろえる)
- 端末モード `term` (2026-10-31, ユーザー要望): PC のターミナルで打った文字を送り、受信文字を表示する。
  loop が `g_typeBuf` (keyer.h の TypeBuffer, lockShared で保護) に積み、engine の StreamKeyer が 1 単位ずつ
  (1 文字 / "[SK]" 略符号 / 空白 = 語間) 設定の WPM で送る。前の文字の終わりから文字間 3 dit (語間 7 dit)。
  送信中はキー・パドル無視。エコーは即時、行頭 "> " (打った文字) / "< " (受信)。BS は未送信分だけ取り消し、
  符号の無い文字は BEL。Ctrl-C / Ctrl-D で終了。端末モード中は log と "settings saved" を出さない。
  StickS3 (RADIO OFF) でエコー・BS・終了を確認。ユーザーが端末モードの動作を確認済み (2 台間の確認はまだ)
- シリアルコマンド: ch N / radio espnow|lora|off / term / defaults / output speaker|phones (ADV) / mode / wpm / side N|on|off / rxtone / vol / swap / jitter /
  test on [TEXT] (`[SK]` で文字間なし送信) / test off / loss N / dropup 0|1 / log 0|1 / stats / show / clear / save /
  sleep / help (README に一覧)。`stats` の `|` 以降は送信側 (無線の種類, sent, csBusy, air など)
- 設定は Preferences (名前空間 "morse") に保存。キー: mode wpm sideon sidehz rxhz vol swap ch radio、
  ジッタ遅延は無線ごとに jitter (ESP-NOW, 既定 150) / ljitter (LoRa, 既定 250)。
  実機 (2026-11-01 の状態): いずれも ESP-NOW・CH1。Nesso 101 = IAMBIC_B, Nesso 1101 = STRAIGHT, StickS3 = IAMBIC_B,
  Plus2 ([Plus2-A]) = IAMBIC_B 22WPM, Plus2 ([Plus2-B]) = IAMBIC_B 20WPM。設定はユーザーがよく変えるので、試験前に `show` で
  記録し、試験で変えたら元に戻すこと (ポート番号は接続のたびに変わる)
- M5StickS3 対応 (2026-10-30): 上記の機能はすべて共通。設定画面はタッチが無いので選択/変更モード
  (選択: A=次, B=変更開始, 長押し=終了 / 変更: A=+, B=-, 長押し=選択へ戻る)。
  ユーザーが StickS3 の設定画面で RADIO を切り替えて OFF モードを確認済み。StickS3 ⇔ Nesso の ESP-NOW 通信も確認済み

- LoRa 対応 (2026-10-30, §4.6): Nesso 2 台で実機確認済み。PARIS 20/35/50WPM の DOWN/UP 長を受信側で完全に再現
  (60/180/420, 34/102/238, 24/72/168ms)、デコード誤りなし、ロス 0.5〜1%。双方向同時送信 (25WPM と 30WPM) でも
  再現・デコードとも誤りなし。チャンネル (周波数) の分離・切り替え、ESP-NOW ⇔ LoRa の切り替えも確認。
  ロス注入 30%: ジッタ 250ms で遅延超過 5 回・デコード誤り 2 語 (13 語中)、150ms では 25 回・4 語 → 既定 250ms。
  設定画面の RADIO 項目とスクロールはユーザーが実機で確認済み。チャンネル数は 8 (ユーザー指示) で、ESP-NOW・LoRa とも
  全 8 チャンネルで送受信できることをユーザーが確認済み (2026-10-30)。LoRa モードでの電池スリープ・ボタン復帰もユーザーが
  確認済み。自宅内では問題なく通信できることをユーザーが確認済み。より遠距離の試験は後日 (未実施)
- RADIO OFF (2026-10-30): StickS3・Nesso とも OFF モード (画面・操作) をユーザーが確認済み。送信 0 もシリアルで確認。
  StickS3 と Nesso の ESP-NOW での相互通信はユーザーが確認済み

### 実機テストの手順 (再開時に使う)
- **書き込みは sketch.yaml のプロファイルを使う** (2026-11-01 ユーザー要望で作成。プロファイル: nesson1 / sticks3 /
  stickcplus2 / stickcplus / cardputer。ボードパッケージ・ライブラリのバージョンとボードのオプションを固定してある):
  `arduino-cli compile --profile <名前> --build-path build/<名前> -u -p PORT .` (全プロファイルのビルドと cardputer の
  書き込みを確認済み)。ライブラリやボードパッケージを更新したら sketch.yaml のバージョンも直すこと。
  sketch.yaml.sample はユーザーが参考に置いた別プロジェクトのもの (リポジトリには入れていない)
- 以下は FQBN を直接指定する場合 (プロファイルと同じ内容。ボードごとにビルドディレクトリを分ける):
  - Nesso N1: `arduino-cli compile -b esp32:esp32:arduino_nesso_n1 --build-path build/nesso -u -p PORT .`
  - M5StickS3: `arduino-cli compile -b m5stack:esp32:m5stack_sticks3 --build-path build/sticks3 -u -p PORT .`
  - M5StickC Plus2: `arduino-cli compile -b m5stack:esp32:m5stack_stickc_plus2 --build-path build/stickcplus2 -u -p PORT .`
  - M5StickC Plus: `arduino-cli compile -b m5stack:esp32:m5stack_stickc_plus:UploadSpeed=115200 --build-path build/stickcplus -u -p PORT .`
  - Cardputer / ADV: `arduino-cli compile -b m5stack:esp32:m5stack_cardputer:FlashSize=8M,PartitionScheme=default_8MB --build-path build/cardputer -u -p PORT .`
    (ポートは /dev/cu.usbserial-*。Nesso / StickS3 は /dev/cu.usbmodem*)
  - 接続されている機器はセッションによって変わるので、`show` の出力 (power: 行の形式や mode) で判別する
- シリアル操作: `tools/ser.sh PORT 秒数 "cmd1" "cmd2"` でコマンド送信と出力の取得ができる
  (ポートを開いたまま `stty 115200 raw -echo clocal` してからコマンドを書き込み、`cat` で読む。
  `stty -f PORT` で先に設定すると、USB-UART (Plus2) は閉じた時点で 9600bps に戻ってしまう)
- 端末モードの試験: ser.sh は 1 行ずつ送るので、生の文字 (BS, Ctrl-C) を送るには Python で termios を設定して書き込む
- 典型的な確認: 送信側 `test on PARIS`、受信側 `log 1` → `RX D t=.. prevU=60` 行と `RXCHR` 行を集計。
  ロス注入は送信側 `loss 50` / `dropup 1`
- LoRa の確認: 両方の Nesso で `radio lora` (再起動する) → 同じ手順。`stats` の csBusy (キャリアセンスで待った回数)、
  air (送信時間の合計) も見る。終わったら必要に応じて `radio espnow` で戻す
- 注意: 試験で `wpm` などを変えたら元に戻す (ユーザーの設定が Preferences に保存されている)
- LoRa の調査用に使った単体スケッチ (雑音スキャン、ping/pong、連続送信) はリポジトリに入れていない
  (セッションの一時ディレクトリにあった)。必要なら net_lora.cpp を参考に作り直す
- ホストテスト: `make -C test/host test`

### 既知の制限・今後の候補 (ユーザー未依頼)
- UP のパケットが全部落ちる極端な条件では、語末の要素がウォッチドッグぶん長くなる (例: dit 60→141ms)。仕様上の限界
- 距離を離した実運用 (ESP-NOW): ユーザーが「少し離れたところ」で問題なく通信できることを確認済み。遠距離での統計
  (R/W 回数) を見てジッタ遅延 150ms を調整する余地あり
- ペアリング (ユニキャスト) は未対応 (ブロードキャスト + channel で十分という判断)
- 同時 DOWN 時の音の区別 (2 音同時発音) は未対応
- スリープまでの時間は config.h の定数のみ (設定画面には無い)
- LoRa: 距離を離した試験 (到達距離・ロス率・ジッタ遅延の調整) はユーザーが後日実施予定。ESP-NOW / OFF 時の SX1262 は
  初期化していない (以前と同じ状態)。LoRa から切り替えたときは再起動前に SX1262 をスリープさせている
- LoRa: キー操作から相手の音まで約 0.26 秒 (ジッタ遅延 250ms)。ESP-NOW (約 0.15 秒) より遅い。ロスが 4 回以上続くと
  遅延超過でデコードを誤ることがある。半二重 (送信中の約 6ms は受信できない)
- LoRa: キャリアセンスレベル -80dBm は記憶による値 (原文未確認)。LNA 利得 20dB の補正も資料値ベース (実測で厳密には未校正)
- ESP-NOW の局と LoRa の局は相互に通信できない (全員が同じ無線の種類・チャンネルに合わせる)

## 14. 他機種への移植: M5Stack Tab5 (保留中, 2026-10-30)

ユーザーの要望: Tab5 (https://docs.m5stack.com/en/core/Tab5) でも使えるようにしたい。広い画面・物理ボタンなし (タッチ操作)、
キーは Grove ポート (4番ピン = GPIO54, 3番ピン = GPIO53。ユーザー申告)、スピーカーに加えてヘッドセット端子も使いたい。
Tab5 にはバッテリーが付いていない (バッテリーテストは後日)。

**ESP-NOW が使えないため保留** (ユーザー判断)。調査結果:
- Tab5 のメイン MCU は ESP32-P4 (無線なし、revision v1.0 → ボード設定 ChipVariant = "Before v3.00")。
  無線は内蔵 ESP32-C6 コプロセッサを esp-hosted (SDIO) 経由で使う
- FQBN: `esp32:esp32:m5stack_tab5:USBMode=hwcdc,CDCOnBoot=cdc` (USB シリアルを使うために必要)。
  variant: SDA=53/SCL=54 (= Grove)、パーティション既定 6.5MB アプリ
- Arduino コア 3.3.12 の P4 ライブラリ (esp_hosted 2.12.13, esp_wifi_remote 1.6.4) には esp_now_* の実装が無い
  (ヘッダだけ)。Arduino の ESP_NOW サンプルも `CONFIG_SOC_WIFI_SUPPORTED` 必須
- Tab5 内蔵 C6 のファームは esp-hosted **1.4.1** (`esp_hosted_get_coprocessor_fwversion` で確認)。Wi-Fi STA は動作
- 回避策 (未実施): C6 を ESP-NOW 拡張入りの esp-hosted 2.x に書き換え、P4 側で esp_now_* を CustomRpc
  (`esp_hosted_send_custom_data` / `esp_hosted_register_custom_callback`, 2.12.13 に存在) で転送するシム実装。
  参考: ESPHome PR https://github.com/esphome/esphome/pull/17712 (Tab5 で検証済み)、
  https://github.com/esphome/esp-hosted-firmware 、https://github.com/tymorton/esp32-p4-c6-espnow-enabler 。
  公式対応は https://github.com/espressif/esp-hosted-mcu/issues/19 (未対応)。
  リスク: 1.4.1 からの C6 OTA が通るか不明 (失敗時は P4 ブートローダを書き換える手段しかない)
- 別案: 外付けの ESP32 モジュールを UART ブリッジにする (追加購入が必要)
- 再開時は、ユーザーにどの方式で進めるかを確認すること
- 注意: Tab5 には調査用の小さなスケッチを書き込んだ (工場出荷のデモは消えている。M5Burner 等で戻せる)
- 移植時の構成方針 (案): ロジック部 (protocol/jitter/keyer/decoder/peers) はそのまま共有し、ハード依存部
  (tone_out, net, ui, power, ピン) をボードごとに `#if defined(ARDUINO_NESSO_N1)` / `ARDUINO_M5STACK_TAB5` で切り替える

