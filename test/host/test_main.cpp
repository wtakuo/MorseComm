// ホスト側ユニットテスト・損失シミュレーション (§9.1)
// ビルド: make -C test/host
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>
#include <algorithm>

#include "../../protocol.h"
#include "../../jitter.h"
#include "../../keyer.h"
#include "../../morse_decoder.h"

using namespace cw;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, ...)                                       \
  do {                                                         \
    if (cond) {                                                \
      g_pass++;                                                \
    } else {                                                   \
      g_fail++;                                                \
      printf("  FAIL %s:%d: %s  ", __FILE__, __LINE__, #cond); \
      printf(__VA_ARGS__);                                     \
      printf("\n");                                            \
    }                                                          \
  } while (0)

struct Tr {
  uint32_t t;
  uint8_t s;
};

// ---- デコーダに遷移列を流す ----
static std::string decodeTimeline(const std::vector<Tr>& tl, int jitterMs = 0, unsigned seed = 1) {
  MorseDecoder d;
  std::mt19937 rng(seed);
  std::uniform_int_distribution<int> J(-jitterMs, jitterMs);
  std::string s;
  char c;
  for (auto& e : tl) {
    uint32_t t = e.t + (jitterMs ? J(rng) : 0);
    d.update(t);
    d.onTransition(e.s, t);
    while (d.pop(c)) s += c;
  }
  if (!tl.empty()) {
    uint32_t end = tl.back().t + 3000;
    for (uint32_t t = tl.back().t; t <= end; t += 10) d.update(t);
  }
  while (d.pop(c)) s += c;
  return s;
}

// =================================================================
static void testProtocolRoundTrip() {
  printf("[protocol] round trip\n");
  Packet p;
  p.group = 0x37;
  p.state = 1;
  p.pktSeq = 0xBEEF;
  p.lastTid = 0xFFFE;
  p.txMs = 0xDEADBEEF;
  p.n = cfg::HIST_MAX;
  for (int i = 0; i < p.n; i++) p.age[i] = (uint16_t)(i * 1000 + 7);
  uint8_t buf[PKT_MAX_SIZE];
  size_t len = encodePacket(p, buf, sizeof(buf));
  CHECK(len == PKT_HEADER_SIZE + 2 * cfg::HIST_MAX, "len=%zu", len);
  CHECK(buf[0] == 0xC7 && buf[1] == 1 && buf[4] == 0xEF && buf[5] == 0xBE, "header bytes");
  Packet q;
  CHECK(decodePacket(buf, len, q), "decode");
  CHECK(q.group == p.group && q.state == p.state && q.pktSeq == p.pktSeq && q.lastTid == p.lastTid &&
            q.txMs == p.txMs && q.n == p.n,
        "fields");
  CHECK(memcmp(q.age, p.age, sizeof(uint16_t) * p.n) == 0, "ages");
  CHECK(!decodePacket(buf, len - 1, q), "truncated must fail");
  buf[0] = 0;
  CHECK(!decodePacket(buf, len, q), "bad magic must fail");
  CHECK(encodePacket(p, buf, 10) == 0, "small buffer");

  TxHistory h(100);
  h.push(1, 1000);
  h.push(0, 1060);
  h.push(1, 1120);
  h.push(1, 1130);  // 同じ状態は無視
  h.build(p, 1200, cfg::HIST_N, 1, 5);
  CHECK(p.state == 1 && p.lastTid == 103 && p.n == 3, "n=%d tid=%d", p.n, p.lastTid);
  CHECK(p.age[0] == 80 && p.age[1] == 140 && p.age[2] == 200, "ages %d %d %d", p.age[0], p.age[1], p.age[2]);
  CHECK(p.stateOf(0) == 1 && p.stateOf(1) == 0 && p.stateOf(2) == 1, "stateOf");
  h.build(p, 1000 + 70000, cfg::HIST_N, 1, 6);
  CHECK(p.age[2] == 0xFFFF, "age saturates");
}

static void testScheduler() {
  printf("[protocol] tx scheduler\n");
  TxScheduler s;
  int sent = 0;
  // アイドル: 500ms ごと
  for (uint32_t t = 1000; t < 3000; t++) sent += s.poll(t);
  CHECK(sent >= 4 && sent <= 6, "idle sent=%d", sent);
  // DOWN 300ms: 遷移で即時 + 20ms ごと
  s.onTransition(1, 3000);
  CHECK(s.poll(3000), "immediate on transition");
  sent = 1;
  for (uint32_t t = 3001; t < 3300; t++) sent += s.poll(t);
  CHECK(sent >= 15 && sent <= 16, "down sent=%d", sent);
  s.onTransition(0, 3300);
  sent = 0;
  for (uint32_t t = 3300; t < 3800; t++) sent += s.poll(t);
  CHECK(sent >= 25 && sent <= 26, "hold sent=%d", sent);
  sent = 0;
  for (uint32_t t = 3800; t < 5800; t++) sent += s.poll(t);
  CHECK(sent >= 4 && sent <= 5, "idle again sent=%d", sent);
}

// =================================================================
// キーヤー
static std::vector<Tr> runIambic(bool modeB, int wpm, const std::vector<std::pair<uint32_t, int>>& paddle,
                                 uint32_t endMs) {
  // paddle: (時刻, ビット0=dit ビット1=dah) の変化点
  IambicKeyer k;
  k.setWpm(wpm);
  k.setModeB(modeB);
  std::vector<Tr> out;
  size_t pi = 0;
  int cur = 0;
  KeyEvent ev;
  for (uint32_t t = 0; t < endMs; t++) {
    while (pi < paddle.size() && paddle[pi].first <= t) cur = paddle[pi++].second;
    k.update(t, cur & 1, cur & 2);
    while (k.pop(ev)) out.push_back({ev.t, ev.state});
  }
  return out;
}

static std::string elements(const std::vector<Tr>& tr, uint32_t dit) {
  std::string s;
  for (size_t i = 0; i + 1 < tr.size(); i++)
    if (tr[i].s == 1) s += (tr[i + 1].t - tr[i].t) >= 2 * dit ? '-' : '.';
  return s;
}

static void testKeyer() {
  printf("[keyer] iambic\n");
  const uint32_t dit = 60;  // 20 WPM
  CHECK(ditMsForWpm(20) == 60 && ditMsForWpm(12) == 100, "dit");

  // dit だけ押し続ける: 60ms マーク / 60ms スペース
  auto tr = runIambic(true, 20, {{10, 1}, {10 + 5 * 120 - 30, 0}}, 2000);
  CHECK(elements(tr, dit) == ".....", "dits=%s", elements(tr, dit).c_str());
  bool timingOk = true;
  for (size_t i = 0; i + 1 < tr.size(); i++)
    if (tr[i + 1].t - tr[i].t != dit) timingOk = false;
  CHECK(timingOk && tr[0].t == 10, "dit timing");

  // dah だけ: 180ms マーク
  tr = runIambic(true, 20, {{0, 2}, {300, 0}}, 2000);
  CHECK(elements(tr, dit) == "--", "dahs=%s", elements(tr, dit).c_str());
  CHECK(tr.size() >= 2 && tr[1].t - tr[0].t == 180, "dah len");

  // スクイーズ (dit 先): .-.-
  tr = runIambic(true, 20, {{0, 1}, {20, 3}, {20 + 470, 0}}, 2000);
  // 0:dit(0-60) 120:dah(120-300) 360:dit(360-420) 480:dah... 490 で離す → B: dah 中なので次の dit も送る
  CHECK(elements(tr, dit) == ".-.-.", "squeeze B=%s", elements(tr, dit).c_str());
  tr = runIambic(false, 20, {{0, 1}, {20, 3}, {20 + 470, 0}}, 2000);
  CHECK(elements(tr, dit) == ".-.-", "squeeze A=%s", elements(tr, dit).c_str());

  // Mode A/B の違い: dah 中にスクイーズして dah 中に両方離す
  tr = runIambic(true, 20, {{0, 2}, {50, 3}, {100, 0}}, 2000);
  CHECK(elements(tr, dit) == "-.", "B extra dit=%s", elements(tr, dit).c_str());
  tr = runIambic(false, 20, {{0, 2}, {50, 3}, {100, 0}}, 2000);
  CHECK(elements(tr, dit) == "-", "A no extra=%s", elements(tr, dit).c_str());

  // dah 中に dit を軽く叩く (B: メモリで dit が入る)
  tr = runIambic(true, 20, {{0, 2}, {30, 3}, {60, 2}, {170, 0}}, 2000);
  CHECK(elements(tr, dit) == "-.", "dit memory=%s", elements(tr, dit).c_str());

  // WPM 30
  tr = runIambic(true, 30, {{0, 1}, {30, 0}}, 1000);
  CHECK(tr.size() == 2 && tr[1].t - tr[0].t == 40, "30wpm dit");

  // update の呼び出しが遅れても遷移時刻は正確
  {
    IambicKeyer k;
    k.setWpm(20);
    std::vector<Tr> out;
    KeyEvent ev;
    for (uint32_t t = 0; t < 1000; t += 37) {
      k.update(t, t < 400, false);
      while (k.pop(ev)) out.push_back({ev.t, ev.state});
    }
    bool ok = out.size() >= 4;
    for (size_t i = 1; i < out.size(); i++)
      if (out[i].t - out[i - 1].t != dit) ok = false;
    CHECK(ok, "late update timing");
  }

  printf("[keyer] debounce / text\n");
  Debouncer db;
  // 押下時のバウンス: 最初のエッジで即採用
  CHECK(db.update(true, 100) == true, "first edge");
  CHECK(db.update(false, 101) == true && db.update(true, 102) == true, "bounce ignored");
  CHECK(db.update(false, 103) == true, "still locked");
  CHECK(db.update(false, 104) == false, "release after lockout");

  // TextKeyer: PARIS @20WPM = 50 dit (末尾語間込み) = 3000ms
  TextKeyer tk;
  tk.start("PARIS", 20, 0);
  std::vector<Tr> out;
  KeyEvent ev;
  for (uint32_t t = 0; t <= 3000; t++) {
    tk.update(t);
    while (tk.pop(ev)) out.push_back({ev.t, ev.state});
  }
  CHECK(out.size() == 29, "PARIS transitions=%zu", out.size());  // 14 要素 *2 + 次周の DOWN
  CHECK(out.back().t == 3000 && out.back().s == 1, "PARIS = 50 dit, last=%u", out.back().t);
}

// =================================================================
static std::vector<Tr> textOnce(const char* text, int wpm, uint32_t t0) {
  // 1周ぶん (末尾の語間の直前まで) の遷移列
  size_t need = 0;
  for (const char* p = text; *p; p++) {
    const char* c = morseEncode(*p);
    if (c) need += 2 * strlen(c);
  }
  TextKeyer k;
  k.start(text, wpm, t0);
  std::vector<Tr> out;
  KeyEvent ev;
  for (uint32_t t = t0; out.size() < need; t++) {
    k.update(t);
    while (k.pop(ev) && out.size() < need) out.push_back({ev.t, ev.state});
  }
  return out;
}

static void testDecoder() {
  printf("[decoder]\n");
  struct Case {
    const char* text;
    int wpm;
    int jitter;
  } cases[] = {
      {"PARIS PARIS", 20, 0}, {"CQ CQ DE JA1ABC K", 20, 0}, {"CQ CQ DE JA1ABC K", 20, 8},
      {"TEST 5NN 599 TU", 25, 5}, {"HELLO WORLD 73", 30, 4}, {"ABCDEFGHIJKLMNOPQRSTUVWXYZ", 18, 5},
      {"0123456789 /?.,=", 22, 3},
  };
  for (auto& c : cases) {
    auto tl = textOnce(c.text, c.wpm, 1000);
    std::string got = decodeTimeline(tl, c.jitter, 7);
    std::string want = std::string(c.text) + " ";
    CHECK(got == want, "%dwpm j%d: want '%s' got '%s'", c.wpm, c.jitter, c.text, got.c_str());
  }
  // 初期推定 (20WPM) から大きく外れた速度: 前置きで収束した後は正しく読める
  for (int wpm : {8, 12, 35, 45}) {
    auto tl = textOnce("EEEE TTTT PARIS PARIS", wpm, 1000);
    std::string got = decodeTimeline(tl, 0, 3);
    bool ok = got.size() >= 12 && got.substr(got.size() - 12) == "PARIS PARIS ";
    CHECK(ok, "%dwpm adapt: got '%s'", wpm, got.c_str());
  }
  // 未知の符号は ?
  std::vector<Tr> tl;
  uint32_t t = 1000;
  for (int i = 0; i < 9; i++) {  // 9 dit
    tl.push_back({t, 1});
    tl.push_back({t + 60, 0});
    t += 120;
  }
  CHECK(decodeTimeline(tl) == std::string(1, CHAR_UNKNOWN) + " ", "unknown: '%s'", decodeTimeline(tl).c_str());
  // 略符号: 文字間を空けずに送った SK は [SK]。正しく送った '?' は '?' のまま
  {
    std::string got = decodeTimeline(textOnce("CQ [SK] ? TU", 20, 1000));
    CHECK(got == "CQ [SK] ? TU ", "prosign: '%s'", got.c_str());
    got = decodeTimeline(textOnce("SK [SK]", 25, 1000), 4, 5);
    CHECK(got == "SK [SK] ", "prosign jitter: '%s'", got.c_str());
  }
  {
    MorseDecoder d;
    auto tl2 = textOnce("PARIS PARIS PARIS", 25, 0);
    for (auto& e : tl2) d.onTransition(e.s, e.t);
    CHECK(d.wpm() >= 24 && d.wpm() <= 26, "wpm estimate %d", d.wpm());
  }
}

// =================================================================
// 損失シミュレーション: 送信側 → 通信路 → 受信側
struct ChannelOpts {
  double loss = 0;           // ランダムロス率
  bool dropUp = false;       // state==UP のパケットを全部落とす
  double burstProb = 0;      // バースト開始確率 (パケットごと)
  int burstLen = 0;          // バースト長 (パケット数)
  int jitterMax = 0;         // 遅延変動 0..jitterMax ms
  double dupProb = 0;        // 重複
  int baseDelay = 3;         // 基本遅延
  double driftPpm = 0;       // 送信側クロックの進み (ppm)
  uint32_t clockOffset = 0;  // 送受信の時計の差
  unsigned seed = 1;
};

struct SimResult {
  std::vector<Tr> src;      // 送信側の遷移 (受信側の時刻に換算)
  std::vector<PlayedEvent> played;
  RxStats st;
  uint32_t maxStuck = 0;    // 送信側 UP なのに再生側 DOWN だった最長時間 (遅延補正後)
  uint32_t endState = 0;
};

static SimResult simulate(const std::vector<Tr>& keying, uint32_t endMs, const ChannelOpts& o) {
  // keying: 受信側の時間軸での送信側キー操作 (t, state)
  std::mt19937 rng(o.seed);
  std::uniform_real_distribution<double> U(0, 1);
  struct InFlight {
    uint32_t arrive;
    std::vector<uint8_t> data;
  };
  std::vector<InFlight> flight;
  TxHistory hist(0xFFF0);  // wrap も試す
  TxScheduler sched;
  Receiver rx;
  uint16_t seq = 0;
  int burstLeft = 0;
  size_t ki = 0;
  SimResult r;
  uint8_t srcState = 0;
  uint32_t srcUpSince = 0;
  for (uint32_t t = 0; t < endMs; t++) {
    // 送信側の時計 (オフセット + ドリフト)
    uint32_t txClock = o.clockOffset + t + (uint32_t)(t * o.driftPpm * 1e-6);
    while (ki < keying.size() && keying[ki].t <= t) {
      uint32_t kt = o.clockOffset + keying[ki].t + (uint32_t)(keying[ki].t * o.driftPpm * 1e-6);
      if (hist.push(keying[ki].s, kt)) {
        sched.onTransition(keying[ki].s, kt);
        r.src.push_back(keying[ki]);
        srcState = keying[ki].s;
        if (!srcState) srcUpSince = t;
      }
      ki++;
    }
    if (sched.poll(txClock)) {
      Packet p;
      hist.build(p, txClock, cfg::HIST_N, cfg::GROUP_ID, seq++);
      uint8_t buf[PKT_MAX_SIZE];
      size_t len = encodePacket(p, buf, sizeof(buf));
      bool drop = U(rng) < o.loss;
      if (o.dropUp && p.state == 0) drop = true;
      if (burstLeft > 0) {
        drop = true;
        burstLeft--;
      } else if (o.burstProb > 0 && U(rng) < o.burstProb) {
        burstLeft = o.burstLen - 1;
        drop = true;
      }
      if (!drop) {
        int copies = U(rng) < o.dupProb ? 2 : 1;
        for (int c = 0; c < copies; c++) {
          uint32_t delay = o.baseDelay + (o.jitterMax ? (uint32_t)(U(rng) * o.jitterMax) : 0);
          flight.push_back({t + delay, std::vector<uint8_t>(buf, buf + len)});
        }
      }
    }
    // 到着 (到着時刻順。ジッタにより順序逆転が起きる)
    for (size_t i = 0; i < flight.size();) {
      if (flight[i].arrive <= t) {
        Packet p;
        if (decodePacket(flight[i].data.data(), flight[i].data.size(), p)) rx.onPacket(p, t);
        flight.erase(flight.begin() + i);
      } else {
        i++;
      }
    }
    rx.update(t);
    PlayedEvent ev;
    while (rx.pop(ev)) r.played.push_back(ev);
    // 押しっぱなし検出: 送信側が UP になってから (遅延 + 余裕) 以上経っても再生側 DOWN
    if (srcState == 0 && rx.state() == 1) {
      uint32_t since = t - srcUpSince;
      if (since > r.maxStuck) r.maxStuck = since;
    }
  }
  r.st = rx.stats();
  r.endState = rx.state();
  return r;
}

// 送信側の遷移列と再生側の遷移列を比較
static bool compareTimelines(const SimResult& r, uint32_t skipMs, int tol, int& maxErr, std::string& why) {
  std::vector<Tr> a, b;
  for (auto& e : r.src)
    if (e.t >= skipMs) a.push_back(e);
  // 再生側は (送信時刻 + offset + delay) の時間軸なので先頭の差で揃える
  for (auto& e : r.played) b.push_back({e.playMs, e.state});
  // 先頭合わせ: a[0] に対応する b を探す (状態一致かつ最も近い)
  if (a.empty()) return true;
  maxErr = 0;
  // 再生側の時刻 - 送信側の時刻 の差は一定 (offset 推定誤差 + delay) のはず
  size_t j = 0;
  while (j < b.size() && (b[j].t < a[0].t || b[j].s != a[0].s)) j++;
  if (j + a.size() > b.size()) {
    why = "played too short: src=" + std::to_string(a.size()) + " played_after=" + std::to_string(b.size() - j);
    return false;
  }
  int32_t base = (int32_t)(b[j].t - a[0].t);
  for (size_t i = 0; i < a.size(); i++) {
    if (a[i].s != b[j + i].s) {
      why = "state mismatch at " + std::to_string(i);
      return false;
    }
    int32_t d = (int32_t)(b[j + i].t - a[i].t) - base;
    if (std::abs(d) > maxErr) maxErr = std::abs(d);
  }
  if (maxErr > tol) {
    why = "timing error " + std::to_string(maxErr);
    return false;
  }
  return true;
}

static std::vector<Tr> sampleKeying(unsigned seed, uint32_t t0, uint32_t dur, bool longHolds) {
  // ランダムな DOWN/UP 長 (20〜400ms, 長押しありの場合は時々 2 秒)
  std::mt19937 rng(seed);
  std::uniform_int_distribution<int> L(20, 400);
  std::uniform_int_distribution<int> P(0, 9);
  std::vector<Tr> out;
  uint32_t t = t0;
  uint8_t s = 1;
  while (t < t0 + dur) {
    out.push_back({t, s});
    int len = L(rng);
    if (longHolds && P(rng) == 0) len = 2000;
    if (!s && P(rng) == 0) len = 1500;  // 時々アイドル (ハートビート間隔へ移行)
    t += len;
    s ^= 1;
  }
  if (s == 0) out.push_back({t, 0});
  return out;
}

static void testLossSim() {
  printf("[sim] loss / jitter / reorder\n");
  const uint32_t START = 2000, DUR = 60000, END = START + DUR + 3000;
  auto keying = sampleKeying(42, START, DUR, true);
  // 遅延上限: jitter delay + watchdog + 送信間隔 + 余裕
  const uint32_t STUCK_LIMIT = cfg::JITTER_DELAY_MS + cfg::WATCHDOG_MS + 100;

  struct Sc {
    const char* name;
    ChannelOpts o;
    bool expectExact;  // 遷移列が完全一致するはず
    int tol;
  };
  std::vector<Sc> scs;
  {
    ChannelOpts o;
    scs.push_back({"clean", o, true, 2});
  }
  for (double l : {0.1, 0.3, 0.5}) {
    ChannelOpts o;
    o.loss = l;
    o.seed = (unsigned)(l * 100);
    scs.push_back({l == 0.1 ? "loss10" : l == 0.3 ? "loss30" : "loss50", o, true, 2});
  }
  {
    ChannelOpts o;
    o.jitterMax = 80;
    o.dupProb = 0.1;
    scs.push_back({"jitter80+dup", o, true, 10});
  }
  {
    ChannelOpts o;
    o.jitterMax = 80;
    o.loss = 0.3;
    o.dupProb = 0.05;
    o.clockOffset = 0xFFFF0000u;  // wrap をまたぐ
    o.driftPpm = 200;
    scs.push_back({"jitter80+loss30+drift", o, true, 10});
  }
  {
    ChannelOpts o;
    o.burstProb = 0.01;
    o.burstLen = 15;  // 300ms
    o.loss = 0.1;
    scs.push_back({"burst300ms", o, false, 0});
  }
  {
    ChannelOpts o;
    o.dropUp = true;
    scs.push_back({"dropUP", o, false, 0});
  }
  {
    ChannelOpts o;
    o.jitterMax = 250;  // ジッタ遅延を越える: 遅延超過が起きる
    o.loss = 0.2;
    scs.push_back({"jitter250", o, false, 0});
  }

  for (auto& sc : scs) {
    SimResult r = simulate(keying, END, sc.o);
    printf("  %-22s pkts=%u lost=%u stale=%u resync=%u late=%u wd=%u reassert=%u restarts=%u maxStuck=%u\n",
           sc.name, r.st.packets, r.st.lost, r.st.stale, r.st.resyncs, r.st.late, r.st.watchdogs, r.st.reasserts,
           r.st.restarts, r.maxStuck);
    CHECK(r.endState == 0, "%s: ends DOWN (stuck)", sc.name);
    CHECK(r.maxStuck <= STUCK_LIMIT, "%s: stuck %u ms > %u", sc.name, r.maxStuck, STUCK_LIMIT);
    if (sc.o.loss == 0 && !sc.o.dropUp && sc.o.burstProb == 0)
      CHECK(r.st.lost == 0, "%s: lost=%u (reorder must not count as loss)", sc.name, r.st.lost);
    if (sc.expectExact) {
      int maxErr = 0;
      std::string why;
      bool ok = compareTimelines(r, START, sc.tol, maxErr, why);
      CHECK(ok, "%s: %s", sc.name, why.c_str());
      CHECK(r.st.resyncs == 0 && r.st.watchdogs == 0, "%s: resync=%u wd=%u", sc.name, r.st.resyncs, r.st.watchdogs);
      printf("    timeline matches, max timing error %d ms\n", maxErr);
    }
  }

  // 送信中の相手の再起動 (txMs と tid がリセットされる) からの復帰
  printf("[sim] sender restart / link loss\n");
  {
    Receiver rx;
    TxHistory h1(500);
    Packet p;
    h1.push(1, 10000);
    h1.build(p, 10010, cfg::HIST_N, 1, 0);
    rx.onPacket(p, 20010);
    for (uint32_t t = 20010; t < 21000; t++) rx.update(t);
    CHECK(rx.state() == 0 && rx.stats().watchdogs == 1, "watchdog after silence");
    // 再起動した相手: txMs=500 から、tid=0 から
    TxHistory h2(0);
    h2.push(1, 600);
    h2.build(p, 610, cfg::HIST_N, 1, 0);
    rx.onPacket(p, 21500);
    for (uint32_t t = 21500; t < 21700; t++) rx.update(t);
    CHECK(rx.state() == 1, "follows restarted sender");
    h2.push(0, 700);
    h2.build(p, 700, cfg::HIST_N, 1, 1);
    rx.onPacket(p, 21592);
    for (uint32_t t = 21700; t < 22000; t++) rx.update(t);
    CHECK(rx.state() == 0 && rx.stats().restarts == 2, "restarts=%u", rx.stats().restarts);
  }
}

int main() {
  testProtocolRoundTrip();
  testScheduler();
  testKeyer();
  testDecoder();
  testLossSim();
  printf("\n%d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
