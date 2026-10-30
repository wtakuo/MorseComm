// engine.h — キーイングタスク: キー入力・キーヤー・送信・受信再生・トーン・デコードを 1ms 周期で回す
#pragma once
#include "shared.h"

namespace engine {

// g_settings / g_control を設定してから呼ぶ
bool begin();
// デコード文字・ログを取り出す (loop() から呼ぶ)
bool popOut(OutItem& item);

}  // namespace engine
