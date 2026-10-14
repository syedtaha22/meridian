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

namespace chess {

    /**
     * The clock of a game, as a `go` command of the UCI protocol gives it. A value of 0 means
     * that the field was not given.
     */
    struct Clock {
        int64_t time_ms = 0;       ///< Time left for the side to move.
        int64_t increment_ms = 0;  ///< Time added after each move of the side to move.
        int moves_to_go = 0;       ///< Moves until the next time control.
        int64_t move_time_ms = 0;  ///< Exact time for this move.
    };

    /**
     * How long one search may run.
     */
    struct TimeBudget {
        int64_t soft_ms = 0;  ///< After this (scaled) no new iteration should start.
        int64_t hard_ms = 0;  ///< The search has to stop at this time.
    };

    /**
     * @brief Divides the clock into a budget for one move.
     *
     * The hard limit never reaches the time left: it stays below it by at least the move
     * overhead, which covers the time a message takes to reach the engine and return. With the
     * moves to the next time control unknown, the budget assumes that 30 more moves are needed.
     *
     * @param clock The clock of the side to move.
     * @param move_overhead_ms Time that a move takes outside the search.
     * @return The soft and hard limits, each at least 1.
     */
    TimeBudget allocate_time(const Clock &clock, int64_t move_overhead_ms);

}  // namespace chess
