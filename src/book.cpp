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

#include "book.hpp"

#include <algorithm>

#include "movegen.hpp"

namespace chess {

    namespace {

#include "book_data.inc"

    }  // namespace

    const Book &Book::builtin() {
        static const Book book(kBookData, kBookDepth);
        return book;
    }

    Book::Book(std::span<const BookEntry> entries, int depth) noexcept
        : entries_(entries), depth_(depth) {
    }

    std::span<const BookEntry> Book::find(uint64_t key) const noexcept {
        const auto first = std::lower_bound(
            entries_.begin(), entries_.end(), key,
            [](const BookEntry &entry, uint64_t value) { return entry.key < value; });
        auto last = first;

        while (last != entries_.end() && last->key == key) {
            ++last;
        }
        return {first, last};
    }

    std::optional<BookChoice> Book::pick(const Position &pos, uint64_t random) const {
        const auto found = find(pos.key());

        if (found.empty()) {
            return std::nullopt;
        }

        MoveList legal;
        generate_legal(pos, legal);

        // Only moves that are legal here count, so that a hash collision cannot give an
        // illegal move.
        BookEntry options[64];
        size_t count = 0;
        uint64_t total = 0;

        for (const BookEntry &entry : found) {
            const bool is_legal = std::any_of(legal.begin(), legal.end(),
                                              [&](Move move) { return move.data == entry.move; });

            if (is_legal && count < 64) {
                options[count++] = entry;
                total += entry.weight;
            }
        }

        if (count == 0 || total == 0) {
            return std::nullopt;
        }

        uint64_t pick = random % total;

        for (size_t i = 0; i < count; ++i) {
            if (pick < options[i].weight) {
                Move move;
                move.data = options[i].move;
                return BookChoice{move, options[i].score, depth_};
            }
            pick -= options[i].weight;
        }
        return std::nullopt;
    }

}  // namespace chess
