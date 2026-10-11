#include "chess/eval.hpp"

namespace chess {

    namespace {

        /**
         * Tables are written from White's point of view with rank 8 first, so a
         * white piece on square `s` reads entry `s ^ 56` and a black piece reads `s`.
         */
        constexpr int kPawnTable[64] = {
            0,  0,  0,   0,   0,   0,   0,  0,   //
            50, 50, 50,  50,  50,  50,  50, 50,  //
            10, 10, 20,  30,  30,  20,  10, 10,  //
            5,  5,  10,  25,  25,  10,  5,  5,   //
            0,  0,  0,   20,  20,  0,   0,  0,   //
            5,  -5, -10, 0,   0,   -10, -5, 5,   //
            5,  10, 10,  -20, -20, 10,  10, 5,   //
            0,  0,  0,   0,   0,   0,   0,  0,
        };

        constexpr int kKnightTable[64] = {
            -50, -40, -30, -30, -30, -30, -40, -50,  //
            -40, -20, 0,   0,   0,   0,   -20, -40,  //
            -30, 0,   10,  15,  15,  10,  0,   -30,  //
            -30, 5,   15,  20,  20,  15,  5,   -30,  //
            -30, 0,   15,  20,  20,  15,  0,   -30,  //
            -30, 5,   10,  15,  15,  10,  5,   -30,  //
            -40, -20, 0,   5,   5,   0,   -20, -40,  //
            -50, -40, -30, -30, -30, -30, -40, -50,
        };

        constexpr int kBishopTable[64] = {
            -20, -10, -10, -10, -10, -10, -10, -20,  //
            -10, 0,   0,   0,   0,   0,   0,   -10,  //
            -10, 0,   5,   10,  10,  5,   0,   -10,  //
            -10, 5,   5,   10,  10,  5,   5,   -10,  //
            -10, 0,   10,  10,  10,  10,  0,   -10,  //
            -10, 10,  10,  10,  10,  10,  10,  -10,  //
            -10, 5,   0,   0,   0,   0,   5,   -10,  //
            -20, -10, -10, -10, -10, -10, -10, -20,
        };

        constexpr int kRookTable[64] = {
            0,  0,  0,  0,  0,  0,  0,  0,   //
            5,  10, 10, 10, 10, 10, 10, 5,   //
            -5, 0,  0,  0,  0,  0,  0,  -5,  //
            -5, 0,  0,  0,  0,  0,  0,  -5,  //
            -5, 0,  0,  0,  0,  0,  0,  -5,  //
            -5, 0,  0,  0,  0,  0,  0,  -5,  //
            -5, 0,  0,  0,  0,  0,  0,  -5,  //
            0,  0,  0,  5,  5,  0,  0,  0,
        };

        constexpr int kQueenTable[64] = {
            -20, -10, -10, -5, -5, -10, -10, -20,  //
            -10, 0,   0,   0,  0,  0,   0,   -10,  //
            -10, 0,   5,   5,  5,  5,   0,   -10,  //
            -5,  0,   5,   5,  5,  5,   0,   -5,   //
            0,   0,   5,   5,  5,  5,   0,   -5,   //
            -10, 5,   5,   5,  5,  5,   0,   -10,  //
            -10, 0,   5,   0,  0,  0,   0,   -10,  //
            -20, -10, -10, -5, -5, -10, -10, -20,
        };

        constexpr int kKingMiddlegame[64] = {
            -30, -40, -40, -50, -50, -40, -40, -30,  //
            -30, -40, -40, -50, -50, -40, -40, -30,  //
            -30, -40, -40, -50, -50, -40, -40, -30,  //
            -30, -40, -40, -50, -50, -40, -40, -30,  //
            -20, -30, -30, -40, -40, -30, -30, -20,  //
            -10, -20, -20, -20, -20, -20, -20, -10,  //
            20,  20,  0,   0,   0,   0,   20,  20,   //
            20,  30,  10,  0,   0,   10,  30,  20,
        };

        constexpr int kKingEndgame[64] = {
            -50, -40, -30, -20, -20, -30, -40, -50,  //
            -30, -20, -10, 0,   0,   -10, -20, -30,  //
            -30, -10, 20,  30,  30,  20,  -10, -30,  //
            -30, -10, 30,  40,  40,  30,  -10, -30,  //
            -30, -10, 30,  40,  40,  30,  -10, -30,  //
            -30, -10, 20,  30,  30,  20,  -10, -30,  //
            -30, -30, 0,   0,   0,   0,   -30, -30,  //
            -50, -30, -30, -30, -30, -30, -30, -50,
        };

        constexpr const int *kTables[5] = {kPawnTable, kKnightTable, kBishopTable, kRookTable,
                                           kQueenTable};

        constexpr int kPhaseWeight[6] = {0, 1, 1, 2, 4, 0};
        constexpr int kMaxPhase = 24;

    }  // namespace

    int evaluate(const Position &pos) noexcept {
        int score[2] = {0, 0};
        int king_mg[2] = {0, 0};
        int king_eg[2] = {0, 0};
        int phase = 0;

        for (Color color : {kWhite, kBlack}) {
            for (int type = kPawn; type <= kQueen; ++type) {
                Bitboard bb = pos.pieces(color, static_cast<PieceType>(type));
                phase += kPhaseWeight[type] * __builtin_popcountll(bb);

                while (bb) {
                    const Square s = __builtin_ctzll(bb);
                    bb &= bb - 1;
                    const int index = color == kWhite ? (s ^ 56) : s;
                    score[color] += kPieceValues[type] + kTables[type][index];
                }
            }

            const Square king = pos.king_square(color);
            const int index = color == kWhite ? (king ^ 56) : king;
            king_mg[color] = kKingMiddlegame[index];
            king_eg[color] = kKingEndgame[index];
        }

        if (phase > kMaxPhase) {
            phase = kMaxPhase;
        }

        for (Color color : {kWhite, kBlack}) {
            score[color] +=
                (king_mg[color] * phase + king_eg[color] * (kMaxPhase - phase)) / kMaxPhase;
        }

        const int white_view = score[kWhite] - score[kBlack];
        return pos.side_to_move() == kWhite ? white_view : -white_view;
    }

}  // namespace chess
