#include "chess/see.hpp"

#include <algorithm>

#include "chess/attacks.hpp"
#include "chess/eval.hpp"

namespace chess {

    namespace {

        /**
         * Value of a piece in an exchange. The king is worth more than all other pieces together,
         * so that it is the last to capture.
         */
        constexpr int kExchangeValue[6] = {100, 320, 330, 500, 900, 20000};

        /**
         * All pieces of both sides that attack `square` when the occupied squares are `occupied`.
         */
        Bitboard attackers_to(const Position &pos, Square square, Bitboard occupied) noexcept {
            Bitboard attackers = 0;

            attackers |= pawn_attacks(kBlack, square) & pos.pieces(kWhite, kPawn);
            attackers |= pawn_attacks(kWhite, square) & pos.pieces(kBlack, kPawn);

            for (const Color color : {kWhite, kBlack}) {
                attackers |= knight_attacks(square) & pos.pieces(color, kKnight);
                attackers |= king_attacks(square) & pos.pieces(color, kKing);
                attackers |= bishop_attacks(square, occupied) &
                             (pos.pieces(color, kBishop) | pos.pieces(color, kQueen));
                attackers |= rook_attacks(square, occupied) &
                             (pos.pieces(color, kRook) | pos.pieces(color, kQueen));
            }

            return attackers & occupied;
        }

    }  // namespace

    int see(const Position &pos, Move move) noexcept {
        const Square to = move.to();
        const Square from = move.from();

        int gain[32];
        int depth = 0;

        Bitboard occupied = pos.occupied() ^ square_bb(from);
        PieceType on_target = piece_type(pos.piece_on(from));

        if (move.is_en_passant()) {
            gain[0] = kExchangeValue[kPawn];
            occupied ^= square_bb(make_square(file_of(to), rank_of(from)));
        } else if (pos.piece_on(to) != kNoPiece) {
            gain[0] = kExchangeValue[piece_type(pos.piece_on(to))];
        } else {
            return 0;
        }

        Bitboard attackers = attackers_to(pos, to, occupied);
        Color side = ~pos.side_to_move();

        while (true) {
            const Bitboard mine = attackers & pos.pieces(side);

            if (mine == 0) {
                break;
            }

            // The least valuable attacker of the side that is to capture.
            PieceType least = kPawn;
            Bitboard chosen = 0;

            for (int type = kPawn; type <= kKing; ++type) {
                chosen = mine & pos.pieces(side, static_cast<PieceType>(type));

                if (chosen != 0) {
                    least = static_cast<PieceType>(type);
                    break;
                }
            }

            ++depth;
            gain[depth] = kExchangeValue[on_target] - gain[depth - 1];

            // Neither side can improve its result by going on.
            if (std::max(-gain[depth - 1], gain[depth]) < 0) {
                break;
            }

            // A king may capture only when the other side has no attacker left.
            if (least == kKing && (attackers & pos.pieces(~side) & occupied) != 0) {
                --depth;
                break;
            }

            occupied ^= chosen & (~chosen + 1);
            on_target = least;

            // Removing a piece can uncover a slider behind it.
            if (least == kPawn || least == kBishop || least == kQueen) {
                attackers |= bishop_attacks(to, occupied) &
                             (pos.pieces(kWhite, kBishop) | pos.pieces(kWhite, kQueen) |
                              pos.pieces(kBlack, kBishop) | pos.pieces(kBlack, kQueen));
            }

            if (least == kRook || least == kQueen) {
                attackers |= rook_attacks(to, occupied) &
                             (pos.pieces(kWhite, kRook) | pos.pieces(kWhite, kQueen) |
                              pos.pieces(kBlack, kRook) | pos.pieces(kBlack, kQueen));
            }

            attackers &= occupied;
            side = ~side;
        }

        // `gain[d]` is the result of the capture at step d if it is made; each side declines it
        // when declining is better.
        for (; depth > 0; --depth) {
            gain[depth - 1] = -std::max(-gain[depth - 1], gain[depth]);
        }

        return gain[0];
    }

}  // namespace chess
