#include "chess/attacks.hpp"

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
         * Finds a multiplier that maps every occupancy subset of `mask` to a slot
         * of a table of 2^bits entries without two subsets with different attacks
         * sharing a slot, and fills `table` with the attack sets.
         */
        template <size_t N>
        void init_magic(Square square, const std::array<Direction, N> &directions,
                        detail::Magic &entry, std::vector<Bitboard> &storage, size_t offset,
                        Random &random) {
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
                    entry.mask = mask;
                    entry.magic = magic;
                    entry.shift = 64 - bits;

                    for (size_t i = 0; i < size; ++i) {
                        storage[offset + i] = table[i];
                    }
                    return;
                }
            }
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
            Random random;
            size_t rook_offset = 0;
            size_t bishop_offset = 0;

            for (Square square = 0; square < kSquares; ++square) {
                init_magic(square, kRookDirections, detail::rook_magics[square], rook_storage,
                           rook_offset, random);
                detail::rook_magics[square].attacks = rook_storage.data() + rook_offset;
                rook_offset += size_t{1} << (64 - detail::rook_magics[square].shift);

                init_magic(square, kBishopDirections, detail::bishop_magics[square], bishop_storage,
                           bishop_offset, random);
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
