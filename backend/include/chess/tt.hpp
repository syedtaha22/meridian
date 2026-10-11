#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "chess/types.hpp"

namespace chess {

    /**
     * How a stored score relates to the true value of its position.
     */
    enum Bound : uint8_t {
        kBoundNone = 0,
        kBoundExact = 1,  ///< The score is the value.
        kBoundLower = 2,  ///< The value is at least the score (a beta cutoff).
        kBoundUpper = 3,  ///< The value is at most the score (no move raised alpha).
    };

    /**
     * One slot of the transposition table.
     */
    struct TTEntry {
        uint64_t key = 0;
        Move move;
        int16_t score = 0;
        int8_t depth = 0;
        uint8_t bound = kBoundNone;
        uint8_t generation = 0;
    };

    /**
     * @brief A hash table of search results keyed by position hash.
     *
     * One entry per slot. An entry is replaced when it is from an earlier search
     * (generation) or when the new result is searched at least as deep.
     */
    class TranspositionTable {
    public:
        /**
         * @brief Allocates a table.
         *
         * @param megabytes Approximate size; rounded down to a power-of-two number of slots.
         */
        explicit TranspositionTable(size_t megabytes = 16);

        /**
         * @brief Resizes the table and clears it.
         *
         * @param megabytes Approximate size in megabytes.
         */
        void resize(size_t megabytes);

        /**
         * Empties the table.
         */
        void clear();

        /**
         * Marks the start of a new search, so older entries may be replaced.
         */
        void new_generation() noexcept {
            ++generation_;
        }

        /**
         * @brief Looks up a position.
         *
         * @param key Position hash.
         * @return The entry when the slot holds this position, otherwise null.
         */
        const TTEntry *probe(uint64_t key) const noexcept {
            const TTEntry &slot = entries_[key & mask_];
            return slot.key == key && slot.bound != kBoundNone ? &slot : nullptr;
        }

        /**
         * @brief Stores a search result.
         *
         * @param key Position hash.
         * @param move Best move found, or the null move.
         * @param score Score relative to the position (mate scores already made ply-independent).
         * @param depth Search depth that produced the score.
         * @param bound How the score bounds the true value.
         */
        void store(uint64_t key, Move move, int score, int depth, Bound bound) noexcept {
            TTEntry &slot = entries_[key & mask_];

            if (slot.key == key && move.is_null()) {
                move = slot.move;  // keep a known best move when this search found none
            }
            if (slot.bound == kBoundNone || slot.generation != generation_ || slot.key == key ||
                depth >= slot.depth) {
                slot.key = key;
                slot.move = move;
                slot.score = static_cast<int16_t>(score);
                slot.depth = static_cast<int8_t>(depth);
                slot.bound = bound;
                slot.generation = generation_;
            }
        }

        /**
         * Number of slots.
         */
        size_t size() const noexcept {
            return entries_.size();
        }

        /**
         * Size in megabytes.
         */
        size_t megabytes() const noexcept {
            return entries_.size() * sizeof(TTEntry) / (1024 * 1024);
        }

    private:
        std::vector<TTEntry> entries_;
        size_t mask_ = 0;
        uint8_t generation_ = 0;
    };

}  // namespace chess
