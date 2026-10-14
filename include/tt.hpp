/*
  Meridian, a UCI chess engine
  Copyright (C) 2026 Syed Taha

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "types.hpp"

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
     * One result of the transposition table, as a value.
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
     * Several search threads may read and write one table. A slot is two words, the data and
     * the key combined with the data, so that a slot torn by a concurrent write fails the
     * check and reads as a miss. An entry is replaced when it is from an earlier search
     * (generation) or when the new result is searched at least as deep. With two slots to a
     * bucket, the weaker of the two is replaced.
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
         * Chooses between one slot and a bucket of two for each position.
         */
        void set_buckets(bool two_way) noexcept {
            two_way_ = two_way;
        }

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
         * @param out Receives the entry on a hit.
         * @return True when the table holds this position.
         */
        bool probe(uint64_t key, TTEntry &out) const noexcept;

        /**
         * @brief Stores a search result.
         *
         * @param key Position hash.
         * @param move Best move found, or the null move.
         * @param score Score relative to the position (mate scores already made ply-independent).
         * @param depth Search depth that produced the score.
         * @param bound How the score bounds the true value.
         */
        void store(uint64_t key, Move move, int score, int depth, Bound bound) noexcept;

        /**
         * Number of slots.
         */
        size_t size() const noexcept {
            return mask_ + 1;
        }

        /**
         * Size in megabytes.
         */
        size_t megabytes() const noexcept {
            return size() * sizeof(Slot) / (1024 * 1024);
        }

    private:
        struct Slot {
            std::atomic<uint64_t> check{0};  ///< The key combined with `data`.
            std::atomic<uint64_t> data{0};
        };

        static uint64_t pack(Move move, int score, int depth, int bound, int generation) noexcept;
        static TTEntry unpack(uint64_t key, uint64_t data) noexcept;

        std::unique_ptr<Slot[]> slots_;
        size_t mask_ = 0;
        uint8_t generation_ = 0;
        bool two_way_ = false;
    };

}  // namespace chess
