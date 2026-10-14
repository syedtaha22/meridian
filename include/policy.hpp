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

#include "attacks.hpp"
#include "position.hpp"
#include "see.hpp"
#include "types.hpp"

namespace chess::policy {

    /**
     * Number of binary features of a move.
     */
    constexpr int kFeatures = 196;

    /**
     * Most features a move has at once.
     */
    constexpr int kMaxActive = 8;

    /**
     * @brief The features that a move has in a position.
     *
     * The features are the type of the moving piece with its origin and destination (from the
     * mover's side of the board), the victim and the attacker of a capture, the kind of the
     * move, whether a quiet move goes to a square that an enemy pawn attacks, and the sign of
     * the exchange on the destination square.
     *
     * @param pos Position before the move.
     * @param move The move.
     * @param out Receives the indices of the active features.
     * @return The number of active features.
     */
    inline int features(const Position &pos, Move move, int *out) noexcept {
        const Color us = pos.side_to_move();
        const int type = piece_type(pos.piece_on(move.from()));
        const int to_rank = us == kWhite ? rank_of(move.to()) : 7 - rank_of(move.to());
        const int from_rank = us == kWhite ? rank_of(move.from()) : 7 - rank_of(move.from());
        int count = 0;

        out[count++] = type * 8 + to_rank;                  // 0..47
        out[count++] = 48 + type * 8 + file_of(move.to());  // 48..95
        out[count++] = 96 + type * 8 + from_rank;           // 96..143

        if (move.is_capture()) {
            const int victim = move.is_en_passant() ? kPawn : piece_type(pos.piece_on(move.to()));
            out[count++] = 144 + victim * 6 + type;  // 144..179

            if (!move.is_promotion()) {
                const int exchange = see(pos, move);
                out[count++] = 192 + (exchange < 0 ? 0 : (exchange == 0 ? 1 : 2));  // 192..194
            }
        } else {
            if (pos.piece_on(move.to()) == kNoPiece &&
                (pawn_attacks(us, move.to()) & pos.pieces(~us, kPawn)) != 0) {
                out[count++] = 180 + type;  // 180..185
            }
        }

        if (move.is_promotion()) {
            out[count++] = 186 + (move.promotion_type() - kKnight);  // 186..189
        } else if (move.is_castle()) {
            out[count++] = 190;
        } else if (move.flag() == kDoublePush) {
            out[count++] = 191;
        }

        return count;
    }

    /**
     * Learned weights of the features, in 1/64 of a logit. Written by `train_policy`.
     */
    extern const int kWeights[kFeatures];

    /**
     * @brief The learned score of a move, in 1/64 of a logit.
     */
    inline int score(const Position &pos, Move move) noexcept {
        int active[kMaxActive];
        const int count = features(pos, move, active);
        int total = 0;

        for (int i = 0; i < count; ++i) {
            total += kWeights[active[i]];
        }

        return total;
    }

}  // namespace chess::policy
