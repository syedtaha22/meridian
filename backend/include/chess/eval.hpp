#pragma once

#include "chess/position.hpp"

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

}  // namespace chess
