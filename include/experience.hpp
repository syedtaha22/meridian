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

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace chess {

    /**
     * What an earlier, deeper analysis found for one position.
     */
    struct ExperienceEntry {
        uint64_t key;    ///< Hash of the position, as `Position::key` gives it.
        uint16_t move;   ///< The best move, as `Move::data` holds it.
        int16_t score;   ///< Score for the side to move, with mates near `kMateScore`.
        uint16_t depth;  ///< Depth of the analysis.
    };

    /**
     * @brief Results of earlier analyses, looked up by position.
     *
     * The file of the option `ExperienceFile` is a header of 8 bytes with the text `MRDEXP01`
     * and 8 bytes with the number of entries, then the entries in order of their keys, each one
     * 14 bytes: the key (8 bytes), the move (2), the score (2) and the depth (2). Numbers are
     * little-endian. The engine reads the file and never writes it.
     */
    class Experience {
    public:
        /**
         * @brief Reads a file.
         *
         * @param path The experience file.
         * @return The entries, or null when the file is missing, is not of this format, has a
         *         wrong size, or has keys that are not in strictly increasing order.
         */
        static std::shared_ptr<const Experience> load(const std::string &path);

        /**
         * Number of entries.
         */
        size_t size() const noexcept {
            return entries_.size();
        }

        /**
         * The entry of a position, or null when the position is not in the file.
         */
        const ExperienceEntry *find(uint64_t key) const noexcept;

    private:
        std::vector<ExperienceEntry> entries_;
    };

}  // namespace chess
