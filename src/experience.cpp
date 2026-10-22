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

#include "experience.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>

namespace chess {

    namespace {

        constexpr char kMagic[8] = {'M', 'R', 'D', 'E', 'X', 'P', '0', '1'};
        constexpr size_t kHeaderBytes = 16;
        constexpr size_t kEntryBytes = 14;

    }  // namespace

    std::shared_ptr<const Experience> Experience::load(const std::string &path) {
        std::ifstream in(path, std::ios::binary);

        if (!in) {
            return nullptr;
        }

        const std::vector<char> data((std::istreambuf_iterator<char>(in)),
                                     std::istreambuf_iterator<char>());

        if (data.size() < kHeaderBytes || std::memcmp(data.data(), kMagic, sizeof(kMagic)) != 0) {
            return nullptr;
        }

        uint64_t count = 0;
        std::memcpy(&count, data.data() + 8, sizeof(count));

        if (count > (data.size() - kHeaderBytes) / kEntryBytes ||
            data.size() != kHeaderBytes + count * kEntryBytes) {
            return nullptr;
        }

        auto experience = std::make_shared<Experience>();
        experience->entries_.resize(static_cast<size_t>(count));
        const char *at = data.data() + kHeaderBytes;

        for (ExperienceEntry &entry : experience->entries_) {
            std::memcpy(&entry.key, at, 8);
            std::memcpy(&entry.move, at + 8, 2);
            std::memcpy(&entry.score, at + 10, 2);
            std::memcpy(&entry.depth, at + 12, 2);
            at += kEntryBytes;
        }

        const bool ordered =
            std::adjacent_find(experience->entries_.begin(), experience->entries_.end(),
                               [](const ExperienceEntry &a, const ExperienceEntry &b) {
                                   return a.key >= b.key;
                               }) == experience->entries_.end();

        return ordered ? experience : nullptr;
    }

    const ExperienceEntry *Experience::find(uint64_t key) const noexcept {
        const auto at = std::lower_bound(
            entries_.begin(), entries_.end(), key,
            [](const ExperienceEntry &entry, uint64_t value) { return entry.key < value; });

        return at != entries_.end() && at->key == key ? &*at : nullptr;
    }

}  // namespace chess
