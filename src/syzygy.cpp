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

#include "syzygy.hpp"

#include <atomic>
#include <mutex>

#include "movegen.hpp"

extern "C" {
#include "tbprobe.h"
}

namespace chess::syzygy {

    namespace {

        std::atomic<int> g_largest{0};
        std::mutex g_root_lock;

        struct Inputs {
            uint64_t white;
            uint64_t black;
            uint64_t kings;
            uint64_t queens;
            uint64_t rooks;
            uint64_t bishops;
            uint64_t knights;
            uint64_t pawns;
            unsigned ep;
            bool white_to_move;
        };

        Inputs inputs_of(const Position &pos) {
            auto both = [&pos](PieceType type) {
                return pos.pieces(kWhite, type) | pos.pieces(kBlack, type);
            };

            Inputs in{};
            in.white = pos.pieces(kWhite);
            in.black = pos.pieces(kBlack);
            in.kings = both(kKing);
            in.queens = both(kQueen);
            in.rooks = both(kRook);
            in.bishops = both(kBishop);
            in.knights = both(kKnight);
            in.pawns = both(kPawn);
            in.ep = pos.en_passant() == kNoSquare ? 0u : static_cast<unsigned>(pos.en_passant());
            in.white_to_move = pos.side_to_move() == kWhite;
            return in;
        }

        bool probeable(const Position &pos) {
            const int limit = g_largest.load(std::memory_order_relaxed);
            return limit > 0 && pos.castling() == 0 &&
                   __builtin_popcountll(pos.occupied()) <= limit;
        }

        Wdl wdl_of(unsigned value) {
            switch (value) {
            case TB_LOSS:         return Wdl::kLoss;
            case TB_BLESSED_LOSS: return Wdl::kBlessedLoss;
            case TB_DRAW:         return Wdl::kDraw;
            case TB_CURSED_WIN:   return Wdl::kCursedWin;
            default:              return Wdl::kWin;
            }
        }

    }  // namespace

    bool init(const std::string &path) {
        std::lock_guard<std::mutex> guard(g_root_lock);
        tb_free();
        g_largest.store(0);

        if (path.empty() || !tb_init(path.c_str())) {
            return false;
        }

        g_largest.store(static_cast<int>(TB_LARGEST));
        return TB_LARGEST > 0;
    }

    int largest() {
        return g_largest.load(std::memory_order_relaxed);
    }

    std::optional<Wdl> probe_wdl(const Position &pos) {
        if (!probeable(pos) || pos.halfmove_clock() != 0) {
            return std::nullopt;
        }

        const Inputs in = inputs_of(pos);
        const unsigned result =
            tb_probe_wdl(in.white, in.black, in.kings, in.queens, in.rooks, in.bishops, in.knights,
                         in.pawns, 0, 0, in.ep, in.white_to_move);

        if (result == TB_RESULT_FAILED) {
            return std::nullopt;
        }
        return wdl_of(result);
    }

    std::optional<RootResult> probe_root(const Position &pos) {
        if (!probeable(pos)) {
            return std::nullopt;
        }

        const Inputs in = inputs_of(pos);
        unsigned result;
        {
            std::lock_guard<std::mutex> guard(g_root_lock);
            result = tb_probe_root(
                in.white, in.black, in.kings, in.queens, in.rooks, in.bishops, in.knights, in.pawns,
                static_cast<unsigned>(pos.halfmove_clock()), 0, in.ep, in.white_to_move, nullptr);
        }

        if (result == TB_RESULT_FAILED || result == TB_RESULT_CHECKMATE ||
            result == TB_RESULT_STALEMATE) {
            return std::nullopt;
        }

        const unsigned from = TB_GET_FROM(result);
        const unsigned to = TB_GET_TO(result);
        const unsigned promotes = TB_GET_PROMOTES(result);

        MoveList legal;
        generate_legal(pos, legal);

        for (const Move move : legal) {
            if (move.from() != static_cast<Square>(from) || move.to() != static_cast<Square>(to)) {
                continue;
            }

            if (move.is_promotion()) {
                const PieceType wanted = promotes == TB_PROMOTES_QUEEN    ? kQueen
                                         : promotes == TB_PROMOTES_ROOK   ? kRook
                                         : promotes == TB_PROMOTES_BISHOP ? kBishop
                                                                          : kKnight;

                if (move.promotion_type() != wanted) {
                    continue;
                }
            }

            RootResult root;
            root.best = move;
            root.wdl = wdl_of(TB_GET_WDL(result));
            root.dtz = static_cast<int>(TB_GET_DTZ(result));
            return root;
        }
        return std::nullopt;
    }

}  // namespace chess::syzygy
