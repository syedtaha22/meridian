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

#include "timeman.hpp"

#include <algorithm>

namespace chess {

    TimeBudget allocate_time(const Clock &clock, int64_t move_overhead_ms) {
        TimeBudget budget;

        if (clock.move_time_ms > 0) {
            // A move time is the time to use. A move that ends a little early is fine.
            const int64_t hard = std::max<int64_t>(1, clock.move_time_ms - move_overhead_ms);
            budget.hard_ms = hard;
            budget.soft_ms = hard;
            return budget;
        }

        const int64_t available = std::max<int64_t>(1, clock.time_ms - move_overhead_ms);
        const int moves = clock.moves_to_go > 0 ? std::min(clock.moves_to_go, 50) : 30;
        const bool last_moves = clock.moves_to_go > 0 && clock.moves_to_go <= 2;

        // The share of the time left, and three quarters of the increment, which comes back
        // after the move.
        int64_t soft = available / moves + clock.increment_ms * 3 / 4;
        int64_t hard = std::min(soft * 4, available * (last_moves ? 95 : 80) / 100);
        soft = std::min(soft, available * (last_moves ? 90 : 50) / 100);

        hard = std::max<int64_t>(1, std::min(hard, available - 1));
        soft = std::max<int64_t>(1, std::min(soft, hard));

        budget.soft_ms = soft;
        budget.hard_ms = std::max(hard, soft);
        return budget;
    }

}  // namespace chess
