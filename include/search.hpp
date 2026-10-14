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

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "eval.hpp"
#include "nnue.hpp"
#include "position.hpp"
#include "tt.hpp"
#include "types.hpp"

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

        /**
         * Time after which no new iteration starts, in milliseconds; 0 means no limit. Each
         * finished iteration compares the elapsed time with a fraction of this value, smaller
         * when the best move has stayed the same. `time_limit_ms` remains the hard limit.
         */
        int64_t soft_time_limit_ms = 0;
        uint64_t node_limit = 0;  ///< Node budget; 0 means no limit.
        size_t hash_mb = 16;      ///< Transposition table size.

        bool move_ordering = true;        ///< Try captures by MVV-LVA before quiet moves.
        bool killers_history = true;      ///< Order quiet moves by killer and history scores.
        bool transposition_table = true;  ///< Cache and reuse results by position hash.
        bool quiescence = true;           ///< Resolve captures at the horizon.
        bool iterative_deepening = true;  ///< Search depth 1, 2, ... and order by the last pass.
        bool pvs = true;                  ///< Zero-window search after the first move.
        bool null_move = true;            ///< Give up the move to prove a node is good enough.
        bool lmr = true;                  ///< Search late quiet moves to a reduced depth.
        bool check_extension = true;      ///< Search one ply deeper when in check.

        // Techniques that were tested by a match. The ones that helped are on by default, and the
        // others stay as switches with their results recorded.
        bool see = true;                   ///< Skip losing captures in quiescence by exchange.
        bool delta_pruning = false;        ///< Skip captures that cannot reach alpha.
        bool losing_captures_late = true;  ///< Try captures that lose material after quiet moves.
        bool reverse_futility = true;    ///< Return early when the static score is far above beta.
        bool futility = true;            ///< Skip quiet moves near the leaves that cannot help.
        bool razoring = false;           ///< Go to quiescence when the static score is far low.
        bool late_move_pruning = false;  ///< Skip late quiet moves at shallow depth.
        bool lmr_table = false;          ///< Reduce late moves by a logarithmic table.
        bool iir = false;                ///< Reduce a deep node that has no table move.
        bool null_margin = true;         ///< Reduce more in the null move at a large margin.
        bool aspiration = true;          ///< Search each pass in a window around the last.
        bool singular = false;           ///< Extend a table move that is better than the rest.
        bool counter_history = true;     ///< Order quiet moves by the move before them.
        bool tt_buckets = false;         ///< Two slots to a table bucket.

        // Experiments.
        bool root_entropy = false;           ///< Drop root moves of low probability.
        bool transposition_history = false;  ///< Share cutoff statistics between move orders.
        bool window_variance = false;        ///< Size aspiration windows by score variance.
        bool inertness = false;              ///< Reduce nodes that a one-ply probe finds inert.
        bool policy = false;                 ///< Order moves by a learned policy.
        bool legacy_eval = false;            ///< Evaluate with the tables before the tuning.
        bool tablebases = true;              ///< Use Syzygy tables, when some are loaded.
        bool history_malus = false;          ///< Lower the score of quiet moves that failed to cut.
        bool history_gravity = true;         ///< Decay a history entry in proportion to its size.
        bool history_piece = false;          ///< Key history by piece and destination.
        bool capture_history = false;        ///< Order captures by a learned table.
        bool continuation2 = false;          ///< A second continuation score, two plies back.
        bool counter_first = false;          ///< Try the counter move before the killers.
        bool underpromotions_late = false;   ///< Try knight, rook and bishop promotions late.

        // Parameters of the techniques above, in centipawns unless stated otherwise.
        int rfp_margin = 107;    ///< Reverse futility margin per ply.
        int futility_base = 74;  ///< Futility margin at depth 1, plus `futility_step` each ply.
        int futility_step = 214;
        int razor_base = 350;  ///< Razoring margin at depth 1, plus `razor_step` each ply.
        int razor_step = 150;
        int delta_margin = 200;        ///< Value added to a victim in delta pruning.
        int lmp_base = 3;              ///< Late move pruning count: base + depth squared.
        int lmr_scale = 225;           ///< Divisor of the reduction table, in hundredths.
        int iir_depth = 6;             ///< Smallest depth of the internal iterative reduction.
        int null_margin_step = 246;    ///< Margin over beta that adds one ply to the null move.
        int aspiration_delta = 18;     ///< Half width of the first aspiration window.
        int singular_depth = 8;        ///< Smallest depth of a singular extension search.
        int threads = 1;               ///< Search threads that share the table.
        std::vector<Move> root_moves;  ///< The moves the root is limited to; empty means all.
        int policy_scale = 16;         ///< Ordering score of one logit of the policy, in 1/64.
        int killer_count = 2;          ///< Killer moves kept for each ply, 1 or 2.
        int mvv_scale = 16;            ///< Ordering weight of the victim against the attacker.
        int bad_capture_margin = 0;    ///< A capture is late when its exchange is below minus this.
        int history_limit = 16384;     ///< Largest history score.
        int history_bonus = 8;         ///< History change of a cutoff, per ply squared.
        int malus_percent = 100;       ///< Malus of a failed move, in percent of the bonus.
        int continuation_limit = 16000;  ///< Largest continuation score.

        /**
         * Numbers of the tunable evaluation, or null for the evaluation of the engine.
         */
        std::shared_ptr<const EvalParams> eval_params;

        /**
         * A network for the evaluation, or null. It takes the place of `eval_params` and of the
         * hand written evaluation.
         */
        std::shared_ptr<const nnue::Network> nnue = nnue::builtin();

        /**
         * @brief A named configuration, from the plain search to the full one.
         *
         * Names: "baseline", "ordering", "tt", "quiescence", "iterative", "pruning", "full"
         * "tuned" and "network". "full" is the engine of step 08, with the evaluation, margins
         * and history of that step. "tuned" is step 09, which adds the tuned evaluation, margins
         * and history. "network" is step 10, which replaces the evaluation with the network
         * that is compiled into the library, and its values are the defaults of these
         * options. A name
         * may be followed by changes joined with `+`: a switch (`+see`, `+see=0`) or a parameter
         * (`+rfp_margin=100`), as in `full+delta_pruning+razoring`.
         *
         * @param name Preset name, with its changes.
         * @param max_depth Nominal depth in plies.
         * @return The options, or the network configuration for an unknown name.
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
        uint64_t tb_hits = 0;  ///< Positions that a tablebase decided.
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
     * What the search knows after one finished iteration.
     */
    struct IterationInfo {
        int depth = 0;
        int score = 0;  ///< Centipawns for the side to move; mates near kMateScore.
        uint64_t nodes = 0;
        uint64_t tb_hits = 0;
        double elapsed_ms = 0;
        std::vector<Move> pv;
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
         * @param table A table to share with other searchers, or null for a table of its own.
         */
        explicit Searcher(SearchOptions options = {},
                          std::shared_ptr<TranspositionTable> table = nullptr);

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
         * @return Best move, score, line and counters. With `options().threads` above 1, the
         *         other threads search the same position into the shared table, and the
         *         result is that of this thread.
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
         * @brief Calls `callback` after every finished iteration of the next searches.
         *
         * The call comes from the thread that runs `search`, and a search with helper threads
         * reports the nodes of that thread only. An empty function turns the reports off.
         *
         * @param callback Receives the iteration.
         */
        void set_info_callback(std::function<void(const IterationInfo &)> callback);

    private:
        SearchResult search_one(const Position &root, const std::vector<uint64_t> &history);
        void prepare_table();
        void clear_own_tables();
        int negamax(const Position &pos, int depth, int alpha, int beta, int ply, bool pv,
                    bool allow_null, Move last_move);
        int quiescence(const Position &pos, int alpha, int beta, int ply);
        int search_root(const Position &pos, int depth, int alpha, int beta);
        bool should_stop();

        /**
         * The static score of `pos`, which is at `ply`. With a network, the accumulator of that
         * ply has to hold the position.
         */
        int static_evaluation(const Position &pos, int ply) const noexcept {
            if (nnue_ != nullptr) {
                return nnue::evaluate(*nnue_, acc_[static_cast<size_t>(ply)], pos.side_to_move());
            }
            if (options_.eval_params) {
                return evaluate(pos, *options_.eval_params);
            }
            return options_.legacy_eval ? evaluate_legacy(pos) : evaluate(pos);
        }

        bool is_repetition(const Position &pos) const;

        SearchOptions options_;
        std::shared_ptr<TranspositionTable> tt_;

        // Helper threads of a search with `options.threads` above 1. They stop when the flag of
        // the main searcher is set, and a helper has its own searcher.
        std::vector<std::unique_ptr<Searcher>> helpers_;
        std::atomic<bool> helpers_stop_{false};
        const std::atomic<bool> *parent_stop_ = nullptr;
        int helper_id_ = 0;
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

        // The move excluded from the search of a node (singular extension), by ply.
        Move excluded_[kMaxPly];

        // The piece and destination of the move that led to each ply, or -1.
        int stack_[kMaxPly];

        // The largest position that the loaded tablebases decide, as of the start of the search.
        int tb_limit_ = 0;

        // The network of the search that runs, and the accumulator of the position at each ply.
        const nnue::Network *nnue_ = nullptr;
        std::vector<nnue::Accumulator> acc_;

        // History by piece and destination, captures by piece, destination and victim, and the
        // continuation score two plies back.
        int piece_history_[12][kSquares];
        int capture_history_[12][kSquares][6];
        std::vector<int16_t> continuation2_;

        // The move that refuted the move before it, and the score of a move after its
        // predecessor, both by the (piece, destination) of the predecessor.
        Move counter_move_[12 * kSquares];
        std::vector<int16_t> continuation_;

        // Reductions of the logarithmic table, by depth and by number of moves searched.
        uint8_t lmr_table_[64][64];

        // Scores of the root moves of the last completed pass, for the root entropy test.
        std::vector<std::pair<uint16_t, int>> root_scores_;
        std::vector<std::pair<uint16_t, int>> new_root_scores_;

        // Cutoff counts by position and move, shared between move orders (experiment).
        std::vector<int16_t> position_history_;

        // Running change of the score between passes, for the window of the next pass.
        double volatility_ = 20;

        Move root_best_;
        int root_best_score_ = 0;
        std::vector<Move> root_order_;

        std::function<void(const IterationInfo &)> info_callback_;
    };

    /**
     * @brief Writes a score as a client would show it.
     *
     * @param score Centipawns or a mate score.
     * @return `{false, cp}` or `{true, signed moves to mate}`.
     */
    Score to_display_score(int score);

}  // namespace chess
