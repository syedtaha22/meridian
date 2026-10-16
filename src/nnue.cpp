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

#include "nnue.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>

// On x86-64, the functions that add and sum rows of weights are compiled twice, once for AVX2 and
// once for the base instruction set, and the program picks one when it starts.
#if defined(__x86_64__) && defined(__GNUC__) && !defined(__clang__)
#define CHESS_SIMD_CLONES __attribute__((target_clones("avx2", "default")))
#else
#define CHESS_SIMD_CLONES
#endif

#define CHESS_INLINE inline __attribute__((always_inline))

namespace chess::nnue {

    namespace {

        constexpr char kMagic[8] = {'M', 'R', 'D', 'N', 'N', 'U', 'E', '3'};

        CHESS_INLINE void add_row(int16_t *values, const int16_t *row) {
            for (int i = 0; i < kHidden; ++i) {
                values[i] = static_cast<int16_t>(values[i] + row[i]);
            }
        }

        CHESS_INLINE void sub_row(int16_t *values, const int16_t *row) {
            for (int i = 0; i < kHidden; ++i) {
                values[i] = static_cast<int16_t>(values[i] - row[i]);
            }
        }

        CHESS_INLINE void add_piece(const Network &network, Accumulator &acc, Piece piece,
                                    Square square) {
            for (int p = 0; p < 2; ++p) {
                add_row(
                    acc.values[p],
                    network.feature_weights[king_feature(p, piece, square, acc.kb[p], acc.mir[p])]);
            }
        }

        CHESS_INLINE void remove_piece(const Network &network, Accumulator &acc, Piece piece,
                                       Square square) {
            for (int p = 0; p < 2; ++p) {
                sub_row(
                    acc.values[p],
                    network.feature_weights[king_feature(p, piece, square, acc.kb[p], acc.mir[p])]);
            }
        }

        // Computes one point of view of an accumulator from the pieces of a position.
        CHESS_INLINE void refresh_perspective(const Network &network, const Position &pos, int p,
                                              Accumulator &out) {
            int kb = 0, mir = 0;
            king_context(p, pos.king_square(p == 0 ? kWhite : kBlack), kb, mir);
            out.kb[p] = static_cast<uint8_t>(kb);
            out.mir[p] = static_cast<uint8_t>(mir);
            std::memcpy(out.values[p], network.feature_bias, sizeof(out.values[p]));

            for (int square = 0; square < kSquares; ++square) {
                const Piece piece = pos.piece_on(static_cast<Square>(square));

                if (piece != kNoPiece) {
                    add_row(out.values[p], network.feature_weights[king_feature(
                                               p, piece, square, out.kb[p], out.mir[p])]);
                }
            }
        }

    }  // namespace

    CHESS_SIMD_CLONES void refresh(const Network &network, const Position &pos, Accumulator &out) {
        out.count = __builtin_popcountll(pos.occupied());
        refresh_perspective(network, pos, 0, out);
        refresh_perspective(network, pos, 1, out);
    }

    CHESS_SIMD_CLONES void update(const Network &network, const Position &pos, Move move,
                                  const Accumulator &before, Accumulator &after) {
        after = before;

        const Square from = move.from();
        const Square to = move.to();
        const Piece moving = pos.piece_on(from);
        const Color us = piece_color(moving);

        remove_piece(network, after, moving, from);

        if (move.is_en_passant()) {
            const Square captured = static_cast<Square>(us == kWhite ? to - 8 : to + 8);
            remove_piece(network, after, make_piece(~us, kPawn), captured);
            --after.count;
        } else if (move.is_capture()) {
            remove_piece(network, after, pos.piece_on(to), to);
            --after.count;
        }

        if (move.is_castle()) {
            const bool kingside = move.flag() == kKingCastle;
            const Square rook_from = static_cast<Square>(kingside ? to + 1 : to - 2);
            const Square rook_to = static_cast<Square>(kingside ? to - 1 : to + 1);
            const Piece rook = make_piece(us, kRook);
            remove_piece(network, after, rook, rook_from);
            add_piece(network, after, rook, rook_to);
        }

        const Piece placed = move.is_promotion() ? make_piece(us, move.promotion_type()) : moving;
        add_piece(network, after, placed, to);

        // A king that changes bucket or mirror changes every input of its own point of view.
        if (piece_type(moving) == kKing) {
            const int p = us == kWhite ? 0 : 1;
            int kb = 0, mir = 0;
            king_context(p, to, kb, mir);

            if (kb != before.kb[p] || mir != before.mir[p]) {
                Position next = pos;
                next.make_move(move);
                refresh_perspective(network, next, p, after);
            }
        }
    }

    CHESS_SIMD_CLONES int evaluate(const Network &network, const Accumulator &acc,
                                   Color side_to_move) {
        const int16_t *us = acc.values[side_to_move];
        const int16_t *them = acc.values[~side_to_move];
        const int bucket = bucket_of(acc.count);
        int32_t sum = network.output_bias[bucket];

        for (int i = 0; i < kHidden; ++i) {
            const int32_t a = std::clamp<int32_t>(us[i], 0, kQuantInput);
            const int32_t b = std::clamp<int32_t>(them[i], 0, kQuantInput);
            sum +=
                a * network.output_weights[bucket][0][i] + b * network.output_weights[bucket][1][i];
        }

        // sum is the output in units of 1 / (kQuantInput * kQuantOutput).
        return static_cast<int>(static_cast<int64_t>(sum) * kOutputScale /
                                (kQuantInput * kQuantOutput));
    }

    bool save(const Network &network, const std::string &path) {
        std::ofstream out(path, std::ios::binary);

        if (!out) {
            return false;
        }

        const int32_t header[6] = {kInputs,      kHidden,      kQuantInput,
                                   kQuantOutput, kOutputScale, kBuckets};
        out.write(kMagic, sizeof(kMagic));
        out.write(reinterpret_cast<const char *>(header), sizeof(header));
        out.write(reinterpret_cast<const char *>(network.feature_weights),
                  sizeof(network.feature_weights));
        out.write(reinterpret_cast<const char *>(network.feature_bias),
                  sizeof(network.feature_bias));
        out.write(reinterpret_cast<const char *>(network.output_weights),
                  sizeof(network.output_weights));
        out.write(reinterpret_cast<const char *>(network.output_bias), sizeof(network.output_bias));
        return static_cast<bool>(out);
    }

    std::shared_ptr<const Network> load(const std::string &path) {
        std::ifstream in(path, std::ios::binary);

        if (!in) {
            return nullptr;
        }

        char magic[8];
        int32_t header[6] = {0, 0, 0, 0, 0, 0};
        in.read(magic, sizeof(magic));
        in.read(reinterpret_cast<char *>(header), sizeof(header));

        // The sizes and scales of the file have to be those of this build.
        const int32_t expected[6] = {kInputs,      kHidden,      kQuantInput,
                                     kQuantOutput, kOutputScale, kBuckets};

        if (!in || std::memcmp(magic, kMagic, sizeof(kMagic)) != 0 ||
            std::memcmp(header, expected, sizeof(header)) != 0) {
            return nullptr;
        }

        auto network = std::make_shared<Network>();
        in.read(reinterpret_cast<char *>(network->feature_weights),
                sizeof(network->feature_weights));
        in.read(reinterpret_cast<char *>(network->feature_bias), sizeof(network->feature_bias));
        in.read(reinterpret_cast<char *>(network->output_weights), sizeof(network->output_weights));
        in.read(reinterpret_cast<char *>(network->output_bias), sizeof(network->output_bias));

        if (!in) {
            return nullptr;
        }
        return network;
    }

}  // namespace chess::nnue
