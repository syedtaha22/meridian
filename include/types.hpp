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
#include <string>

namespace chess {

    /**
     * One bit per square; bit `s` is square `s`.
     */
    using Bitboard = uint64_t;

    /**
     * Side of a piece or of the player to move.
     */
    enum Color : int { kWhite = 0, kBlack = 1 };

    /**
     * @brief The other side.
     *
     * @param color A side.
     * @return The opponent of `color`.
     */
    constexpr Color operator~(Color color) noexcept {
        return static_cast<Color>(color ^ 1);
    }

    /**
     * Kind of a piece, ignoring its side.
     */
    enum PieceType : int { kPawn, kKnight, kBishop, kRook, kQueen, kKing, kNoPieceType };

    /**
     * A piece including its side, as `color * 6 + type`, or kNoPiece.
     */
    using Piece = int;

    constexpr Piece kNoPiece = 12;  ///< Value of an empty square in a piece array.

    /**
     * @brief Builds a piece from its side and kind.
     *
     * @param color Side of the piece.
     * @param type Kind of the piece.
     * @return The combined piece value.
     */
    constexpr Piece make_piece(Color color, PieceType type) noexcept {
        return color * 6 + type;
    }

    /**
     * @brief Side of a piece. The piece should not be kNoPiece.
     *
     * @param piece A piece.
     * @return The side that owns it.
     */
    constexpr Color piece_color(Piece piece) noexcept {
        return static_cast<Color>(piece / 6);
    }

    /**
     * @brief Kind of a piece. The piece should not be kNoPiece.
     *
     * @param piece A piece.
     * @return Its kind.
     */
    constexpr PieceType piece_type(Piece piece) noexcept {
        return static_cast<PieceType>(piece % 6);
    }

    /**
     * @brief Square numbering: a1 is 0, b1 is 1, h1 is 7, a2 is 8 and h8 is 63.
     *
     * Squares are plain ints. Files and ranks run 0 to 7.
     */
    using Square = int;

    constexpr int kSquares = 64;
    constexpr Square kNoSquare = -1;

    constexpr int file_of(Square square) noexcept {
        return square & 7;
    }

    constexpr int rank_of(Square square) noexcept {
        return square >> 3;
    }

    constexpr Square make_square(int file, int rank) noexcept {
        return rank * 8 + file;
    }

    constexpr Bitboard square_bb(Square square) noexcept {
        return Bitboard{1} << square;
    }

    /**
     * Castling right bits in `Position::castling`.
     */
    enum CastlingRight : int {
        kWhiteKingSide = 1,
        kWhiteQueenSide = 2,
        kBlackKingSide = 4,
        kBlackQueenSide = 8,
    };

    /**
     * Kind of a move, stored in the four high bits of `Move`.
     */
    enum MoveFlag : int {
        kQuiet = 0,
        kDoublePush = 1,
        kKingCastle = 2,
        kQueenCastle = 3,
        kCapture = 4,
        kEnPassant = 5,
        kPromoKnight = 8,
        kPromoBishop = 9,
        kPromoRook = 10,
        kPromoQueen = 11,
        kPromoCaptureKnight = 12,
        kPromoCaptureBishop = 13,
        kPromoCaptureRook = 14,
        kPromoCaptureQueen = 15,
    };

    /**
     * A move packed into 16 bits: origin, destination and a MoveFlag.
     */
    struct Move {
        uint16_t data = 0;

        constexpr Move() noexcept = default;

        constexpr Move(Square from, Square to, MoveFlag flag = kQuiet) noexcept
            : data(static_cast<uint16_t>(from | (to << 6) | (flag << 12))) {
        }

        constexpr Square from() const noexcept {
            return data & 63;
        }

        constexpr Square to() const noexcept {
            return (data >> 6) & 63;
        }

        constexpr MoveFlag flag() const noexcept {
            return static_cast<MoveFlag>(data >> 12);
        }

        constexpr bool is_capture() const noexcept {
            return (data >> 12) & 4;
        }

        constexpr bool is_promotion() const noexcept {
            return (data >> 12) & 8;
        }

        constexpr bool is_en_passant() const noexcept {
            return flag() == kEnPassant;
        }

        constexpr bool is_castle() const noexcept {
            return flag() == kKingCastle || flag() == kQueenCastle;
        }

        /**
         * Piece a promotion turns the pawn into; only for promotions.
         */
        constexpr PieceType promotion_type() const noexcept {
            return static_cast<PieceType>(kKnight + (flag() & 3));
        }

        /**
         * True for the null value, which is not a legal move.
         */
        constexpr bool is_null() const noexcept {
            return data == 0;
        }

        constexpr bool operator==(const Move &other) const noexcept = default;
    };

    /**
     * Largest number of legal moves any position is known to have, rounded up.
     */
    constexpr int kMaxMoves = 256;

    /**
     * Score of a position in which the side to move is checkmated.
     */
    constexpr int kMateScore = 30000;

    /**
     * Scores at or beyond this magnitude are forced mates.
     */
    constexpr int kMateBound = kMateScore - 1000;

    constexpr int kInfinity = 32000;  ///< Larger than any score.

}  // namespace chess
