// jitter.h - receiver: transition reconstruction, jitter buffer playback,
// watchdog (sec. 4.1, 4.5) Arduino-independent. Times are passed as the
// receiver's local ms (uint32 with wraparound).
//
// Playback timeline: a transition at sender time T is played back at receiver
// time T + offset + delay.
//   offset = minimum of (rxMs - txMs) over the last few seconds (the packet
//   with the smallest network delay)
// Watchdog: if the playback position passes "transmit time of the latest
// received packet + WATCHDOG_MS"
//   and the state is still DOWN, assume information was lost and force it to
//   UP. If a DOWN packet with the same transition ID arrives afterwards (just a
//   loss), DOWN is resumed.
#pragma once
#include <stdint.h>
#include "config.h"
#include "protocol.h"

namespace cw {

inline bool timeBefore(uint32_t a, uint32_t b) { return (int32_t)(a - b) < 0; }

// Clock offset estimation (minimum per bucket, minimum over the last
// OFFSET_BUCKETS buckets)
class OffsetEstimator {
  public:
    void reset() { valid_ = false; }

    void addSample(int32_t off, uint32_t nowMs) {
        if (!valid_) {
            valid_ = true;
            bucketStart_ = nowMs;
            head_ = 0;
            for (int i = 0; i < cfg::OFFSET_BUCKETS; i++) has_[i] = false;
        }
        while (nowMs - bucketStart_ >= cfg::OFFSET_BUCKET_MS) {
            bucketStart_ += cfg::OFFSET_BUCKET_MS;
            head_ = (head_ + 1) % cfg::OFFSET_BUCKETS;
            has_[head_] = false;
            // Keep the while loop from spinning for long after a long gap
            if (nowMs - bucketStart_ >=
                cfg::OFFSET_BUCKET_MS * cfg::OFFSET_BUCKETS) {
                for (int i = 0; i < cfg::OFFSET_BUCKETS; i++) has_[i] = false;
                bucketStart_ = nowMs;
            }
        }
        if (!has_[head_] || off < min_[head_]) {
            min_[head_] = off;
            has_[head_] = true;
        }
    }

    bool valid() const { return valid_; }

    int32_t value() const {
        bool any = false;
        int32_t m = 0;
        for (int i = 0; i < cfg::OFFSET_BUCKETS; i++) {
            if (has_[i] && (!any || min_[i] < m)) {
                m = min_[i];
                any = true;
            }
        }
        return m;
    }

  private:
    bool valid_ = false;
    uint32_t bucketStart_ = 0;
    int head_ = 0;
    int32_t min_[cfg::OFFSET_BUCKETS] = {};
    bool has_[cfg::OFFSET_BUCKETS] = {};
};

enum class EventKind : uint8_t {
    Normal,    // transition reconstructed from the packet history
    Forced,    // forced UP by the watchdog
    Reassert,  // resumed after forced UP when DOWN turned out to be continuing
};

// A played-back transition (for the decoder / logs)
struct PlayedEvent {
    uint32_t playMs;  // time on the playback timeline (may be in the past for
                      // late arrivals)
    uint8_t state;
    EventKind kind;
};

struct RxStats {
    uint32_t packets = 0;  // accepted packets
    uint32_t lost = 0;     // loss estimated from gaps in pktSeq
    uint32_t stale = 0;  // duplicate / old packets (out of order, containing no
                         // new transitions)
    uint32_t resyncs = 0;  // resyncs due to losses exceeding the history count
    uint32_t late = 0;     // transitions that arrived after their playback time
    uint32_t watchdogs = 0;  // forced UPs by the watchdog
    uint32_t reasserts = 0;  // DOWN resumptions after forced UP
    uint32_t restarts =
        0;  // initializations (first packet, link loss, sender restart)
    uint32_t lastRxMs = 0;
};

class Receiver {
  public:
    static constexpr int QUEUE_SIZE = 64;
    static constexpr int OUT_SIZE = 64;

    void setDelay(uint32_t ms) { delay_ = ms; }
    uint32_t delay() const { return delay_; }

    // Current playback state (for tone output)
    uint8_t state() const { return playState_; }
    bool linkUp(uint32_t nowMs) const {
        return init_ && (nowMs - stats_.lastRxMs) < cfg::LINK_TIMEOUT_MS;
    }
    int32_t offset() const { return offset_.value(); }
    const RxStats& stats() const { return stats_; }

