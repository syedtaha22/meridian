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

#include <cstdint>
#include <memory>
#include <string>

#include "position.hpp"
#include "types.hpp"

namespace chess::nnue {

    /**
     * Inputs: one for each piece (12 kinds, own and other side) on each square, for each king
     * bucket. The king bucket of a point of view is the half of the board, by rank, that holds its
     * own king, after the board is mirrored so that the king stands on the files a to d.
     */
    constexpr int kBaseInputs = 768;  ///< The pieces and squares of one king bucket.
    constexpr int kKingBuckets = 2;
    constexpr int kInputs = kBaseInputs * kKingBuckets;

    /**
     * Width of the accumulator of each side. The network holds `2 * kHidden` values after the
     * accumulators of both sides, for each output layer. A build can change it with the macro
     * `CHESS_NNUE_HIDDEN`, for networks of another size; a network file holds its size.
     */
#ifndef CHESS_NNUE_HIDDEN
#define CHESS_NNUE_HIDDEN 128
#endif
    constexpr int kHidden = CHESS_NNUE_HIDDEN;

    constexpr int kBuckets = 8;        ///< Output layers, chosen by the number of pieces.
    constexpr int kQuantInput = 255;   ///< Scale of the accumulator, which is clipped to [0, 255].
    constexpr int kQuantOutput = 128;  ///< Scale of the weights of the output layer.
    constexpr int kOutputScale = 400;  ///< Centipawns of an output of 1.

    /**
     * @brief The weights of a network, in the integers that the evaluation uses.
     *
     * One layer maps the pieces of the board to `kHidden` values for each side, which are clipped
     * to [0, kQuantInput] and combined by an output layer into one score. The side to move comes
     * first, and the number of pieces on the board chooses the output layer.
     */
    struct Network {
        alignas(32) int16_t feature_weights[kInputs][kHidden];
        alignas(32) int16_t feature_bias[kHidden];
        alignas(32) int16_t output_weights[kBuckets][2][kHidden];
        int32_t output_bias[kBuckets] = {};
    };

    /**
     * @brief The first layer for a position, for the white and for the black point of view.
     *
     * An accumulator follows the position along a line of moves: `update` turns the accumulator
     * of a position into that of the position after a move, which costs a few rows of weights.
     * A king that moves to another bucket, or across the mirror, changes every input of its own
     * point of view, and `update` computes that point of view again.
     */
    struct Accumulator {
        alignas(32) int16_t values[2][kHidden];
        int count = 0;            ///< Pieces on the board, which choose the output layer.
        uint8_t kb[2] = {0, 0};   ///< King bucket of each point of view.
        uint8_t mir[2] = {0, 0};  ///< Whether the board is mirrored for that point of view.
    };

    /**
     * @brief The king bucket and the mirror of one point of view.
     *
     * @param perspective 0 for White, 1 for Black. Black sees the board flipped.
     * @param king_square The square of the own king.
     * @param kb Receives the king bucket.
     * @param mir Receives 1 when the board is mirrored, which is when the king stands on the files
     *        e to h.
     */
    inline void king_context(int perspective, int king_square, int &kb, int &mir) noexcept {
        const int f = perspective == 0 ? king_square : (king_square ^ 56);
        mir = (f & 7) >= 4 ? 1 : 0;
        const int m = mir ? (f ^ 7) : f;
        kb = (m >> 3) >= 4 ? 1 : 0;
    }

    /**
     * @brief The input number of a piece on a square, for one point of view.
     *
     * @param perspective 0 for White, 1 for Black. Black sees the board flipped and the sides
     *        exchanged.
     * @param piece The piece, as `color * 6 + type`.
     * @param square The square of the piece.
     * @param kb The king bucket of the point of view.
     * @param mir Whether the board is mirrored for the point of view.
     * @return A number below `kInputs`.
     */
    inline int king_feature(int perspective, int piece, int square, int kb, int mir) noexcept {
        const int slot = perspective == 0 ? piece : (piece + 6) % 12;
        const int sq = (perspective == 0 ? square : (square ^ 56)) ^ (mir ? 7 : 0);
        return kb * kBaseInputs + slot * 64 + sq;
    }

    /**
     * The output layer for a number of pieces: 2 to 5 pieces the first, 6 to 9 the second, and so
     * on.
     */
    constexpr int bucket_of(int pieces) noexcept {
        const int b = (pieces - 2) / (32 / kBuckets);
        return b < 0 ? 0 : (b >= kBuckets ? kBuckets - 1 : b);
    }

    /**
     * @brief Computes the accumulator of a position from the pieces on the board.
     *
     * @param network Weights.
     * @param pos Position.
     * @param out Receives the accumulator.
     */
    void refresh(const Network &network, const Position &pos, Accumulator &out);

    /**
     * @brief Computes the accumulator after a move from the one before it.
     *
     * @param network Weights.
     * @param pos The position before `move`.
     * @param move A legal move of `pos`.
     * @param before The accumulator of `pos`.
     * @param after Receives the accumulator of the position after `move`. It may not be
     *        `before`.
     */
    void update(const Network &network, const Position &pos, Move move, const Accumulator &before,
                Accumulator &after);

    /**
     * @brief The score of a position from its accumulator.
     *
     * @param network Weights.
     * @param acc Accumulator of the position.
     * @param side_to_move Side to move in the position.
     * @return Centipawns for the side to move.
     */
    int evaluate(const Network &network, const Accumulator &acc, Color side_to_move);

    /**
     * @brief The network that is compiled into the library.
     *
     * @return The network, or null when the build has none. It is the one of
     *         `src/nnue_net.inc`, which `tools/train_nnue --inc` writes.
     */
    std::shared_ptr<const Network> builtin();

    /**
     * @brief Reads a network from a file written by `save`.
     *
     * @param path File.
     * @return The network, or null when the file is missing or does not match this build.
     */
    std::shared_ptr<const Network> load(const std::string &path);

    /**
     * @brief Writes a network to a file.
     *
     * @param network Weights.
     * @param path File.
     * @return True when the file was written.
     */
    bool save(const Network &network, const std::string &path);

}  // namespace chess::nnue
