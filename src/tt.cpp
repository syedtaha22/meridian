#include "chess/tt.hpp"

#include <algorithm>

namespace chess {

    TranspositionTable::TranspositionTable(size_t megabytes) {
        resize(megabytes);
    }

    void TranspositionTable::resize(size_t megabytes) {
        size_t count = 2;
        const size_t wanted = megabytes * 1024 * 1024 / sizeof(Slot);

        while (count * 2 <= wanted) {
            count *= 2;
        }

        slots_ = std::make_unique<Slot[]>(count);
        mask_ = count - 1;
        generation_ = 0;
    }

    void TranspositionTable::clear() {
        for (size_t i = 0; i <= mask_; ++i) {
            slots_[i].check.store(0, std::memory_order_relaxed);
            slots_[i].data.store(0, std::memory_order_relaxed);
        }

        generation_ = 0;
    }

    uint64_t TranspositionTable::pack(Move move, int score, int depth, int bound,
                                      int generation) noexcept {
        return static_cast<uint64_t>(move.data) |
               (static_cast<uint64_t>(static_cast<uint16_t>(score)) << 16) |
               (static_cast<uint64_t>(static_cast<uint8_t>(depth)) << 32) |
               (static_cast<uint64_t>(static_cast<uint8_t>(bound)) << 40) |
               (static_cast<uint64_t>(static_cast<uint8_t>(generation)) << 48);
    }

    TTEntry TranspositionTable::unpack(uint64_t key, uint64_t data) noexcept {
        TTEntry entry;
        entry.key = key;
        entry.move.data = static_cast<uint16_t>(data & 0xFFFF);
        entry.score = static_cast<int16_t>((data >> 16) & 0xFFFF);
        entry.depth = static_cast<int8_t>((data >> 32) & 0xFF);
        entry.bound = static_cast<uint8_t>((data >> 40) & 0xFF);
        entry.generation = static_cast<uint8_t>((data >> 48) & 0xFF);
        return entry;
    }

    bool TranspositionTable::probe(uint64_t key, TTEntry &out) const noexcept {
        const size_t first = key & mask_ & (two_way_ ? ~size_t{1} : ~size_t{0});
        const size_t count = two_way_ ? 2 : 1;

        for (size_t i = 0; i < count; ++i) {
            const Slot &slot = slots_[first + i];
            const uint64_t data = slot.data.load(std::memory_order_relaxed);
            const uint64_t check = slot.check.load(std::memory_order_relaxed);

            if ((check ^ data) == key && ((data >> 40) & 0xFF) != kBoundNone) {
                out = unpack(key, data);
                return true;
            }
        }

        return false;
    }

    void TranspositionTable::store(uint64_t key, Move move, int score, int depth,
                                   Bound bound) noexcept {
        const size_t first = key & mask_ & (two_way_ ? ~size_t{1} : ~size_t{0});
        const size_t count = two_way_ ? 2 : 1;

        // The slot that holds this position, or else the weakest one of the bucket.
        size_t chosen = first;
        int weakest = 1 << 30;
        bool found = false;

        for (size_t i = 0; i < count; ++i) {
            const Slot &slot = slots_[first + i];
            const uint64_t data = slot.data.load(std::memory_order_relaxed);
            const uint64_t check = slot.check.load(std::memory_order_relaxed);
            const TTEntry old = unpack(check ^ data, data);

            if ((check ^ data) == key && old.bound != kBoundNone) {
                chosen = first + i;
                found = true;

                if (move.is_null()) {
                    move = old.move;  // keep a known best move when this search found none
                }

                break;
            }

            // An entry of an earlier search counts as shallower, by 8 plies for each search.
            const int age = static_cast<uint8_t>(generation_ - old.generation);
            const int value = old.bound == kBoundNone ? -1000 : old.depth - 8 * age;

            if (value < weakest) {
                weakest = value;
                chosen = first + i;
            }
        }

        Slot &slot = slots_[chosen];

        if (!found && !two_way_) {
            // One slot: replace an empty slot, an older search, or a shallower result.
            const uint64_t data = slot.data.load(std::memory_order_relaxed);
            const uint64_t check = slot.check.load(std::memory_order_relaxed);
            const TTEntry old = unpack(check ^ data, data);

            if (old.bound != kBoundNone && old.generation == generation_ && depth < old.depth) {
                return;
            }
        }

        const uint64_t data = pack(move, score, depth, bound, generation_);
        slot.data.store(data, std::memory_order_relaxed);
        slot.check.store(key ^ data, std::memory_order_relaxed);
    }

}  // namespace chess
