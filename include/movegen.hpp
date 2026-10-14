#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "position.hpp"
#include "types.hpp"

namespace chess {

    /**
     * A fixed-capacity list of moves, filled by the generators below.
     */
    class MoveList {
    public:
        void add(Move move) noexcept {
            moves_[size_++] = move;
        }

        int size() const noexcept {
            return size_;
        }

        bool empty() const noexcept {
            return size_ == 0;
        }

        Move &operator[](int index) noexcept {
            return moves_[index];
        }

        const Move &operator[](int index) const noexcept {
            return moves_[index];
        }

        const Move *begin() const noexcept {
            return moves_;
        }

        const Move *end() const noexcept {
            return moves_ + size_;
        }

    private:
        Move moves_[kMaxMoves];
        int size_ = 0;
    };

    /**
     * @brief Generates every move that follows piece movement rules, including
     * ones that leave the mover's king attacked.
     *
     * @param pos Position to move in.
     * @param list Receives the moves, and should be empty on entry.
     */
    void generate_pseudo_legal(const Position &pos, MoveList &list);

    /**
     * @brief Generates captures, en passant captures and promotions that follow
     * piece movement rules. Used by quiescence search.
     *
     * @param pos Position to move in.
     * @param list Receives the moves.
     */
    void generate_captures(const Position &pos, MoveList &list);

    /**
     * @brief Generates the legal moves of the side to move.
     *
     * @param pos Position to move in.
     * @param list Receives the moves.
     */
    void generate_legal(const Position &pos, MoveList &list);

    /**
     * @brief Counts the leaf positions reachable in exactly `depth` plies.
     *
     * @param pos Root position.
     * @param depth Plies to play; zero counts the root itself.
     * @return Number of leaf positions.
     */
    uint64_t perft(const Position &pos, int depth);

    /**
     * @brief Writes a move in coordinate notation, such as "e2e4" or "e7e8q".
     *
     * @param move A move.
     * @return Origin and destination squares, plus the promotion piece letter.
     */
    std::string move_to_uci(Move move);

    /**
     * @brief Finds the legal move written in coordinate notation.
     *
     * @param pos Position the move is played in.
     * @param text A move such as "e2e4" or "e7e8q".
     * @return The matching legal move, or the null move when there is none.
     */
    Move move_from_uci(const Position &pos, std::string_view text);

    /**
     * @brief Writes a legal move in standard algebraic notation, such as "Nbd7+".
     *
     * @param pos Position the move is played in.
     * @param move A legal move.
     * @return The notation with disambiguation and check or mate marks.
     */
    std::string move_to_san(const Position &pos, Move move);

}  // namespace chess
