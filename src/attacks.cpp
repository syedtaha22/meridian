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

#include "attacks.hpp"

#ifdef CHESS_PEXT
#include <immintrin.h>
#endif

#include <array>
#include <cstdlib>
#include <vector>

namespace chess {

    namespace detail {

        Bitboard knight_table[kSquares];
        Bitboard king_table[kSquares];
        Bitboard pawn_table[2][kSquares];
        Magic rook_magics[kSquares];
        Magic bishop_magics[kSquares];

    }  // namespace detail

    namespace {

        struct Direction {
            int file;
            int rank;
        };

        constexpr std::array<Direction, 4> kRookDirections = {{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
        constexpr std::array<Direction, 4> kBishopDirections = {
            {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}}};

        bool on_board(int file, int rank) {
            return file >= 0 && file < 8 && rank >= 0 && rank < 8;
        }

        /**
         * Walks each ray from `square` until the board edge or the first blocker.
         */
        template <size_t N>
        Bitboard walk(Square square, Bitboard occupied,
                      const std::array<Direction, N> &directions) {
            Bitboard attacks = 0;

            for (const Direction &d : directions) {
                int file = file_of(square) + d.file;
                int rank = rank_of(square) + d.rank;

                while (on_board(file, rank)) {
                    const Bitboard bit = square_bb(make_square(file, rank));
                    attacks |= bit;

                    if (occupied & bit) {
                        break;
                    }

                    file += d.file;
                    rank += d.rank;
                }
            }

            return attacks;
        }

        /**
         * Squares whose occupancy can change the attacks: rays without their last square.
         */
        template <size_t N>
        Bitboard relevant_mask(Square square, const std::array<Direction, N> &directions) {
            Bitboard mask = 0;

            for (const Direction &d : directions) {
                int file = file_of(square) + d.file;
                int rank = rank_of(square) + d.rank;

                while (on_board(file + d.file, rank + d.rank)) {
                    mask |= square_bb(make_square(file, rank));
                    file += d.file;
                    rank += d.rank;
                }
            }

            return mask;
        }

        // The multipliers of the rook and the bishop on each square, found by a search.
        // `position_test` checks every lookup against walking the rays.
#include "magic_numbers.inc"

        /**
         * Fills the attack table of one square for a multiplier from the tables above.
         */
        template <size_t N>
        void init_magic(Square square, const std::array<Direction, N> &directions,
                        detail::Magic &entry, std::vector<Bitboard> &storage, size_t offset,
                        uint64_t magic) {
            const Bitboard mask = relevant_mask(square, directions);
            const int bits = __builtin_popcountll(mask);
            const size_t size = size_t{1} << bits;

#ifdef CHESS_PEXT
            // The lookup is a parallel bit extract of the occupancy, so the table is in the
            // order of the extracted index and needs no multiplier.
            for (size_t i = 0; i < size; ++i) {
                storage[offset + i] = walk(square, _pdep_u64(i, mask), directions);
            }

            entry.mask = mask;
            entry.magic = 0;
            entry.shift = 64 - bits;
            static_cast<void>(magic);
#else
            Bitboard subset = 0;

            for (size_t i = 0; i < size; ++i) {
                storage[offset + (subset * magic >> (64 - bits))] =
                    walk(square, subset, directions);
                subset = (subset - mask) & mask;  // next subset (carry-rippler)
            }

            entry.mask = mask;
            entry.magic = magic;
            entry.shift = 64 - bits;
#endif
        }

        void init_tables() {
            for (Square square = 0; square < kSquares; ++square) {
                const int file = file_of(square);
                const int rank = rank_of(square);

                Bitboard knight = 0;

                for (int df : {-2, -1, 1, 2}) {
                    for (int dr : {-2, -1, 1, 2}) {
                        if (std::abs(df) != std::abs(dr) && on_board(file + df, rank + dr)) {
                            knight |= square_bb(make_square(file + df, rank + dr));
                        }
                    }
                }

                detail::knight_table[square] = knight;

                Bitboard king = 0;

                for (int df = -1; df <= 1; ++df) {
                    for (int dr = -1; dr <= 1; ++dr) {
                        if ((df != 0 || dr != 0) && on_board(file + df, rank + dr)) {
                            king |= square_bb(make_square(file + df, rank + dr));
                        }
                    }
                }

                detail::king_table[square] = king;

                for (Color color : {kWhite, kBlack}) {
                    const int step = color == kWhite ? 1 : -1;
                    Bitboard pawn = 0;

                    for (int df : {-1, 1}) {
                        if (on_board(file + df, rank + step)) {
                            pawn |= square_bb(make_square(file + df, rank + step));
                        }
                    }

                    detail::pawn_table[color][square] = pawn;
                }
            }

            // Both tables live in static storage so that entries can hold pointers into them.
            static std::vector<Bitboard> rook_storage(102400);
            static std::vector<Bitboard> bishop_storage(5248);
            size_t rook_offset = 0;
            size_t bishop_offset = 0;

            for (Square square = 0; square < kSquares; ++square) {
                init_magic(square, kRookDirections, detail::rook_magics[square], rook_storage,
                           rook_offset, kRookMagics[square]);
                detail::rook_magics[square].attacks = rook_storage.data() + rook_offset;
                rook_offset += size_t{1} << (64 - detail::rook_magics[square].shift);

                init_magic(square, kBishopDirections, detail::bishop_magics[square], bishop_storage,
                           bishop_offset, kBishopMagics[square]);
                detail::bishop_magics[square].attacks = bishop_storage.data() + bishop_offset;
                bishop_offset += size_t{1} << (64 - detail::bishop_magics[square].shift);
            }
        }

        // Runs before main so the tables are ready for every user of this library.
        [[maybe_unused]] const bool kTablesReady = (init_tables(), true);

    }  // namespace

    Bitboard rook_attacks_rays(Square square, Bitboard occupied) noexcept {
        return walk(square, occupied, kRookDirections);
    }

    Bitboard bishop_attacks_rays(Square square, Bitboard occupied) noexcept {
        return walk(square, occupied, kBishopDirections);
    }

}  // namespace chess
