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

// Finds the magic numbers of the slider attacks and prints them as C++ source.
//
// Usage: find_magics [--out src/magic_numbers.inc]
//
// A magic number is a multiplier that maps every occupancy of the squares that can block a rook or
// a bishop to a slot of a table of 2^bits entries, without two occupancies with different attacks
// sharing a slot. The search tries sparse random numbers from a fixed sequence, square by square
// (the rook of each square, then its bishop), so that every run prints the same numbers. The
// numbers are printed once, to the file of `--out` or to the standard output, and the library
// reads them from `src/magic_numbers.inc`, so that it does not search at start-up.
//
// The program starts from the rules of the board alone. `position_test` checks every lookup of the
// library against walking the rays.

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "../bench/common.hpp"
#include "types.hpp"

namespace {

    using namespace chess;

    struct Direction {
        int file;
        int rank;
    };

    constexpr std::array<Direction, 4> kRookDirections = {{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
    constexpr std::array<Direction, 4> kBishopDirections = {{{1, 1}, {1, -1}, {-1, 1}, {-1, -1}}};

    bool on_board(int file, int rank) {
        return file >= 0 && file < 8 && rank >= 0 && rank < 8;
    }

    /**
     * Walks each ray from `square` until the board edge or the first blocker.
     */
    Bitboard walk(Square square, Bitboard occupied, const std::array<Direction, 4> &directions) {
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
    Bitboard relevant_mask(Square square, const std::array<Direction, 4> &directions) {
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

    /**
     * Deterministic generator, so the magic numbers found do not change between runs.
     */
    struct Random {
        uint64_t state = 0x9E3779B97F4A7C15ULL;

        uint64_t next() {
            state ^= state >> 12;
            state ^= state << 25;
            state ^= state >> 27;
            return state * 0x2545F4914F6CDD1DULL;
        }

        /**
         * Few set bits: sparse numbers make better multipliers.
         */
        uint64_t sparse() {
            return next() & next() & next();
        }
    };

    /**
     * Finds a multiplier that maps every occupancy subset of the relevant squares to a slot of a
     * table of 2^bits entries, without two subsets with different attacks sharing a slot.
     */
    uint64_t find_magic(Square square, const std::array<Direction, 4> &directions, Random &random) {
        const Bitboard mask = relevant_mask(square, directions);
        const int bits = __builtin_popcountll(mask);
        const size_t size = size_t{1} << bits;

        std::vector<Bitboard> occupancies(size);
        std::vector<Bitboard> references(size);
        Bitboard subset = 0;

        for (size_t i = 0; i < size; ++i) {
            occupancies[i] = subset;
            references[i] = walk(square, subset, directions);
            subset = (subset - mask) & mask;  // next subset (carry-rippler)
        }

        std::vector<Bitboard> table(size);
        std::vector<int> epoch(size, 0);
        int attempt = 0;

        while (true) {
            const Bitboard magic = random.sparse();

            if (__builtin_popcountll((mask * magic) >> 56) < 6) {
                continue;
            }

            ++attempt;
            bool ok = true;

            for (size_t i = 0; i < size && ok; ++i) {
                const size_t index = (occupancies[i] * magic) >> (64 - bits);

                if (epoch[index] != attempt) {
                    epoch[index] = attempt;
                    table[index] = references[i];
                } else if (table[index] != references[i]) {
                    ok = false;
                }
            }

            if (ok) {
                return magic;
            }
        }
    }

    std::string array_source(const char *name, const std::vector<uint64_t> &magics) {
        std::ostringstream out;
        out << "constexpr uint64_t " << name << "[kSquares] = {\n";

        for (size_t i = 0; i < magics.size(); ++i) {
            char text[32];
            std::snprintf(text, sizeof text, "0x%016llxULL,",
                          static_cast<unsigned long long>(magics[i]));
            out << (i % 4 == 0 ? "    " : " ") << text << (i % 4 == 3 ? "\n" : "");
        }

        out << "};\n";
        return out.str();
    }

}  // namespace

int main(int argc, char **argv) {
    const bench::Args args(argc, argv);
    Random random;
    std::vector<uint64_t> rook;
    std::vector<uint64_t> bishop;

    for (Square square = 0; square < kSquares; ++square) {
        rook.push_back(find_magic(square, kRookDirections, random));
        bishop.push_back(find_magic(square, kBishopDirections, random));
    }

    std::ostringstream source;
    source << "// Magic numbers of the rook and the bishop on each square, written by\n"
           << "// tools/find_magics.\n\n"
           << array_source("kRookMagics", rook) << "\n"
           << array_source("kBishopMagics", bishop);

    if (args.has("out")) {
        std::ofstream file(args.get("out", ""));
        file << source.str();
        std::cout << "written to " << args.get("out", "") << std::endl;
    } else {
        std::cout << source.str();
    }

    return 0;
}
