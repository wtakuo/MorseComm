// protocol.h - packet format (sec. 4.3) and the sender's transition history /
// transmit scheduler Arduino-independent. All times are passed as the argument
// nowMs (milliseconds, uint32 with wraparound).
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "config.h"

namespace cw {

constexpr uint8_t PKT_MAGIC = 0xC7;
constexpr uint8_t PKT_VERSION = 1;
constexpr size_t PKT_HEADER_SIZE = 13;
constexpr size_t PKT_MAX_SIZE = PKT_HEADER_SIZE + 2 * cfg::HIST_MAX;

struct Packet {
    uint8_t channel = 0;  // communication channel (accepted only between
                          // stations with the same value)
    uint8_t state = 0;    // 1=DOWN, 0=UP
    uint16_t pktSeq = 0;
    uint16_t lastTid = 0;  // latest transition ID
    uint32_t txMs = 0;     // sender's millis()
    uint8_t n = 0;         // history count
    uint16_t age[cfg::HIST_MAX] =
        {};  // i=0 is the latest. ms elapsed since txMs (saturating)

    // State after transition i of the history (latest = state, alternating from
    // there)
    uint8_t stateOf(int i) const { return (i & 1) ? (state ^ 1) : state; }
};

inline void put16(uint8_t* p, uint16_t v) {
    p[0] = v & 0xFF;
    p[1] = v >> 8;
}
inline void put32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = (v >> (8 * i)) & 0xFF;
}
inline uint16_t get16(const uint8_t* p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}
inline uint32_t get32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

// Encode. Returns the number of bytes written (0 if the buffer is too small)
inline size_t encodePacket(const Packet& pk, uint8_t* buf, size_t cap) {
    if (pk.n > cfg::HIST_MAX) return 0;
    size_t len = PKT_HEADER_SIZE + 2 * pk.n;
    if (cap < len) return 0;
    buf[0] = PKT_MAGIC;
    buf[1] = PKT_VERSION;
    buf[2] = pk.channel;
    buf[3] = pk.state;
    put16(buf + 4, pk.pktSeq);
    put16(buf + 6, pk.lastTid);
    put32(buf + 8, pk.txMs);
    buf[12] = pk.n;
    for (int i = 0; i < pk.n; i++)
        put16(buf + PKT_HEADER_SIZE + 2 * i, pk.age[i]);
    return len;
}

// Decode. Returns false if malformed (channel check is left to the caller)
inline bool decodePacket(const uint8_t* buf, size_t len, Packet& pk) {
    if (len < PKT_HEADER_SIZE) return false;
    if (buf[0] != PKT_MAGIC || buf[1] != PKT_VERSION) return false;
    if (buf[3] > 1) return false;
    uint8_t n = buf[12];
    if (n > cfg::HIST_MAX || len < PKT_HEADER_SIZE + 2 * (size_t)n)
        return false;
    pk.channel = buf[2];
    pk.state = buf[3];
    pk.pktSeq = get16(buf + 4);
    pk.lastTid = get16(buf + 6);
    pk.txMs = get32(buf + 8);
    pk.n = n;
    for (int i = 0; i < n; i++)
        pk.age[i] = get16(buf + PKT_HEADER_SIZE + 2 * i);
    return true;
}

// Restamp the transmit time of an encoded packet to newTxMs (the history ages
// are increased by the same amount). Called just before transmitting on paths
// where the packet waits between assembly and actual transmission (LoRa). If
// there was no transition since assembly (a new transition would cause it to be
// reassembled), the content is unchanged
inline bool restampPacket(uint8_t* buf, size_t len, uint32_t newTxMs) {
    if (len < PKT_HEADER_SIZE || buf[0] != PKT_MAGIC) return false;
    uint8_t n = buf[12];
    if (n > cfg::HIST_MAX || len < PKT_HEADER_SIZE + 2 * (size_t)n)
        return false;
    uint32_t d = newTxMs - get32(buf + 8);
    if (d >= 0x80000000u) return false;  // time never goes backwards
    put32(buf + 8, newTxMs);
    for (int i = 0; i < n; i++) {
        uint32_t a = get16(buf + PKT_HEADER_SIZE + 2 * i) + d;
        put16(buf + PKT_HEADER_SIZE + 2 * i, a > 0xFFFF ? 0xFFFF : (uint16_t)a);
    }
    return true;
}

// Rewrite the sequence number of an encoded packet. On paths that send only
// some of the assembled packets (LoRa), sequence numbers are reassigned in
// actual transmission order (because the receiver estimates loss by counting
// gaps in sequence numbers)
inline void setPacketSeq(uint8_t* buf, uint16_t seq) { put16(buf + 4, seq); }

// Sender: holds the history of key state transitions and assembles packets
class TxHistory {
  public:
    explicit TxHistory(uint16_t initialTid = 0) : lastTid_(initialTid) {}

    uint8_t state() const { return state_; }
    uint16_t lastTid() const { return lastTid_; }
    uint32_t lastChangeMs() const { return count_ ? times_[0] : 0; }

    // Record a state transition. Ignored (returns false) if the state is the
    // same
    bool push(uint8_t newState, uint32_t tMs) {
        if (newState == state_) return false;
        for (int i = cfg::HIST_MAX - 1; i > 0; i--) times_[i] = times_[i - 1];
        times_[0] = tMs;
        if (count_ < cfg::HIST_MAX) count_++;
        state_ = newState;
        lastTid_++;
        return true;
    }

    void build(Packet& pk, uint32_t nowMs, int nHist, uint8_t channel,
               uint16_t pktSeq) const {
        pk.channel = channel;
        pk.state = state_;
        pk.pktSeq = pktSeq;
        pk.lastTid = lastTid_;
        pk.txMs = nowMs;
        int n = nHist < count_ ? nHist : count_;
        if (n > cfg::HIST_MAX) n = cfg::HIST_MAX;
        pk.n = (uint8_t)n;
        for (int i = 0; i < n; i++) {
            uint32_t a = nowMs - times_[i];
            pk.age[i] = a > 0xFFFF ? 0xFFFF : (uint16_t)a;
        }
    }

  private:
    uint8_t state_ = 0;
    uint16_t lastTid_;
    uint32_t times_[cfg::HIST_MAX] = {};
    int count_ = 0;
};

// Transmit scheduler (sec. 4.2): decides when to send packets
class TxScheduler {
  public:
    // Idle heartbeat interval (varies by radio type)
    void setIdleInterval(uint32_t ms) { idleMs_ = ms; }

    // Call when a transition occurs (sent immediately on the next poll)
    void onTransition(uint8_t newState, uint32_t nowMs) {
        state_ = newState;
        lastChangeMs_ = nowMs;
        pending_ = true;
    }

    // Returns true if a packet should be sent now (updates the internal time as
    // if sent)
    bool poll(uint32_t nowMs) {
        bool active =
            state_ == 1 || (nowMs - lastChangeMs_) < cfg::TX_ACTIVE_HOLD_MS;
        uint32_t interval = active ? cfg::TX_INTERVAL_ACTIVE_MS : idleMs_;
        if (pending_ || !started_ || (nowMs - lastSendMs_) >= interval) {
            pending_ = false;
            started_ = true;
            lastSendMs_ = nowMs;
            return true;
        }
        return false;
    }

  private:
    uint8_t state_ = 0;
    uint32_t lastChangeMs_ = 0;
    uint32_t lastSendMs_ = 0;
    uint32_t idleMs_ = cfg::TX_INTERVAL_IDLE_MS;
    bool pending_ = false;
    bool started_ = false;
};

}  // namespace cw
