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

#include <optional>
#include <string>

#include "position.hpp"
#include "types.hpp"

namespace chess::syzygy {

    /**
     * Score of a position that a tablebase wins, before the distance to the root is taken off. It
     * is below the scores of forced mates.
     */
    constexpr int kTbWinScore = 28000;

    /**
     * The result of a position for the side to move, as the tablebases give it. A cursed win and
     * a blessed loss are results that the 50-move rule turns into draws.
     */
    enum class Wdl { kLoss, kBlessedLoss, kDraw, kCursedWin, kWin };

    /**
     * @brief Loads the tables that are in a directory.
     *
     * Several directories can be given, separated by `:`. Loading a second time replaces the
     * tables. Probing is possible only for positions with no more pieces than `largest`.
     *
     * @param path Directory of Syzygy files (`.rtbw` and `.rtbz`).
     * @return True when at least one table was found.
     */
    bool init(const std::string &path);

    /**
     * The largest number of pieces (kings included) of a position that can be probed, or 0 when
     * no table is loaded.
     */
    int largest();

    /**
     * @brief The win, draw or loss of a position.
     *
     * The position needs no more pieces than `largest()`, no castling rights, and a half move
     * clock of 0. Safe from several threads.
     *
     * @param pos Position to probe.
     * @return The result, or nothing when the position cannot be probed or a table is missing.
     */
    std::optional<Wdl> probe_wdl(const Position &pos);

    /**
     * The move that a distance-to-zero table suggests at the root.
     */
    struct RootResult {
        Move best;  ///< A legal move that keeps the result, nearest to the win or the longest loss.
        Wdl wdl = Wdl::kDraw;
        int dtz = 0;  ///< Plies to the next capture or pawn move after the move, as the table says.
    };

    /**
     * @brief The best move of a position by the distance-to-zero tables.
     *
     * Not safe from several threads at once. The half move clock may have any value.
     *
     * @param pos Position to probe, with a legal move.
     * @return The move and result, or nothing when the position cannot be probed.
     */
    std::optional<RootResult> probe_root(const Position &pos);

}  // namespace chess::syzygy
