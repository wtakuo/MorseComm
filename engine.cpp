#include "engine.h"
#include <Arduino.h>
#include <esp_random.h>
#include <string.h>
#include "board.h"
#include "config.h"
#include "protocol.h"
#include "morse_decoder.h"
#include "peers.h"
#include "tone_out.h"

namespace engine {
namespace {

constexpr int OUT_QUEUE_LEN = 128;
constexpr UBaseType_t TASK_PRIORITY = 5;  // loop() (1) より高く、Wi-Fi タスクより低い
constexpr uint32_t TASK_STACK = 6144;
constexpr uint32_t SNAPSHOT_INTERVAL_MS = 50;

QueueHandle_t outQ = nullptr;
ToneOut toneOut;

cw::Debouncer dbStraight, dbDit, dbDah;
cw::IambicKeyer iambic;
cw::TextKeyer textKeyer;
cw::StreamKeyer streamKeyer;  // 端末モード
cw::TxHistory hist;
cw::TxScheduler sched;
cw::PeerTable peers;
cw::MorseDecoder decLocal;

Settings set;
Control ctl;
uint32_t setVer = 0xFFFFFFFF, ctlVer = 0xFFFFFFFF;
bool logOn = false;

uint16_t pktSeq = 0;
uint32_t txPackets = 0;
uint8_t localState = 0;
uint32_t localLastMs = 0;
uint32_t lastSnapshotMs = 0;
uint32_t lastActivityMs = 0;

uint16_t curHz = 0;
uint16_t previewHz = 0;
uint32_t previewUntil = 0;

void out(const OutItem& it) { xQueueSend(outQ, &it, 0); }

void preview(uint16_t hz, uint32_t now) {
  previewHz = hz;
  previewUntil = now + cfg::TONE_PREVIEW_MS;
}

// トーン: 自局 (サイドトーン) と受信再生の OR。周波数は自局優先
void updateTone(uint32_t now) {
  bool side = localState && set.sideOn;
  bool remote = peers.anyDown();
  bool prev = previewHz && (int32_t)(now - previewUntil) < 0;
  if (!prev) previewHz = 0;
  if (!side && !remote && !prev) {
    toneOut.off();
    return;
  }
  uint16_t hz = side ? set.sideHz : remote ? set.rxHz : previewHz;
  if (hz != curHz) {
    toneOut.setFrequency(hz);
    curHz = hz;
  }
  toneOut.on();
}

// 自局のキー状態を変える (どのモードでもここを通る)
void setLocal(uint8_t st, uint32_t t) {
  if (!hist.push(st, t)) return;
  sched.onTransition(st, t);
  decLocal.onTransition(st, t);
  if (logOn) out(OutItem{OutItem::LogLocal, 0, st, 0, t, t - localLastMs});
  localLastMs = t;
  localState = st;
  lastActivityMs = millis();
}

void applySettings(uint32_t now, bool modeChanged) {
  iambic.setWpm(set.wpm);
  streamKeyer.setWpm(set.wpm);
  iambic.setModeB(set.mode == (uint8_t)cw::KeyMode::IambicB);
  toneOut.setVolume(set.volume);
  peers.setDelay(set.jitterMs);
  if (modeChanged) {
    iambic.reset();
    setLocal(0, now);
  }
  if (ctl.testOn) textKeyer.start(ctl.testText, set.wpm, now);
}

void syncShared(uint32_t now) {
  lockShared();
  bool sChanged = g_settingsVer != setVer;
  bool cChanged = g_controlVer != ctlVer;
  uint8_t oldMode = set.mode;
  Settings oldSet = set;
  Control oldCtl = ctl;
  if (sChanged) {
    set = g_settings;
    setVer = g_settingsVer;
  }
  if (cChanged) {
    ctl = g_control;
    ctlVer = g_controlVer;
  }
  logOn = g_logOn;
  unlockShared();

  if (sChanged) {
    applySettings(now, set.mode != oldMode);
    if (set.channel != oldSet.channel) {
      peers.clear();  // 別チャンネルの局の再生は打ち切る
      net::setChannel(set.channel);
    }
    // 音に関わる設定を変えたら、その音を短く鳴らして確認できるようにする
    if (set.sideHz != oldSet.sideHz || (set.sideOn && !oldSet.sideOn)) preview(set.sideHz, now);
    else if (set.rxHz != oldSet.rxHz || set.volume != oldSet.volume) preview(set.rxHz, now);
  }
  if (!cChanged) return;
  net::setLossInjection(ctl.lossPct, ctl.dropUp);
  if (ctl.testOn && (!oldCtl.testOn || strcmp(ctl.testText, oldCtl.testText) != 0)) {
    textKeyer.start(ctl.testText, set.wpm, now);
  } else if (!ctl.testOn && oldCtl.testOn) {
    textKeyer.stop();
    setLocal(0, now);
  }
  if (ctl.termOn != oldCtl.termOn) {
    streamKeyer.reset();
    setLocal(0, now);
  }
}

// 端末モード: 打たれた文字を 1 単位ずつ送る。送信中 (文字間の待ちを含む) なら true
bool streamKeys(uint32_t now) {
  char unit[cw::TypeBuffer::MAX_UNIT + 1];
  while (streamKeyer.ready()) {
    lockShared();
    bool got = g_typeBuf.takeUnit(unit);
    unlockShared();
    if (!got) break;
    streamKeyer.send(unit, now);
  }
  streamKeyer.update(now);
  cw::KeyEvent ev;
  while (streamKeyer.pop(ev)) setLocal(ev.state, ev.t);
  return !streamKeyer.ready();
}

void readKeys(uint32_t now) {
  // 打った文字 (シリアルの端末モード / Cardputer のキーボード) を送っている間はキー・パドルを無視する
  // (送っていなければ普通に使える)
  if (streamKeys(now)) return;
  if (ctl.testOn) {
    textKeyer.update(now);
    cw::KeyEvent ev;
    while (textKeyer.pop(ev)) setLocal(ev.state, ev.t);
    return;
  }
  if (set.mode == (uint8_t)cw::KeyMode::Straight) {
    bool k = dbStraight.update(digitalRead(board::PIN_STRAIGHT) == LOW, now);
    if (dbStraight.takeChanged()) setLocal(k, dbStraight.lastChangeMs());
    return;
  }
  bool a = dbDit.update(digitalRead(board::PIN_PADDLE_DIT) == LOW, now);
  bool b = dbDah.update(digitalRead(board::PIN_PADDLE_DAH) == LOW, now);
  if (set.swap) iambic.update(now, b, a);
  else iambic.update(now, a, b);
  cw::KeyEvent ev;
  while (iambic.pop(ev)) setLocal(ev.state, ev.t);
}

void transmit(uint32_t now) {
  if (!sched.poll(now)) return;
  cw::Packet p;
  hist.build(p, now, cfg::HIST_N, set.channel, pktSeq++);
  txPackets++;
  uint8_t buf[cw::PKT_MAX_SIZE];
  size_t len = cw::encodePacket(p, buf, sizeof(buf));
  if (len) net::send(buf, len);
}

void receive(uint32_t now) {
  net::RxItem item;
  while (net::receive(item)) {
    cw::Packet p;
    if (!cw::decodePacket(item.data, item.len, p) || p.channel != set.channel) continue;
    int idx;
    cw::Peer& peer = peers.get(item.src, item.rxMs, idx);
    peer.rssi = item.rssi;
    peer.lastRxMs = item.rxMs;
    peer.rx.onPacket(p, item.rxMs);
  }
  for (int i = 0; i < cw::PeerTable::MAX; i++) {
    cw::Peer& peer = peers.at(i);
    if (!peer.used) continue;
    peer.rx.update(now);
    cw::PlayedEvent ev;
    while (peer.rx.pop(ev)) {
      peer.dec.onTransition(ev.state, ev.playMs);
      if (logOn) {
        OutItem it{OutItem::LogRemote, 0, ev.state, (uint8_t)ev.kind, ev.playMs, ev.playMs - peer.lastEventMs};
        it.peer = (uint8_t)i;
        out(it);
      }
      peer.lastEventMs = ev.playMs;
      peer.lastPlayMs = now;
      lastActivityMs = now;
    }
  }
}

void decode(uint32_t now) {
  char c;
  decLocal.update(now);
  while (decLocal.pop(c)) out(OutItem{OutItem::CharTx, c, 0, 0, now, 0});
  for (int i = 0; i < cw::PeerTable::MAX; i++) {
    cw::Peer& peer = peers.at(i);
    if (!peer.used) continue;
    peer.dec.update(now);
    while (peer.dec.pop(c)) {
      OutItem it{OutItem::CharRx, c, 0, 0, now, 0};
      it.peer = (uint8_t)i;
      out(it);
    }
  }
}

void publishSnapshot(uint32_t now) {
  if (now - lastSnapshotMs < SNAPSHOT_INTERVAL_MS) return;
  lastSnapshotMs = now;
  Snapshot s;
  s.rx = peers.totalStats();
  s.net = net::stats();
  if (const cw::Peer* p = peers.latestRx()) {
    s.offset = p->rx.offset();
    s.rssi = p->rssi;
  }
  if (const cw::Peer* p = peers.latestPlay()) s.remoteWpm = p->dec.wpm();
  s.stations = peers.activeCount(now);
  s.linkUp = s.stations > 0;
  s.localDown = localState;
  s.remoteDown = peers.anyDown();
  s.localWpm = decLocal.wpm();
  s.txPackets = txPackets;
  s.injectedDrops = s.net.injected;
  s.lastActivityMs = lastActivityMs;
  s.nowMs = now;
  lockShared();
  g_snapshot = s;
  unlockShared();
}

void task(void*) {
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    uint32_t now = millis();
    syncShared(now);
    readKeys(now);
    transmit(now);
    receive(now);
    updateTone(now);
    decode(now);
    publishSnapshot(now);
    vTaskDelayUntil(&last, 1);
  }
}

}  // namespace

bool begin() {
  outQ = xQueueCreate(OUT_QUEUE_LEN, sizeof(OutItem));
  pinMode(board::PIN_STRAIGHT, INPUT_PULLUP);
  pinMode(board::PIN_PADDLE_DIT, INPUT_PULLUP);
  pinMode(board::PIN_PADDLE_DAH, INPUT_PULLUP);
  toneOut.begin(cfg::SIDE_HZ_DEFAULT);
  curHz = cfg::SIDE_HZ_DEFAULT;
  hist = cw::TxHistory((uint16_t)esp_random());
  pktSeq = (uint16_t)esp_random();
  lockShared();
  Settings s0 = g_settings;
  unlockShared();
  bool ok = net::begin(s0.radio, s0.channel);
  // LoRa は送信できる間隔が長い (約 60ms) ので、アイドル時のハートビートも間引く
  if (net::radio() == cfg::RADIO_LORA) sched.setIdleInterval(cfg::LORA_TX_INTERVAL_IDLE_MS);
  xTaskCreate(task, "cw", TASK_STACK, nullptr, TASK_PRIORITY, nullptr);
  return ok;
}

bool popOut(OutItem& item) { return outQ && xQueueReceive(outQ, &item, 0) == pdTRUE; }

}  // namespace engine
