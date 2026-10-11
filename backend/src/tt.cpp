#include "chess/tt.hpp"

namespace chess {

    TranspositionTable::TranspositionTable(size_t megabytes) {
        resize(megabytes);
    }

    void TranspositionTable::resize(size_t megabytes) {
        size_t slots = 1;
        const size_t wanted = megabytes * 1024 * 1024 / sizeof(TTEntry);

        while (slots * 2 <= wanted) {
            slots *= 2;
        }

        entries_.assign(slots, TTEntry{});
        mask_ = slots - 1;
        generation_ = 0;
    }

    void TranspositionTable::clear() {
        entries_.assign(entries_.size(), TTEntry{});
        generation_ = 0;
    }

}  // namespace chess
