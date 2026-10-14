#pragma once

#include "chess/position.hpp"
#include "chess/types.hpp"

namespace chess {

    /**
     * @brief Static exchange evaluation of a capture.
     *
     * Both sides take turns capturing on the destination square with their least valuable
     * piece, and either side may stop when continuing would lose material. Pins and the
     * legality of the king captures are not considered.
     *
     * @param pos Position before the move.
     * @param move A capture, or any move to an empty square (which scores 0).
     * @return Material gained by the side to move, in centipawns. A negative value means that
     *         the capture loses material when the opponent recaptures well.
     */
    int see(const Position &pos, Move move) noexcept;

}  // namespace chess
