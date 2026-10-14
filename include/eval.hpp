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

#include <string>

#include "position.hpp"

namespace chess {

    /**
     * Material value of each piece type in centipawns, indexed by PieceType.
     */
    constexpr int kPieceValues[6] = {100, 320, 330, 500, 900, 0};

    /**
     * @brief Static evaluation of a position.
     *
     * Material plus piece-square tables, with the king table blended between a
     * middlegame and an endgame version by the material left on the board.
     *
     * @param pos Position to evaluate.
     * @return Centipawns from the point of view of the side to move: positive
     *         when the side to move is better.
     */
    int evaluate(const Position &pos) noexcept;

    /**
     * @brief The evaluation of the engine before the numbers were tuned.
     *
     * Material and piece-square tables with the king blended by the phase, and no other term.
     * The presets of the steps before the tuned one use it.
     *
     * @param pos Position to evaluate.
     * @return Centipawns from the point of view of the side to move.
     */
    int evaluate_legacy(const Position &pos) noexcept;

    /**
     * @brief The numbers of the tunable evaluation.
     *
     * Every number has a middlegame and an endgame value, and the result is blended by the
     * material left on the board. `defaults` holds the numbers that `evaluate` uses, which were
     * tuned on self-play games by `tools/texel_tune`. A term whose numbers are all zero is not
     * computed.
     */
    struct EvalParams {
        int material[2][5];   ///< Pawn, knight, bishop, rook, queen, by phase.
        int pst[2][5][64];    ///< Piece-square tables from White's side, rank 8 first.
        int king[2][64];      ///< King table by phase.
        int phase_weight[5];  ///< Weight of a piece in the phase count (the king is 0).
        int bishop_pair[2];   ///< Bonus for two bishops.
        int passed[2][8];     ///< Passed pawn bonus by the rank of the pawn, from its side.
        int doubled[2];       ///< Penalty for each pawn behind another on its file.
        int isolated[2];      ///< Penalty for a pawn with no friendly pawn on a nearby file.
        int rook_open[2];     ///< Rook on a file with no pawns.
        int rook_half[2];     ///< Rook on a file with only enemy pawns.
        int mobility[2][4];   ///< Per reachable square: knight, bishop, rook, queen.
        int tempo[2];         ///< Bonus for the side to move.

        /**
         * The numbers of `evaluate`.
         */
        static EvalParams defaults();

        /**
         * The tables of `evaluate_legacy`, with every other term zero.
         */
        static EvalParams legacy();

        /**
         * @brief Calls `visit(name, values, count)` for each group of numbers.
         */
        template <class Visitor> void visit(Visitor &&visit) {
            visit("material", &material[0][0], 10);
            visit("pst", &pst[0][0][0], 2 * 5 * 64);
            visit("king", &king[0][0], 2 * 64);
            visit("phase_weight", &phase_weight[0], 5);
            visit("bishop_pair", &bishop_pair[0], 2);
            visit("passed", &passed[0][0], 16);
            visit("doubled", &doubled[0], 2);
            visit("isolated", &isolated[0], 2);
            visit("rook_open", &rook_open[0], 2);
            visit("rook_half", &rook_half[0], 2);
            visit("mobility", &mobility[0][0], 8);
            visit("tempo", &tempo[0], 2);
        }

        /**
         * Writes the numbers as text, one group to a line.
         */
        bool save(const std::string &path) const;

        /**
         * Reads a file written by `save`; groups that the file lacks keep their values.
         */
        bool load(const std::string &path);
    };

    /**
     * @brief Evaluates with tunable numbers.
     *
     * @param pos Position to evaluate.
     * @param params Numbers to use.
     * @return Centipawns from the point of view of the side to move.
     */
    int evaluate(const Position &pos, const EvalParams &params) noexcept;

}  // namespace chess