    // Call on packet reception. rxMs is the local time at the moment of
    // reception
    void onPacket(const Packet& pk, uint32_t rxMs) {
        bool reinit =
            !init_ || (rxMs - stats_.lastRxMs) >= cfg::LINK_TIMEOUT_MS ||
            (int32_t)(pk.txMs - horizonTx_) < -cfg::SENDER_RESTART_BACKSTEP_MS;

        if (init_ && !reinit) {
            countSeq(pk.pktSeq);
        } else {
            lastSeq_ = pk.pktSeq;
            seqSeen_ = 1;
        }
        stats_.packets++;
        stats_.lastRxMs = rxMs;

        if (reinit) {
            offset_.reset();
            stats_.restarts++;
        }
        offset_.addSample((int32_t)(rxMs - pk.txMs), rxMs);

        if (reinit) {
            init_ = true;
            horizonTx_ = pk.txMs;
            appliedTid_ = pk.lastTid;
            forcedUp_ = false;
            qHead_ = qTail_ = 0;
            lastQueuedMs_ = rxMs;
            lastQueuedState_ = playState_;
            // On the first packet, adopt only the latest transition (old
            // history is not played back)
            if (pk.state != playState_) {
                uint32_t t = pk.n > 0 ? pk.txMs - pk.age[0] : pk.txMs;
                enqueue(playTime(t), pk.state, EventKind::Normal, rxMs);
            }
            return;
        }

        if (timeBefore(horizonTx_, pk.txMs)) horizonTx_ = pk.txMs;

        uint16_t d = pk.lastTid - appliedTid_;
        if (d == 0) {
            // No new transition. If DOWN turns out to be continuing during
            // forced UP, resume it
            if (forcedUp_ && pk.state == 1 && lastQueuedState_ == 0) {
                enqueue(playTime(pk.txMs), 1, EventKind::Reassert, rxMs);
                stats_.reasserts++;
                forcedUp_ = false;
            }
            return;
        }
        if (d >= 0x8000) {  // packet older than what we have (out of order /
                            // duplicate)
            stats_.stale++;
            return;
        }

        forcedUp_ = false;
        int avail = pk.n;
        if (d > avail) {
            // Loss exceeding the history count: resync with what is visible
            stats_.resyncs++;
            if (avail == 0) {
                enqueue(playTime(pk.txMs), pk.state, EventKind::Normal, rxMs);
            }
        }
        int first = (d < avail ? d : avail) - 1;
        for (int i = first; i >= 0; i--) {
            enqueue(playTime(pk.txMs - pk.age[i]), pk.stateOf(i),
                    EventKind::Normal, rxMs);
        }
        appliedTid_ = pk.lastTid;
    }

    // Call periodically (about every 1ms). Applies transitions whose playback
    // time has come and checks the watchdog
    void update(uint32_t nowMs) {
        while (qHead_ != qTail_ && !timeBefore(nowMs, queue_[qHead_].playMs)) {
            apply(queue_[qHead_]);
            qHead_ = (qHead_ + 1) % QUEUE_SIZE;
        }
        if (init_ && playState_ == 1 && qHead_ == qTail_) {
            uint32_t horizonPlay = playTime(horizonTx_);
            if ((int32_t)(nowMs - horizonPlay) > (int32_t)cfg::WATCHDOG_MS) {
                PlayedEvent ev{nowMs, 0, EventKind::Forced};
                lastQueuedMs_ = nowMs;
                lastQueuedState_ = 0;
                apply(ev);
                forcedUp_ = true;
                stats_.watchdogs++;
            }
        }
    }

    // Fetch played-back transitions
    bool pop(PlayedEvent& ev) {
        if (outHead_ == outTail_) return false;
        ev = out_[outHead_];
        outHead_ = (outHead_ + 1) % OUT_SIZE;
        return true;
    }

  private:
    // Count losses from pktSeq. Uses a reception bitmap of the last 64 sequence
    // numbers; late arrivals are subtracted
    void countSeq(uint16_t seq) {
        uint16_t ds = seq - lastSeq_;
        if (ds == 0) return;
        if (ds < 0x8000) {
            stats_.lost += ds - 1;
            seqSeen_ = ds < 64 ? (seqSeen_ << ds) | 1 : 1;
            lastSeq_ = seq;
        } else {
            uint16_t back = lastSeq_ - seq;
            if (back < 64 && !(seqSeen_ & (1ULL << back))) {
                seqSeen_ |= 1ULL << back;
                if (stats_.lost) stats_.lost--;
            }
        }
    }

    uint32_t playTime(uint32_t senderMs) const {
        return senderMs + (uint32_t)offset_.value() + delay_;
    }

    void enqueue(uint32_t playMs, uint8_t st, EventKind kind, uint32_t nowMs) {
        if (st == lastQueuedState_)
            return;  // discard non-alternating transitions (during resync)
        if (timeBefore(playMs, nowMs)) stats_.late++;
        if (timeBefore(playMs, lastQueuedMs_))
            playMs = lastQueuedMs_;  // keep the timeline monotonic
        int next = (qTail_ + 1) % QUEUE_SIZE;
        if (next ==
            qHead_) {  // overflow: apply the oldest immediately to make room
            apply(queue_[qHead_]);
            qHead_ = (qHead_ + 1) % QUEUE_SIZE;
        }
        queue_[qTail_] = PlayedEvent{playMs, st, kind};
        qTail_ = next;
        lastQueuedMs_ = playMs;
        lastQueuedState_ = st;
    }

    void apply(const PlayedEvent& ev) {
        if (ev.state == playState_) return;
        playState_ = ev.state;
        int next = (outTail_ + 1) % OUT_SIZE;
        if (next == outHead_)
            outHead_ =
                (outHead_ + 1) % OUT_SIZE;  // discard old entries if not read
        out_[outTail_] = ev;
        outTail_ = next;
    }

    uint32_t delay_ = cfg::JITTER_DELAY_MS;
    bool init_ = false;
    bool forcedUp_ = false;
    uint8_t playState_ = 0;
    uint16_t appliedTid_ = 0;
    uint16_t lastSeq_ = 0;
    uint64_t seqSeen_ =
        0;  // bit k = sequence number lastSeq_-k has been received
    uint32_t horizonTx_ = 0;
    uint32_t lastQueuedMs_ = 0;
    uint8_t lastQueuedState_ = 0;
    OffsetEstimator offset_;
    RxStats stats_;

    PlayedEvent queue_[QUEUE_SIZE];
    int qHead_ = 0, qTail_ = 0;
    PlayedEvent out_[OUT_SIZE];
    int outHead_ = 0, outTail_ = 0;
};

}  // namespace cw
