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

#include "position.hpp"
#include "types.hpp"

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
