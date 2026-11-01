// peers.h - multi-station reception: a Receiver and decoder per source (MAC
// address) Arduino-independent. Even with 3 or more stations on the same
// channel, transitions are reconstructed and played back per station. The tone
// is the OR of all stations' playback states; decoding is per station.
#pragma once
#include <stdint.h>
#include <string.h>
#include "config.h"
#include "jitter.h"
#include "morse_decoder.h"

namespace cw {

struct Peer {
    bool used = false;
    uint8_t mac[6] = {};
    Receiver rx;
    MorseDecoder dec;
    int8_t rssi = 0;
    uint32_t lastRxMs = 0;    // time the last packet was received
    uint32_t lastPlayMs = 0;  // time the last transition was played back
    uint32_t lastEventMs =
        0;  // for logs: playback time of the previous transition
};

class PeerTable {
  public:
    static constexpr int MAX = cfg::MAX_PEERS;

    void setDelay(uint32_t ms) {
        delay_ = ms;
        for (auto& p : peers_)
            if (p.used) p.rx.setDelay(ms);
    }

    // Return the slot for the source (allocated if absent). index receives
    // 0..MAX-1
    Peer& get(const uint8_t mac[6], uint32_t nowMs, int& index) {
        int freeSlot = -1, oldest = -1;
        for (int i = 0; i < MAX; i++) {
            Peer& p = peers_[i];
            if (p.used && memcmp(p.mac, mac, 6) == 0) {
                index = i;
                return p;
            }
            if (!p.used) {
                if (freeSlot < 0) freeSlot = i;
            } else if (oldest < 0 ||
                       timeBefore(p.lastRxMs, peers_[oldest].lastRxMs)) {
                oldest = i;
            }
        }
        // If there is no free slot, evict the station not heard from for the
        // longest time
        int i = freeSlot >= 0 ? freeSlot : oldest;
        if (peers_[i].used) retire(peers_[i]);
        Peer& p = peers_[i];
        p = Peer();
        p.used = true;
        memcpy(p.mac, mac, 6);
        p.rx.setDelay(delay_);
        p.lastRxMs = nowMs;
        index = i;
        return p;
    }

    Peer& at(int i) { return peers_[i]; }

    // On channel switch etc.: forget all stations (sound being played back also
    // stops)
    void clear() {
        for (auto& p : peers_) {
            if (p.used) retire(p);
            p.used = false;
        }
    }

    // Whether any station is playing back DOWN
    bool anyDown() const {
        for (auto& p : peers_)
            if (p.used && p.rx.state()) return true;
        return false;
    }

    // Number of stations with a live link
    int activeCount(uint32_t nowMs) const {
        int n = 0;
        for (auto& p : peers_)
            if (p.used && p.rx.linkUp(nowMs)) n++;
        return n;
    }

    // Station from which the last packet was received (nullptr if none)
    const Peer* latestRx() const {
        return latest([](const Peer& p) { return p.lastRxMs; });
    }
    // Station whose transition was played back last (nullptr if none)
    const Peer* latestPlay() const {
        return latest([](const Peer& p) { return p.lastPlayMs; });
    }

    // Sum of the statistics of all stations (including evicted ones)
    RxStats totalStats() const {
        RxStats s = retired_;
        for (auto& p : peers_)
            if (p.used) add(s, p.rx.stats());
        return s;
    }

  private:
    template <typename F>
    const Peer* latest(F key) const {
        const Peer* best = nullptr;
        for (auto& p : peers_)
            if (p.used && (!best || timeBefore(key(*best), key(p)))) best = &p;
        return best;
    }

    static void add(RxStats& a, const RxStats& b) {
        a.packets += b.packets;
        a.lost += b.lost;
        a.stale += b.stale;
        a.resyncs += b.resyncs;
        a.late += b.late;
        a.watchdogs += b.watchdogs;
        a.reasserts += b.reasserts;
        a.restarts += b.restarts;
        if (timeBefore(a.lastRxMs, b.lastRxMs)) a.lastRxMs = b.lastRxMs;
    }

    void retire(const Peer& p) { add(retired_, p.rx.stats()); }

    Peer peers_[MAX];
    RxStats retired_;
    uint32_t delay_ = cfg::JITTER_DELAY_MS;
};

}  // namespace cw
