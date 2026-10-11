#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "chess/position.hpp"
#include "chess/tt.hpp"
#include "chess/types.hpp"

namespace chess {

    /**
     * Deepest ply the search reaches, including extensions and quiescence.
     */
    constexpr int kMaxPly = 128;

    /**
     * @brief Which techniques the search uses and how long it may run.
     *
     * Every technique can be switched off on its own. `preset` builds named
     * configurations from the plain search to the full one.
     */
    struct SearchOptions {
        int max_depth = 6;          ///< Nominal depth in plies.
        int64_t time_limit_ms = 0;  ///< Wall clock budget; 0 means no limit.
        uint64_t node_limit = 0;    ///< Node budget; 0 means no limit.
        size_t hash_mb = 16;        ///< Transposition table size.

        bool move_ordering = true;        ///< Try captures by MVV-LVA before quiet moves.
        bool killers_history = true;      ///< Order quiet moves by killer and history scores.
        bool transposition_table = true;  ///< Cache and reuse results by position hash.
        bool quiescence = true;           ///< Resolve captures at the horizon.
        bool iterative_deepening = true;  ///< Search depth 1, 2, ... and order by the last pass.
        bool pvs = true;                  ///< Zero-window search after the first move.
        bool null_move = true;            ///< Give up the move to prove a node is good enough.
        bool lmr = true;                  ///< Search late quiet moves to a reduced depth.
        bool check_extension = true;      ///< Search one ply deeper when in check.

        /**
         * @brief A named configuration, from the plain search to the full one.
         *
         * Names: "baseline", "ordering", "tt", "quiescence", "iterative", "full".
         *
         * @param name Preset name.
         * @param max_depth Nominal depth in plies.
         * @return The options, or the full configuration for an unknown name.
         */
        static SearchOptions preset(std::string_view name, int max_depth);
    };

    /**
     * Counters of one search.
     */
    struct SearchStats {
        uint64_t nodes = 0;             ///< Positions visited, including quiescence.
        uint64_t quiescence_nodes = 0;  ///< The part of `nodes` visited by quiescence.
        uint64_t tt_probes = 0;
        uint64_t tt_hits = 0;
        uint64_t tt_cutoffs = 0;
        uint64_t beta_cutoffs = 0;
        uint64_t first_move_cutoffs = 0;  ///< Cutoffs by the first move tried.
        uint64_t null_cutoffs = 0;
        uint64_t reductions = 0;  ///< Late moves searched at reduced depth.
        uint64_t re_searches = 0;
    };

    /**
     * Evaluation in the form a client shows: centipawns or moves to mate.
     */
    struct Score {
        bool is_mate = false;
        int value = 0;  ///< Centipawns, or moves to mate (negative when the mover is mated).
    };

    /**
     * The outcome of a search.
     */
    struct SearchResult {
        Move best_move;  ///< Null when the position has no legal move.
        int score = 0;   ///< Centipawns for the side to move; mates near kMateScore.
        int depth = 0;   ///< Deepest fully completed iteration.
        double elapsed_ms = 0;
        bool stopped = false;  ///< A limit or `stop()` ended the search early.
        std::vector<Move> pv;  ///< Best line, starting with `best_move`.
        SearchStats stats;

        /**
         * The score as centipawns or moves to mate.
         */
        Score display_score() const;
    };

    /**
     * One node of a recorded search tree.
     */
    struct TraceNode {
        int parent = -1;  ///< Index of the parent in the trace, -1 for the root.
        Move move;        ///< The move that led here from the parent.
        int ply = 0;
        int depth = 0;  ///< Remaining depth when the node was entered.
        int alpha = 0;
        int beta = 0;
        int score = 0;        ///< Score returned to the parent, from this node's mover.
        int moves_total = 0;  ///< Legal moves generated.
        int moves_searched = 0;
        bool cutoff = false;  ///< A move reached beta here.
        bool quiescence = false;
        bool from_tt = false;  ///< Answered by the transposition table.
        bool null_move = false;
    };

    /**
     * @brief Searches positions with alpha-beta.
     *
     * The transposition table and the move ordering tables persist between
     * calls, so one searcher should serve a whole game. A searcher is not
     * thread safe: use one per thread. `stop` may be called from another thread.
     */
    class Searcher {
    public:
        /**
         * @brief Creates a searcher.
         *
         * @param options Configuration, which can be changed later through `options`.
         */
        explicit Searcher(SearchOptions options = {});

        /**
         * The configuration; change it between searches, not during one.
         */
        SearchOptions &options() noexcept {
            return options_;
        }

        /**
         * @brief Searches for the best move.
         *
         * @param root Position to search.
         * @param history Hashes of the positions played before `root`, oldest first,
         *        used to recognize repetitions.
         * @return Best move, score, line and counters.
         */
        SearchResult search(const Position &root, const std::vector<uint64_t> &history = {});

        /**
         * Asks a running search to stop at the next check. Safe from any thread.
         */
        void stop() noexcept {
            stop_.store(true, std::memory_order_relaxed);
        }

        /**
         * Forgets the transposition table and move ordering tables.
         */
        void clear();

        /**
         * @brief Records the next searches into `sink`, or stops recording when null.
         *
         * Only nodes up to `max_ply` plies from the root are kept, and at most
         * `max_nodes`, so that a deep search cannot exhaust memory.
         *
         * @param sink Receives the nodes in the order they were entered.
         * @param max_ply Deepest ply recorded.
         * @param max_nodes Largest number of nodes recorded.
         */
        void set_trace(std::vector<TraceNode> *sink, int max_ply, size_t max_nodes);

    private:
        int negamax(const Position &pos, int depth, int alpha, int beta, int ply, bool pv,
                    bool allow_null, int trace_parent, Move trace_move);
        int quiescence(const Position &pos, int alpha, int beta, int ply, int trace_parent,
                       Move trace_move);
        int search_root(const Position &pos, int depth, int alpha, int beta);
        bool should_stop();
        bool is_repetition(const Position &pos) const;
        int trace_enter(int parent, Move move, int ply, int depth, int alpha, int beta,
                        bool quiescence, bool null_move);
        void trace_leave(int id, int score, int total, int searched, bool cutoff, bool from_tt);

        SearchOptions options_;
        TranspositionTable tt_;
        SearchStats stats_;

        std::atomic<bool> stop_{false};
        bool aborted_ = false;
        int64_t deadline_ns_ = 0;
        int64_t start_ns_ = 0;

        // Hashes of positions from the game and from the current search path.
        std::vector<uint64_t> keys_;

        Move killers_[kMaxPly][2];
        int history_[2][kSquares][kSquares];
        Move pv_[kMaxPly][kMaxPly];
        int pv_length_[kMaxPly];

        Move root_best_;
        int root_best_score_ = 0;
        std::vector<Move> root_order_;

        std::vector<TraceNode> *trace_ = nullptr;
        int trace_max_ply_ = 0;
        size_t trace_max_nodes_ = 0;
    };

    /**
     * @brief Writes a score as a client would show it.
     *
     * @param score Centipawns or a mate score.
     * @return `{false, cp}` or `{true, signed moves to mate}`.
     */
    Score to_display_score(int score);

}  // namespace chess
