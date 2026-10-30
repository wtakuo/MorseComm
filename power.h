// power.h — 給電状態の検出とディープスリープ (Nesso N1)
// I2C (IO エキスパンダ) を使うので loop() 側からのみ呼ぶこと。
#pragma once
#include <stdint.h>

namespace power {

// USB (VIN) から給電されているか
bool usbPowered();
// 復帰要因がボタンだったか (起動直後に呼ぶ)
bool wokeFromSleep();
// 割り込み線 SYS_IRQ の状態など (診断用)
void printStatus();
// 画面を消し、ボタン (KEY1/KEY2) で復帰するディープスリープに入る。戻らない
// (割り込み線が既に Low で入れなかった場合のみ false を返す)
bool enterDeepSleep();

}  // namespace power
