// Host-side unit tests and loss simulation (section 9.1)
// Build: make -C test/host
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

#include "../../protocol.h"
#include "../../jitter.h"
#include "../../keyer.h"
#include "../../morse_decoder.h"
#include "../../peers.h"

using namespace cw;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, ...)                                             \
    do {                                                             \
        if (cond) {                                                  \
            g_pass++;                                                \
        } else {                                                     \
            g_fail++;                                                \
            printf("  FAIL %s:%d: %s  ", __FILE__, __LINE__, #cond); \
            printf(__VA_ARGS__);                                     \
            printf("\n");                                            \
        }                                                            \
    } while (0)

struct Tr {
    uint32_t t;
    uint8_t s;
};

// ---- Feed a transition sequence to the decoder ----
static std::string decodeTimeline(const std::vector<Tr>& tl, int jitterMs = 0,
                                  unsigned seed = 1) {
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
    p.channel = 7;
    p.state = 1;
    p.pktSeq = 0xBEEF;
    p.lastTid = 0xFFFE;
    p.txMs = 0xDEADBEEF;
    p.n = cfg::HIST_MAX;
    for (int i = 0; i < p.n; i++) p.age[i] = (uint16_t)(i * 1000 + 7);
    uint8_t buf[PKT_MAX_SIZE];
    size_t len = encodePacket(p, buf, sizeof(buf));
    CHECK(len == PKT_HEADER_SIZE + 2 * cfg::HIST_MAX, "len=%zu", len);
    CHECK(buf[0] == 0xC7 && buf[1] == 1 && buf[4] == 0xEF && buf[5] == 0xBE,
          "header bytes");
    Packet q;
    CHECK(decodePacket(buf, len, q), "decode");
    CHECK(q.channel == p.channel && q.state == p.state &&
              q.pktSeq == p.pktSeq && q.lastTid == p.lastTid &&
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
    h.push(1, 1130);  // same state is ignored
    h.build(p, 1200, cfg::HIST_N, 1, 5);
    CHECK(p.state == 1 && p.lastTid == 103 && p.n == 3, "n=%d tid=%d", p.n,
          p.lastTid);
    CHECK(p.age[0] == 80 && p.age[1] == 140 && p.age[2] == 200, "ages %d %d %d",
          p.age[0], p.age[1], p.age[2]);
    CHECK(p.stateOf(0) == 1 && p.stateOf(1) == 0 && p.stateOf(2) == 1,
          "stateOf");
    h.build(p, 1000 + 70000, cfg::HIST_N, 1, 6);
    CHECK(p.age[2] == 0xFFFF, "age saturates");

    // TX time restamping (LoRa): built at 1200 and sent at 1235 = same as built
    // at 1235
    {
        Packet a, b;
        h.build(a, 1200, cfg::HIST_N, 1, 5);
        h.build(b, 1235, cfg::HIST_N, 1, 5);
        uint8_t ba[PKT_MAX_SIZE], bb[PKT_MAX_SIZE];
        size_t la = encodePacket(a, ba, sizeof(ba)),
               lb = encodePacket(b, bb, sizeof(bb));
        CHECK(restampPacket(ba, la, 1235), "restamp");
        CHECK(la == lb && memcmp(ba, bb, la) == 0, "restamped == rebuilt");
        h.build(a, 1000 + 65500, cfg::HIST_N, 1, 6);
        la = encodePacket(a, ba, sizeof(ba));
        CHECK(restampPacket(ba, la, 1000 + 65600), "restamp 2");
        decodePacket(ba, la, q);
        CHECK(q.txMs == 1000 + 65600 && q.age[0] == 65600 - 120 &&
                  q.age[2] == 0xFFFF,
              "restamp saturates");
        CHECK(!restampPacket(ba, la, 1000), "time going back must fail");
        CHECK(!restampPacket(ba, 5, 2000), "short packet must fail");
    }
}

static void testScheduler() {
    printf("[protocol] tx scheduler\n");
    TxScheduler s;
    int sent = 0;
    // idle: every 500ms
    for (uint32_t t = 1000; t < 3000; t++) sent += s.poll(t);
    CHECK(sent >= 4 && sent <= 6, "idle sent=%d", sent);
    // DOWN 300ms: immediately on transition + every 20ms
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
// Keyer
static std::vector<Tr> runIambic(
    bool modeB, int wpm, const std::vector<std::pair<uint32_t, int>>& paddle,
    uint32_t endMs) {
    // paddle: change points of (time, bit0=dit bit1=dah)
    IambicKeyer k;
    k.setWpm(wpm);
    k.setModeB(modeB);
    std::vector<Tr> out;
    size_t pi = 0;
    int cur = 0;
    KeyEvent ev;
    for (uint32_t t = 0; t < endMs; t++) {
        while (pi < paddle.size() && paddle[pi].first <= t)
            cur = paddle[pi++].second;
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

    // hold dit only: 60ms mark / 60ms space
    auto tr = runIambic(true, 20, {{10, 1}, {10 + 5 * 120 - 30, 0}}, 2000);
    CHECK(elements(tr, dit) == ".....", "dits=%s", elements(tr, dit).c_str());
    bool timingOk = true;
    for (size_t i = 0; i + 1 < tr.size(); i++)
        if (tr[i + 1].t - tr[i].t != dit) timingOk = false;
    CHECK(timingOk && tr[0].t == 10, "dit timing");

    // dah only: 180ms mark
    tr = runIambic(true, 20, {{0, 2}, {300, 0}}, 2000);
    CHECK(elements(tr, dit) == "--", "dahs=%s", elements(tr, dit).c_str());
    CHECK(tr.size() >= 2 && tr[1].t - tr[0].t == 180, "dah len");

    // squeeze (dit first): .-.-
    tr = runIambic(true, 20, {{0, 1}, {20, 3}, {20 + 470, 0}}, 2000);
    // 0:dit(0-60) 120:dah(120-300) 360:dit(360-420) 480:dah... released at 490
    // -> B: released during dah, so the next dit is also sent
    CHECK(elements(tr, dit) == ".-.-.", "squeeze B=%s",
          elements(tr, dit).c_str());
    tr = runIambic(false, 20, {{0, 1}, {20, 3}, {20 + 470, 0}}, 2000);
    CHECK(elements(tr, dit) == ".-.-", "squeeze A=%s",
          elements(tr, dit).c_str());

    // Mode A/B difference: squeeze during dah and release both during dah
    tr = runIambic(true, 20, {{0, 2}, {50, 3}, {100, 0}}, 2000);
    CHECK(elements(tr, dit) == "-.", "B extra dit=%s",
          elements(tr, dit).c_str());
    tr = runIambic(false, 20, {{0, 2}, {50, 3}, {100, 0}}, 2000);
    CHECK(elements(tr, dit) == "-", "A no extra=%s", elements(tr, dit).c_str());

    // tap dit briefly during dah (B: dit gets latched into memory)
    tr = runIambic(true, 20, {{0, 2}, {30, 3}, {60, 2}, {170, 0}}, 2000);
    CHECK(elements(tr, dit) == "-.", "dit memory=%s",
          elements(tr, dit).c_str());

    // WPM 30
    tr = runIambic(true, 30, {{0, 1}, {30, 0}}, 1000);
    CHECK(tr.size() == 2 && tr[1].t - tr[0].t == 40, "30wpm dit");

    // transition times are exact even if update is called late
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
    // bounce on press: accept the first edge immediately
    CHECK(db.update(true, 100) == true, "first edge");
    CHECK(db.update(false, 101) == true && db.update(true, 102) == true,
          "bounce ignored");
    CHECK(db.update(false, 103) == true, "still locked");
    CHECK(db.update(false, 104) == false, "release after lockout");

    // TextKeyer: PARIS @20WPM = 50 dit (including trailing word gap) = 3000ms
    TextKeyer tk;
    tk.start("PARIS", 20, 0);
    std::vector<Tr> out;
    KeyEvent ev;
    for (uint32_t t = 0; t <= 3000; t++) {
        tk.update(t);
        while (tk.pop(ev)) out.push_back({ev.t, ev.state});
    }
    CHECK(out.size() == 29, "PARIS transitions=%zu",
          out.size());  // 14 elements *2 + DOWN of the next round
    CHECK(out.back().t == 3000 && out.back().s == 1, "PARIS = 50 dit, last=%u",
          out.back().t);
}

// =================================================================
static std::vector<Tr> textOnce(const char* text, int wpm, uint32_t t0) {
    // transition sequence for one round (up to just before the trailing word
    // gap)
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
        {"PARIS PARIS", 20, 0},       {"CQ CQ DE JA1ABC K", 20, 0},
        {"CQ CQ DE JA1ABC K", 20, 8}, {"TEST 5NN 599 TU", 25, 5},
        {"HELLO WORLD 73", 30, 4},    {"ABCDEFGHIJKLMNOPQRSTUVWXYZ", 18, 5},
        {"0123456789 /?.,=", 22, 3},
    };
    for (auto& c : cases) {
        auto tl = textOnce(c.text, c.wpm, 1000);
        std::string got = decodeTimeline(tl, c.jitter, 7);
        std::string want = std::string(c.text) + " ";
        CHECK(got == want, "%dwpm j%d: want '%s' got '%s'", c.wpm, c.jitter,
              c.text, got.c_str());
    }
    // speed far from the initial estimate (20WPM): reads correctly once it
    // converges on the preamble
    for (int wpm : {8, 12, 35, 45}) {
        auto tl = textOnce("EEEE TTTT PARIS PARIS", wpm, 1000);
        std::string got = decodeTimeline(tl, 0, 3);
        bool ok =
            got.size() >= 12 && got.substr(got.size() - 12) == "PARIS PARIS ";
        CHECK(ok, "%dwpm adapt: got '%s'", wpm, got.c_str());
    }
    // unknown code -> ?
    std::vector<Tr> tl;
    uint32_t t = 1000;
    for (int i = 0; i < 9; i++) {  // 9 dit
        tl.push_back({t, 1});
        tl.push_back({t + 60, 0});
        t += 120;
    }
    CHECK(decodeTimeline(tl) == std::string(1, CHAR_UNKNOWN) + " ",
          "unknown: '%s'", decodeTimeline(tl).c_str());
    // prosign: SK sent without character gap is [SK]. A correctly sent '?'
    // stays '?'
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
// Loss simulation: transmitter -> channel -> receiver
struct ChannelOpts {
    double loss = 0;           // random loss rate
    bool dropUp = false;       // drop every packet with state==UP
    double burstProb = 0;      // burst start probability (per packet)
    int burstLen = 0;          // burst length (packets)
    int jitterMax = 0;         // delay variation 0..jitterMax ms
    double dupProb = 0;        // duplication
    int baseDelay = 3;         // base delay
    double driftPpm = 0;       // transmitter clock running fast (ppm)
    uint32_t clockOffset = 0;  // clock difference between TX and RX
    unsigned seed = 1;
    // LoRa TX conditions: keep only the latest built packet; after pause +
    // carrier sense following the end of the previous TX, restamp the TX time
    // and send (same as net_lora.cpp). Receiver jitter delay and idle interval
    // also use the LoRa values
    bool lora = false;
    int loraToaMs = 25;  // airtime (SF5/BW125, ~24.9ms for 35 bytes)
    int csBusyMax = 0;   // time spent waiting on carrier sense 0..csBusyMax ms
};

struct SimResult {
    std::vector<Tr>
        src;  // transmitter transitions (converted to receiver time)
    std::vector<PlayedEvent> played;
    RxStats st;
    uint32_t maxStuck = 0;  // longest time playback was DOWN while transmitter
                            // was UP (after delay compensation)
    uint32_t endState = 0;
};

static SimResult simulate(const std::vector<Tr>& keying, uint32_t endMs,
                          const ChannelOpts& o) {
    // keying: transmitter key operations on the receiver timeline (t, state)
    std::mt19937 rng(o.seed);
    std::uniform_real_distribution<double> U(0, 1);
    struct InFlight {
        uint32_t arrive;
        std::vector<uint8_t> data;
    };
    std::vector<InFlight> flight;
    TxHistory hist(0xFFF0);  // also test wrap
    TxScheduler sched;
    Receiver rx;
    if (o.lora) {
        rx.setDelay(cfg::LORA_JITTER_DELAY_MS);
        sched.setIdleInterval(cfg::LORA_TX_INTERVAL_IDLE_MS);
    }
    std::vector<uint8_t> mailbox;  // LoRa: latest packet waiting to be sent
    uint32_t radioFreeAt = 0;  // LoRa: time when carrier sense can next start
    bool inCs = false;
    uint32_t csEndAt = 0;
    uint16_t loraSeq = 0;
    std::uniform_int_distribution<int> CSB(0, o.csBusyMax);
    uint16_t seq = 0;
    int burstLeft = 0;
    size_t ki = 0;
    SimResult r;
    uint8_t srcState = 0;
    uint32_t srcUpSince = 0;
    for (uint32_t t = 0; t < endMs; t++) {
        // transmitter clock (offset + drift)
        uint32_t txClock =
            o.clockOffset + t + (uint32_t)(t * o.driftPpm * 1e-6);
        while (ki < keying.size() && keying[ki].t <= t) {
            uint32_t kt = o.clockOffset + keying[ki].t +
                          (uint32_t)(keying[ki].t * o.driftPpm * 1e-6);
            if (hist.push(keying[ki].s, kt)) {
                sched.onTransition(keying[ki].s, kt);
                r.src.push_back(keying[ki]);
                srcState = keying[ki].s;
                if (!srcState) srcUpSince = t;
            }
            ki++;
        }
        bool sendNow = false;
        uint8_t buf[PKT_MAX_SIZE];
        size_t len = 0;
        Packet p;
        if (sched.poll(txClock)) {
            hist.build(p, txClock, cfg::HIST_N, 1, seq++);
            len = encodePacket(p, buf, sizeof(buf));
            if (o.lora)
                mailbox.assign(buf, buf + len);
            else
                sendNow = true;
        }
        uint32_t extraDelay = 0;
        if (o.lora && !mailbox.empty() && !inCs && t >= radioFreeAt) {
            // carrier sense after the pause (5ms + wait due to congestion)
            inCs = true;
            csEndAt = t + (uint32_t)std::ceil(cfg::LORA_CS_US / 1000.0) +
                      (o.csBusyMax ? CSB(rng) : 0);
        }
        if (o.lora && inCs && t >= csEndAt) {
            // send the latest packet at the end of carrier sense, restamped to
            // the TX start time
            inCs = false;
            len = mailbox.size();
            memcpy(buf, mailbox.data(), len);
            mailbox.clear();
            restampPacket(buf, len, txClock);
            setPacketSeq(buf, loraSeq++);
            decodePacket(buf, len, p);
            extraDelay = o.loraToaMs;
            radioFreeAt = t + o.loraToaMs + cfg::LORA_PAUSE_MS;
            sendNow = true;
        }
        if (sendNow) {
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
                    uint32_t delay =
                        extraDelay + o.baseDelay +
                        (o.jitterMax ? (uint32_t)(U(rng) * o.jitterMax) : 0);
                    flight.push_back(
                        {t + delay, std::vector<uint8_t>(buf, buf + len)});
                }
            }
        }
        // arrival (in arrival-time order; jitter causes reordering)
        for (size_t i = 0; i < flight.size();) {
            if (flight[i].arrive <= t) {
                Packet p;
                if (decodePacket(flight[i].data.data(), flight[i].data.size(),
                                 p))
                    rx.onPacket(p, t);
                flight.erase(flight.begin() + i);
            } else {
                i++;
            }
        }
        rx.update(t);
        PlayedEvent ev;
        while (rx.pop(ev)) r.played.push_back(ev);
        // stuck key-down detection: playback still DOWN at least (delay +
        // margin) after the transmitter went UP
        if (srcState == 0 && rx.state() == 1) {
            uint32_t since = t - srcUpSince;
            if (since > r.maxStuck) r.maxStuck = since;
        }
    }
    r.st = rx.stats();
    r.endState = rx.state();
    return r;
}

// Compare transmitter and playback transition sequences
static bool compareTimelines(const SimResult& r, uint32_t skipMs, int tol,
                             int& maxErr, std::string& why) {
    std::vector<Tr> a, b;
    for (auto& e : r.src)
        if (e.t >= skipMs) a.push_back(e);
    // playback is on the (TX time + offset + delay) timeline, so align by the
    // difference at the start
    for (auto& e : r.played) b.push_back({e.playMs, e.state});
    // align start: find the b corresponding to a[0] (same state and nearest)
    if (a.empty()) return true;
    maxErr = 0;
    // playback time - TX time should be constant (offset estimation error +
    // delay)
    size_t j = 0;
    while (j < b.size() && (b[j].t < a[0].t || b[j].s != a[0].s)) j++;
    if (j + a.size() > b.size()) {
        why = "played too short: src=" + std::to_string(a.size()) +
              " played_after=" + std::to_string(b.size() - j);
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

static std::vector<Tr> sampleKeying(unsigned seed, uint32_t t0, uint32_t dur,
                                    bool longHolds) {
    // random DOWN/UP lengths (20-400ms, occasionally 2 s when long presses are
    // enabled)
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
        if (!s && P(rng) == 0)
            len = 1500;  // occasional idle (switch to heartbeat interval)
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
    // upper bound for stuck key-down: jitter delay + watchdog + TX interval +
    // margin (set per scenario below)

    struct Sc {
        const char* name;
        ChannelOpts o;
        bool expectExact;  // transition sequence should match exactly
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
        scs.push_back({l == 0.1   ? "loss10"
                       : l == 0.3 ? "loss30"
                                  : "loss50",
                       o, true, 2});
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
        o.clockOffset = 0xFFFF0000u;  // crosses wrap
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
        o.jitterMax = 250;  // exceeds jitter delay: late arrivals occur
        o.loss = 0.2;
        scs.push_back({"jitter250", o, false, 0});
    }

    // LoRa: TX is limited to ~60ms intervals (pause 50ms + carrier sense)
    for (double l : {0.0, 0.1, 0.3}) {
        ChannelOpts o;
        o.lora = true;
        o.loss = l;
        o.seed = 7 + (unsigned)(l * 100);
        // at 30% loss, 5 consecutive losses (~350ms outage) occasionally occur
        // and the watchdog fires, so exact match is not required
        scs.push_back({l == 0     ? "lora"
                       : l == 0.1 ? "lora+loss10"
                                  : "lora+loss30",
                       o, l < 0.2, 2});
    }
    {
        ChannelOpts o;
        o.lora = true;
        o.loss = 0.1;
        o.csBusyMax =
            30;  // carrier sense is delayed by other stations transmitting
        o.clockOffset = 0xFFFF0000u;
        o.driftPpm = 200;
        scs.push_back({"lora+loss10+csbusy", o, true, 2});
    }
    {
        ChannelOpts o;
        o.lora = true;
        o.dropUp = true;
        scs.push_back({"lora+dropUP", o, false, 0});
    }

    for (auto& sc : scs) {
        SimResult r = simulate(keying, END, sc.o);
        const uint32_t STUCK_LIMIT =
            sc.o.lora ? cfg::LORA_JITTER_DELAY_MS + cfg::WATCHDOG_MS +
                            cfg::LORA_PAUSE_MS + 100
                      : cfg::JITTER_DELAY_MS + cfg::WATCHDOG_MS + 100;
        printf(
            "  %-22s pkts=%u lost=%u stale=%u resync=%u late=%u wd=%u "
            "reassert=%u restarts=%u maxStuck=%u\n",
            sc.name, r.st.packets, r.st.lost, r.st.stale, r.st.resyncs,
            r.st.late, r.st.watchdogs, r.st.reasserts, r.st.restarts,
            r.maxStuck);
        CHECK(r.endState == 0, "%s: ends DOWN (stuck)", sc.name);
        CHECK(r.maxStuck <= STUCK_LIMIT, "%s: stuck %u ms > %u", sc.name,
              r.maxStuck, STUCK_LIMIT);
        if (sc.o.loss == 0 && !sc.o.dropUp && sc.o.burstProb == 0)
            CHECK(r.st.lost == 0,
                  "%s: lost=%u (reorder must not count as loss)", sc.name,
                  r.st.lost);
        if (sc.expectExact) {
            int maxErr = 0;
            std::string why;
            bool ok = compareTimelines(r, START, sc.tol, maxErr, why);
            CHECK(ok, "%s: %s", sc.name, why.c_str());
            CHECK(r.st.resyncs == 0 && r.st.watchdogs == 0,
                  "%s: resync=%u wd=%u", sc.name, r.st.resyncs, r.st.watchdogs);
            printf("    timeline matches, max timing error %d ms\n", maxErr);
        }
    }

    // recovery from the peer restarting mid-transmission (txMs and tid are
    // reset)
    printf("[sim] sender restart / link loss\n");
    {
        Receiver rx;
        TxHistory h1(500);
        Packet p;
        h1.push(1, 10000);
        h1.build(p, 10010, cfg::HIST_N, 1, 0);
        rx.onPacket(p, 20010);
        for (uint32_t t = 20010; t < 21000; t++) rx.update(t);
        CHECK(rx.state() == 0 && rx.stats().watchdogs == 1,
              "watchdog after silence");
        // restarted peer: txMs from 500, tid from 0
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
        CHECK(rx.state() == 0 && rx.stats().restarts == 2, "restarts=%u",
              rx.stats().restarts);
    }
}

// =================================================================
// Terminal mode: send typed characters one unit at a time
// type typed[i].second at time typed[i].first (BS cancels)
static std::vector<Tr> runStream(
    const std::vector<std::pair<uint32_t, std::string>>& typed, int wpm,
    uint32_t endMs) {
    TypeBuffer buf;
    StreamKeyer k;
    k.setWpm(wpm);
    std::vector<Tr> out;
    size_t ti = 0;
    KeyEvent ev;
    for (uint32_t t = 0; t < endMs; t++) {
        while (ti < typed.size() && typed[ti].first <= t) {
            for (char c : typed[ti].second) {
                if (c == '\b')
                    buf.popBack();
                else
                    buf.push(c);
            }
            ti++;
        }
        char unit[TypeBuffer::MAX_UNIT + 1];
        while (k.ready() && buf.takeUnit(unit)) k.send(unit, t);
        k.update(t);
        while (k.pop(ev)) out.push_back({ev.t, ev.state});
    }
    return out;
}

static void testStream() {
    printf("[keyer] terminal stream\n");
    // "CQ DE" typed all at once has the same timing as TextKeyer (character gap
    // 3 dit, word gap 7 dit)
    {
        auto tl = runStream({{100, "CQ DE"}}, 20, 10000);
        std::string d = decodeTimeline(tl);
        CHECK(d == "CQ DE ", "decoded '%s'", d.c_str());
        auto ref = textOnce("CQ DE", 20, 100);
        bool same = ref.size() == tl.size();
        for (size_t i = 0; same && i < tl.size(); i++)
            same = ref[i].t == tl[i].t && ref[i].s == tl[i].s;
        CHECK(same, "same timing as TextKeyer (n=%zu ref=%zu)", tl.size(),
              ref.size());
    }
    // typing one character at a time slowly: start immediately if enough time
    // has passed since the previous character
    {
        auto tl = runStream({{0, "E"}, {1000, "T"}}, 20, 3000);
        CHECK(tl.size() == 4 && tl[0].t == 0 && tl[2].t == 1000,
              "starts immediately when idle (%u)", tl.size() > 2 ? tl[2].t : 0);
    }
    // next character during transmission: 3 dit after the end of the previous
    // character
    {
        auto tl = runStream({{0, "E"}, {10, "E"}}, 20, 2000);
        CHECK(tl.size() == 4 && tl[1].t == 60 && tl[2].t == 60 + 180,
              "char gap (%u)", tl.size() > 2 ? tl[2].t : 0);
    }
    // prosign [SK] has no character gap; cancel (BS) affects only characters
    // not yet sent
    {
        auto tl =
            runStream({{0, "K"}, {5, "X\b"}, {6, "[S"}, {700, "K]"}}, 20, 6000);
        std::string d = decodeTimeline(tl);
        CHECK(d == "K [SK] " || d == "K[SK] ", "decoded '%s'", d.c_str());
        TypeBuffer b;
        char u[TypeBuffer::MAX_UNIT + 1];
        b.push('[');
        b.push('A');
        CHECK(!b.takeUnit(u), "wait for ]");
        b.push(']');
        CHECK(b.takeUnit(u) && !strcmp(u, "[A]"), "unit '%s'", u);
        CHECK(!b.popBack(), "nothing to cancel");
    }
}

// =================================================================
// Multiple stations: each station is reconstructed and decoded independently,
// tone is ORed
static void testPeers() {
    printf("[peers] multi-station\n");
    struct Station {
        uint8_t mac[6];
        std::vector<Tr> tl;
        TxHistory hist;
        TxScheduler sched;
        size_t ki = 0;
        uint16_t seq = 0;
        uint32_t clockOffset;
    };
    // A and B transmit at different times, C transmits overlapping A
    // (simultaneous)
    Station st[3] = {
        {{1, 1, 1, 1, 1, 1},
         textOnce("CQ CQ DE A", 20, 1000),
         TxHistory(10),
         {},
         0,
         0,
         5000},
        {{2, 2, 2, 2, 2, 2},
         textOnce("R R DE B K", 25, 9000),
         TxHistory(20),
         {},
         0,
         0,
         123456},
        {{3, 3, 3, 3, 3, 3},
         textOnce("TEST C", 18, 2000),
         TxHistory(30),
         {},
         0,
         0,
         999},
    };
    PeerTable table;
    std::string text[cfg::MAX_PEERS];
    int idxOf[3] = {-1, -1, -1};
    bool orOk = true;
    for (uint32_t t = 0; t < 20000; t++) {
        for (int s = 0; s < 3; s++) {
            Station& x = st[s];
            uint32_t clk = t + x.clockOffset;
            while (x.ki < x.tl.size() && x.tl[x.ki].t <= t) {
                if (x.hist.push(x.tl[x.ki].s, clk))
                    x.sched.onTransition(x.tl[x.ki].s, clk);
                x.ki++;
            }
            if (x.sched.poll(clk)) {
                Packet p;
                x.hist.build(p, clk, cfg::HIST_N, 1, x.seq++);
                if ((x.seq % 5) == 0) continue;  // 20% loss
                int idx;
                Peer& peer = table.get(x.mac, t, idx);
                idxOf[s] = idx;
                peer.rx.onPacket(p, t + 2);
            }
        }
        bool any = false;
        for (int i = 0; i < cfg::MAX_PEERS; i++) {
            Peer& p = table.at(i);
            if (!p.used) continue;
            p.rx.update(t);
            PlayedEvent ev;
            while (p.rx.pop(ev)) p.dec.onTransition(ev.state, ev.playMs);
            p.dec.update(t);
            char c;
            while (p.dec.pop(c)) text[i] += c;
            any |= p.rx.state() != 0;
        }
        if (any != table.anyDown()) orOk = false;
    }
    CHECK(text[idxOf[0]] == "CQ CQ DE A ", "A: '%s'", text[idxOf[0]].c_str());
    CHECK(text[idxOf[1]] == "R R DE B K ", "B: '%s'", text[idxOf[1]].c_str());
    CHECK(text[idxOf[2]] == "TEST C ", "C: '%s'", text[idxOf[2]].c_str());
    CHECK(orOk && !table.anyDown(), "tone OR");
    CHECK(table.activeCount(20000) == 3, "active=%d", table.activeCount(20000));
    RxStats tot = table.totalStats();
    CHECK(tot.watchdogs == 0 && tot.resyncs == 0, "wd=%u resync=%u",
          tot.watchdogs, tot.resyncs);

    // eviction: when more than MAX_PEERS stations arrive, the oldest station is
    // replaced. Stats remain in the totals
    uint32_t before = table.totalStats().packets;
    for (int k = 0; k < cfg::MAX_PEERS + 2; k++) {
        uint8_t mac[6] = {9, 9, 9, 9, 9, (uint8_t)k};
        int idx;
        Packet p;
        TxHistory h(0);
        h.build(p, 100, cfg::HIST_N, 1, 0);
        table.get(mac, 30000 + k * 10, idx).rx.onPacket(p, 30000 + k * 10);
    }
    CHECK(table.totalStats().packets == before + cfg::MAX_PEERS + 2,
          "stats kept after eviction");
    table.clear();
    CHECK(!table.anyDown() && table.activeCount(30100) == 0, "clear");
}

int main() {
    testProtocolRoundTrip();
    testScheduler();
    testKeyer();
    testDecoder();
    testStream();
    testLossSim();
    testPeers();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
