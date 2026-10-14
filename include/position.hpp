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
#include <optional>
#include <string>
#include <string_view>

#include "attacks.hpp"
#include "types.hpp"

namespace chess {

    /**
     * @brief A chess position: piece placement, side to move, castling rights,
     * en passant square and move counters, with an incrementally updated hash.
     *
     * A position is a small value type. Search plays a move by copying the
     * position and calling `make_move` on the copy, so there is nothing to undo.
     */
    class Position {
    public:
        /**
         * Creates the standard initial position.
         */
        Position();

        /**
         * @brief Parses a FEN string.
         *
         * The four piece placement, side, castling and en passant fields are
         * required in practice; missing counters default to 0 and 1. Castling
         * rights whose king or rook is not on its home square are dropped, since
         * databases of games often leave them in.
         *
         * @param fen Position in Forsyth-Edwards Notation.
         * @return The position, or an empty optional when `fen` is malformed.
         */
        static std::optional<Position> from_fen(std::string_view fen);

        /**
         * @brief Writes the position as a FEN string.
         *
         * @return Six space separated FEN fields.
         */
        std::string to_fen() const;

        /**
         * Side to move.
         */
        Color side_to_move() const noexcept {
            return side_;
        }

        /**
         * Castling rights as a mask of CastlingRight bits.
         */
        int castling() const noexcept {
            return castling_;
        }

        /**
         * @brief Square a pawn could capture onto en passant, or kNoSquare.
         *
         * Set after every double pawn push, whether or not a capture is possible.
         */
        Square en_passant() const noexcept {
            return en_passant_;
        }

        /**
         * Plies since the last capture or pawn move.
         */
        int halfmove_clock() const noexcept {
            return halfmove_;
        }

        /**
         * Hash of placement, side, castling rights and a capturable en passant square.
         */
        uint64_t key() const noexcept {
            return key_;
        }

        /**
         * Piece on a square, or kNoPiece.
         */
        Piece piece_on(Square square) const noexcept {
            return board_[square];
        }

        /**
         * Squares holding a piece of the given side and kind.
         */
        Bitboard pieces(Color color, PieceType type) const noexcept {
            return by_piece_[make_piece(color, type)];
        }

        /**
         * Squares holding any piece of `color`.
         */
        Bitboard pieces(Color color) const noexcept {
            return by_color_[color];
        }

        /**
         * Squares holding any piece.
         */
        Bitboard occupied() const noexcept {
            return by_color_[kWhite] | by_color_[kBlack];
        }

        /**
         * Square of the king of `color`.
         */
        Square king_square(Color color) const noexcept {
            return __builtin_ctzll(by_piece_[make_piece(color, kKing)]);
        }

        /**
         * @brief Whether any piece of `by` attacks `square`.
         *
         * @param square Square to test.
         * @param by Attacking side.
         * @return True when attacked.
         */
        bool is_attacked(Square square, Color by) const noexcept;

        /**
         * @brief Whether the side to move is in check.
         *
         * @return True when its king is attacked.
         */
        bool in_check() const noexcept {
            return is_attacked(king_square(side_), ~side_);
        }

        /**
         * @brief Plays a pseudo-legal move.
         *
         * @param move A move generated for this position.
         * @return False when the move leaves the mover's own king attacked, in
         *         which case the position should be discarded.
         */
        bool make_move(Move move) noexcept;

        /**
         * Passes the turn. Should not be called while in check.
         */
        void make_null_move() noexcept;

        /**
         * @brief Whether the side to move has a piece other than pawns and king.
         *
         * @return True when it has a knight, bishop, rook or queen.
         */
        bool has_non_pawn_material() const noexcept;

        /**
         * @brief Whether neither side can ever deliver checkmate.
         *
         * @return True for king against king, a lone minor piece, or bishops all
         *         on one color of square.
         */
        bool insufficient_material() const noexcept;

    private:
        explicit Position(std::nullptr_t) noexcept;
        void clear() noexcept;
        void put(Piece piece, Square square) noexcept;
        void remove(Piece piece, Square square) noexcept;
        uint64_t compute_key() const noexcept;
        uint64_t en_passant_key() const noexcept;

        Bitboard by_piece_[12];
        Bitboard by_color_[2];
        uint8_t board_[kSquares];
        Color side_;
        int castling_;
        Square en_passant_;
        int halfmove_;
        int fullmove_;
        uint64_t key_;
    };

}  // namespace chess
