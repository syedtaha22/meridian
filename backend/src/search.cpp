#include "chess/search.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

#include "chess/eval.hpp"
#include "chess/movegen.hpp"
#include "chess/policy.hpp"
#include "chess/see.hpp"

namespace chess {

    namespace {

        int64_t now_ns() {
            return std::chrono::duration_cast<std::chrono::nanoseconds>(
                       std::chrono::steady_clock::now().time_since_epoch())
                .count();
        }

        /**
         * Ordering scores. Anything from the table first, then captures by victim
         * value and attacker cost, then the killers, then quiet moves by history.
         */
        constexpr int kHashMoveScore = 2000000;
        constexpr int kCaptureScore = 1000000;
        constexpr int kPromotionScore = 900000;
        constexpr int kKillerScore = 800000;
        constexpr int kCounterMoveScore = 790000;
        constexpr int kLosingCaptureScore = -1000000;
        constexpr int kUnderPromotionScore = -500000;
        constexpr int kCaptureHistoryLimit = 16000;
        constexpr int kCaptureHistoryVictim = 20000;
        constexpr size_t kPositionHistorySize = size_t{1} << 20;

        size_t position_history_index(uint64_t key, Move move) {
            return static_cast<size_t>((key ^ (move.data * 0x9E3779B97F4A7C15ULL)) >> 20) &
                   (kPositionHistorySize - 1);
        }

        int to_tt_score(int score, int ply) {
            if (score >= kMateBound) {
                return score + ply;
            }
            if (score <= -kMateBound) {
                return score - ply;
            }
            return score;
        }

        int from_tt_score(int score, int ply) {
            if (score >= kMateBound) {
                return score - ply;
            }
            if (score <= -kMateBound) {
                return score + ply;
            }
            return score;
        }

        PieceType victim_type(const Position &pos, Move move) {
            if (move.is_en_passant()) {
                return kPawn;
            }
            return piece_type(pos.piece_on(move.to()));
        }

        int mvv_lva(const Position &pos, Move move, int scale) {
            const int victim = move.is_capture() ? static_cast<int>(victim_type(pos, move)) : 0;
            const int attacker = piece_type(pos.piece_on(move.from()));
            return kCaptureScore + victim * scale - attacker;
        }

        /**
         * Moves a history entry by `delta`, toward a limit. With gravity the entry decays in
         * proportion to its size, so recent results count more.
         */
        void bump(int &entry, int delta, int limit, bool gravity) {
            if (gravity) {
                entry +=
                    delta - static_cast<int>(static_cast<int64_t>(entry) * std::abs(delta) / limit);
            } else {
                entry = std::clamp(entry + delta, -limit, limit);
            }
        }

    }  // namespace

    namespace {

        /**
         * A switch or a parameter of `SearchOptions`, found by name.
         */
        struct Field {
            const char *name;
            bool SearchOptions::*flag;
            int SearchOptions::*number;
        };

        constexpr Field kFields[] = {
            {"move_ordering", &SearchOptions::move_ordering, nullptr},
            {"killers_history", &SearchOptions::killers_history, nullptr},
            {"transposition_table", &SearchOptions::transposition_table, nullptr},
            {"quiescence", &SearchOptions::quiescence, nullptr},
            {"iterative_deepening", &SearchOptions::iterative_deepening, nullptr},
            {"pvs", &SearchOptions::pvs, nullptr},
            {"null_move", &SearchOptions::null_move, nullptr},
            {"lmr", &SearchOptions::lmr, nullptr},
            {"check_extension", &SearchOptions::check_extension, nullptr},
            {"see", &SearchOptions::see, nullptr},
            {"delta_pruning", &SearchOptions::delta_pruning, nullptr},
            {"losing_captures_late", &SearchOptions::losing_captures_late, nullptr},
            {"reverse_futility", &SearchOptions::reverse_futility, nullptr},
            {"futility", &SearchOptions::futility, nullptr},
            {"razoring", &SearchOptions::razoring, nullptr},
            {"late_move_pruning", &SearchOptions::late_move_pruning, nullptr},
            {"lmr_table", &SearchOptions::lmr_table, nullptr},
            {"iir", &SearchOptions::iir, nullptr},
            {"null_margin", &SearchOptions::null_margin, nullptr},
            {"aspiration", &SearchOptions::aspiration, nullptr},
            {"singular", &SearchOptions::singular, nullptr},
            {"counter_history", &SearchOptions::counter_history, nullptr},
            {"tt_buckets", &SearchOptions::tt_buckets, nullptr},
            {"root_entropy", &SearchOptions::root_entropy, nullptr},
            {"transposition_history", &SearchOptions::transposition_history, nullptr},
            {"window_variance", &SearchOptions::window_variance, nullptr},
            {"inertness", &SearchOptions::inertness, nullptr},
            {"policy", &SearchOptions::policy, nullptr},
            {"legacy_eval", &SearchOptions::legacy_eval, nullptr},
            {"history_malus", &SearchOptions::history_malus, nullptr},
            {"history_gravity", &SearchOptions::history_gravity, nullptr},
            {"history_piece", &SearchOptions::history_piece, nullptr},
            {"capture_history", &SearchOptions::capture_history, nullptr},
            {"continuation2", &SearchOptions::continuation2, nullptr},
            {"counter_first", &SearchOptions::counter_first, nullptr},
            {"underpromotions_late", &SearchOptions::underpromotions_late, nullptr},
            {"rfp_margin", nullptr, &SearchOptions::rfp_margin},
            {"futility_base", nullptr, &SearchOptions::futility_base},
            {"futility_step", nullptr, &SearchOptions::futility_step},
            {"razor_base", nullptr, &SearchOptions::razor_base},
            {"razor_step", nullptr, &SearchOptions::razor_step},
            {"delta_margin", nullptr, &SearchOptions::delta_margin},
            {"lmp_base", nullptr, &SearchOptions::lmp_base},
            {"lmr_scale", nullptr, &SearchOptions::lmr_scale},
            {"iir_depth", nullptr, &SearchOptions::iir_depth},
            {"null_margin_step", nullptr, &SearchOptions::null_margin_step},
            {"aspiration_delta", nullptr, &SearchOptions::aspiration_delta},
            {"singular_depth", nullptr, &SearchOptions::singular_depth},
            {"threads", nullptr, &SearchOptions::threads},
            {"policy_scale", nullptr, &SearchOptions::policy_scale},
            {"killer_count", nullptr, &SearchOptions::killer_count},
            {"mvv_scale", nullptr, &SearchOptions::mvv_scale},
            {"bad_capture_margin", nullptr, &SearchOptions::bad_capture_margin},
            {"history_limit", nullptr, &SearchOptions::history_limit},
            {"history_bonus", nullptr, &SearchOptions::history_bonus},
            {"malus_percent", nullptr, &SearchOptions::malus_percent},
            {"continuation_limit", nullptr, &SearchOptions::continuation_limit},
        };

        /**
         * Applies one change, `name`, `name=0`, `name=1` or `name=value`, to the options.
         */
        void apply_change(SearchOptions &o, std::string_view change) {
            std::string_view key = change;
            int value = 1;
            const size_t equals = change.find('=');

            if (equals != std::string_view::npos) {
                key = change.substr(0, equals);
                value = std::atoi(std::string(change.substr(equals + 1)).c_str());
            }

            for (const Field &field : kFields) {
                if (key != field.name) {
                    continue;
                }

                if (field.flag != nullptr) {
                    o.*(field.flag) = value != 0;
                } else {
                    o.*(field.number) = value;
                }

                return;
            }
        }

    }  // namespace

    SearchOptions SearchOptions::preset(std::string_view name, int max_depth) {
        // The options default to the tuned engine. The steps before it have the evaluation,
        // margins and history that they had.
        const SearchOptions tuned;
        SearchOptions o;
        o.max_depth = max_depth;
        o.legacy_eval = true;
        o.rfp_margin = 120;
        o.futility_base = 100;
        o.futility_step = 150;
        o.null_margin_step = 200;
        o.aspiration_delta = 25;
        o.history_gravity = false;
        o.history_limit = 700000;
        o.history_bonus = 1;
        o.move_ordering = false;
        o.killers_history = false;
        o.transposition_table = false;
        o.quiescence = false;
        o.iterative_deepening = false;
        o.pvs = false;
        o.null_move = false;
        o.lmr = false;
        o.check_extension = false;
        o.see = false;
        o.losing_captures_late = false;
        o.reverse_futility = false;
        o.futility = false;
        o.null_margin = false;
        o.aspiration = false;
        o.counter_history = false;

        // The changes after the name are applied last, on whatever the name gave.
        const size_t plus = name.find('+');
        const std::string_view base = name.substr(0, plus);
        std::vector<std::string_view> changes;

        for (size_t at = plus; at != std::string_view::npos;) {
            const size_t next = name.find('+', at + 1);
            changes.push_back(name.substr(
                at + 1, next == std::string_view::npos ? std::string_view::npos : next - at - 1));
            at = next;
        }

        auto finish = [&]() {
            for (std::string_view change : changes) {
                apply_change(o, change);
            }

            return o;
        };

        if (base == "baseline") {
            return finish();
        }

        o.move_ordering = true;

        if (base == "ordering") {
            return finish();
        }

        o.transposition_table = true;

        if (base == "tt") {
            return finish();
        }

        o.quiescence = true;

        if (base == "quiescence") {
            return finish();
        }

        o.iterative_deepening = true;
        o.pvs = true;

        if (base == "iterative") {
            return finish();
        }

        o.killers_history = true;
        o.null_move = true;
        o.lmr = true;
        o.check_extension = true;

        if (base == "pruning") {
            return finish();
        }

        // The refinements that won their matches against the engine without them.
        o.see = true;
        o.losing_captures_late = true;
        o.reverse_futility = true;
        o.futility = true;
        o.null_margin = true;
        o.aspiration = true;
        o.counter_history = true;

        if (base == "tuned") {
            o.legacy_eval = tuned.legacy_eval;
            o.rfp_margin = tuned.rfp_margin;
            o.futility_base = tuned.futility_base;
            o.futility_step = tuned.futility_step;
            o.null_margin_step = tuned.null_margin_step;
            o.aspiration_delta = tuned.aspiration_delta;
            o.history_gravity = tuned.history_gravity;
            o.history_limit = tuned.history_limit;
            o.history_bonus = tuned.history_bonus;
        }

        return finish();
    }

    Score to_display_score(int score) {
        if (score >= kMateBound) {
            return {true, (kMateScore - score + 1) / 2};
        }
        if (score <= -kMateBound) {
            return {true, -((kMateScore + score + 1) / 2)};
        }
        return {false, score};
    }

    Score SearchResult::display_score() const {
        return to_display_score(score);
    }

    Searcher::Searcher(SearchOptions options, std::shared_ptr<TranspositionTable> table)
        : options_(options),
          tt_(table ? std::move(table) : std::make_shared<TranspositionTable>(options.hash_mb)) {
        // A helper shares the table of its main searcher, which clears or resizes it.
        clear_own_tables();
    }

    void Searcher::clear() {
        tt_->resize(options_.hash_mb);
        clear_own_tables();

        for (auto &helper : helpers_) {
            helper->clear_own_tables();
        }
    }

    void Searcher::clear_own_tables() {
        std::memset(killers_, 0, sizeof(killers_));
        std::memset(history_, 0, sizeof(history_));
        std::memset(piece_history_, 0, sizeof(piece_history_));
        std::memset(capture_history_, 0, sizeof(capture_history_));

        for (Move &move : counter_move_) {
            move = Move();
        }

        continuation_.assign(static_cast<size_t>(12 * kSquares) * 12 * kSquares, 0);

        if (options_.continuation2) {
            continuation2_.assign(static_cast<size_t>(12 * kSquares) * 12 * kSquares, 0);
        }

        position_history_.assign(kPositionHistorySize, 0);
        root_scores_.clear();
    }

    void Searcher::set_trace(std::vector<TraceNode> *sink, int max_ply, size_t max_nodes) {
        trace_ = sink;
        trace_max_ply_ = max_ply;
        trace_max_nodes_ = max_nodes;
    }

    int Searcher::trace_enter(int parent, Move move, int ply, int depth, int alpha, int beta,
                              bool quiescence, bool null_move) {
        if (trace_ == nullptr || ply > trace_max_ply_ || trace_->size() >= trace_max_nodes_) {
            return -1;
        }

        TraceNode node;
        node.parent = parent;
        node.move = move;
        node.ply = ply;

        node.depth = depth;
        node.alpha = alpha;
        node.beta = beta;

        node.quiescence = quiescence;
        node.null_move = null_move;
        trace_->push_back(node);

        return static_cast<int>(trace_->size()) - 1;
    }

    void Searcher::trace_leave(int id, int score, int total, int searched, bool cutoff,
                               bool from_tt) {
        if (id < 0) {
            return;
        }

        TraceNode &node = (*trace_)[id];
        node.score = score;
        node.moves_total = total;
        node.moves_searched = searched;
        node.cutoff = cutoff;
        node.from_tt = from_tt;
    }

    bool Searcher::should_stop() {
        if (aborted_) {
            return true;
        }
        if (stop_.load(std::memory_order_relaxed) ||
            (parent_stop_ != nullptr && parent_stop_->load(std::memory_order_relaxed))) {
            aborted_ = true;
        } else if ((stats_.nodes & 1023) == 0) {
            if (deadline_ns_ != 0 && now_ns() >= deadline_ns_) {
                aborted_ = true;
            }
            if (options_.node_limit != 0 && stats_.nodes >= options_.node_limit) {
                aborted_ = true;
            }
        }

        return aborted_;
    }

    bool Searcher::is_repetition(const Position &pos) const {
        // Only positions with the same side to move can repeat, so step back two plies at a
        // time: no sooner than four plies ago, and no further than the last irreversible
        // move. An earlier occurrence inside the search tree or in the game is a draw by
        // choice, so one repetition is enough.
        const int limit = std::min<int>(pos.halfmove_clock(), static_cast<int>(keys_.size()) - 1);

        for (int back = 4; back <= limit; back += 2) {
            if (keys_[keys_.size() - 1 - static_cast<size_t>(back)] == pos.key()) {
                return true;
            }
        }
        return false;
    }

    void Searcher::prepare_table() {
        if (options_.transposition_table) {
            if (tt_->megabytes() != options_.hash_mb) {
                tt_->resize(options_.hash_mb);
            }

            tt_->set_buckets(options_.tt_buckets);
            tt_->new_generation();
        }
    }

    SearchResult Searcher::search(const Position &root, const std::vector<uint64_t> &history) {
        prepare_table();

        if (options_.threads <= 1) {
            return search_one(root, history);
        }

        const size_t wanted = static_cast<size_t>(options_.threads - 1);

        while (helpers_.size() < wanted) {
            SearchOptions helper_options = options_;
            helper_options.threads = 1;

            auto helper = std::make_unique<Searcher>(helper_options, tt_);
            helper->parent_stop_ = &helpers_stop_;
            helper->helper_id_ = static_cast<int>(helpers_.size()) + 1;
            helpers_.push_back(std::move(helper));
        }

        helpers_stop_.store(false);
        std::vector<std::thread> threads;

        for (size_t i = 0; i < wanted; ++i) {
            Searcher *helper = helpers_[i].get();
            helper->options_ = options_;
            helper->options_.threads = 1;
            threads.emplace_back([helper, &root, &history] { helper->search_one(root, history); });
        }

        SearchResult result = search_one(root, history);

        helpers_stop_.store(true);

        for (std::thread &thread : threads) {
            thread.join();
        }

        return result;
    }

    SearchResult Searcher::search_one(const Position &root, const std::vector<uint64_t> &history) {
        SearchResult result;
        stats_ = SearchStats{};
        stop_.store(false);
        aborted_ = false;
        start_ns_ = now_ns();
        deadline_ns_ =
            options_.time_limit_ms > 0 ? start_ns_ + options_.time_limit_ms * 1000000 : 0;
        for (auto &row : killers_) {
            row[0] = row[1] = Move();
        }

        keys_ = history;
        keys_.push_back(root.key());
        volatility_ = 20;

        if (options_.continuation2 && continuation2_.empty()) {
            continuation2_.assign(static_cast<size_t>(12 * kSquares) * 12 * kSquares, 0);
        }

        for (Move &move : excluded_) {
            move = Move();
        }

        if (options_.lmr_table) {
            const double scale = options_.lmr_scale / 100.0;

            for (int d = 1; d < 64; ++d) {
                for (int m = 1; m < 64; ++m) {
                    lmr_table_[d][m] = static_cast<uint8_t>(
                        std::max(0.0, 0.75 + std::log(d) * std::log(m) / scale));
                }
            }
        }

        MoveList legal;
        generate_legal(root, legal);

        if (legal.empty()) {
            result.score = root.in_check() ? -kMateScore : 0;
            result.elapsed_ms = (now_ns() - start_ns_) / 1e6;
            return result;
        }

        result.best_move = legal[0];

        const int max_depth = std::clamp(options_.max_depth, 1, kMaxPly - 8);
        const int first_depth = options_.iterative_deepening ? 1 : max_depth;
        root_order_.clear();
        root_best_ = Move();

        for (int depth = first_depth; depth <= max_depth; ++depth) {
            // Helpers skip some depths, so that the threads do not repeat each other.
            if (helper_id_ > 0 && depth > 3 && (depth + helper_id_) % (helper_id_ + 2) == 0) {
                continue;
            }

            root_best_ = Move();

            // The recording describes the latest iteration only.
            if (trace_ != nullptr) {
                trace_->clear();
            }

            int score = 0;
            int low = -kInfinity;
            int high = kInfinity;
            int delta = options_.aspiration_delta;

            if (options_.window_variance) {
                delta = std::clamp(static_cast<int>(1.5 * volatility_), 10, 100);
            }

            // After the first passes, a window around the last score is searched first. A
            // result outside it is searched again in a wider window.
            if ((options_.aspiration || options_.window_variance) && depth >= 4 &&
                !result.pv.empty() && std::abs(result.score) < kMateBound) {
                low = std::max(-kInfinity, result.score - delta);
                high = std::min(kInfinity, result.score + delta);
            }

            while (true) {
                root_best_ = Move();

                if (trace_ != nullptr) {
                    trace_->clear();
                }

                score = search_root(root, depth, low, high);

                if (aborted_) {
                    break;
                }

                if (score <= low && low > -kInfinity) {
                    delta *= 2;
                    low = delta > 700 ? -kInfinity : std::max(-kInfinity, score - delta);
                } else if (score >= high && high < kInfinity) {
                    delta *= 2;
                    high = delta > 700 ? kInfinity : std::min(kInfinity, score + delta);
                } else {
                    break;
                }
            }

            // A search cut short is only trusted for its first completed iteration or when
            // it found a move before being stopped on the first iteration.
            if (aborted_ && depth > first_depth) {
                break;
            }
            if (!root_best_.is_null()) {
                if (!result.pv.empty()) {
                    volatility_ = 0.7 * volatility_ + 0.3 * std::abs(score - result.score);
                }

                result.best_move = root_best_;
                result.score = score;
                result.depth = aborted_ ? depth - 1 : depth;
                result.pv.assign(pv_[0], pv_[0] + pv_length_[0]);
                root_order_.assign(1, root_best_);
            }

            if (aborted_) {
                break;
            }

            // Stop deepening once a forced mate is found and cannot be improved on.
            if (std::abs(score) >= kMateBound && depth >= (kMateScore - std::abs(score)) + 1) {
                break;
            }
        }

        result.stopped = aborted_;
        result.stats = stats_;
        result.elapsed_ms = (now_ns() - start_ns_) / 1e6;

        if (result.pv.empty() || result.pv.front() != result.best_move) {
            result.pv.assign(1, result.best_move);
        }
        return result;
    }

    int Searcher::search_root(const Position &pos, int depth, int alpha, int beta) {
        const int trace_id = trace_enter(-1, Move(), 0, depth, alpha, beta, false, false);
        pv_length_[0] = 0;

        MoveList list;
        generate_pseudo_legal(pos, list);

        // Order: previous best first (iterative deepening), then the ordering by score.
        int scores[kMaxMoves];
        TTEntry found;
        const TTEntry *entry =
            options_.transposition_table && tt_->probe(pos.key(), found) ? &found : nullptr;

        for (int i = 0; i < list.size(); ++i) {
            const Move move = list[i];
            int s = 0;

            if (options_.move_ordering) {
                if (!root_order_.empty() && move == root_order_[0]) {
                    s = kHashMoveScore + 1;
                } else if (entry != nullptr && move == entry->move) {
                    s = kHashMoveScore;
                } else if (move.is_capture()) {
                    s = mvv_lva(pos, move, options_.mvv_scale);
                } else if (move.is_promotion()) {
                    s = kPromotionScore;
                }
            }

            scores[i] = s;
        }

        int best = -kInfinity;
        int searched = 0;

        // Root entropy: the scores of the last pass give each root move a probability of being
        // best, and the moves with a share under 2% are searched two plies shallower first.
        auto previous_score = [&](Move move, int fallback) {
            for (const auto &entry_score : root_scores_) {
                if (entry_score.first == move.data) {
                    return entry_score.second;
                }
            }

            return fallback;
        };

        const bool entropic = options_.root_entropy && depth >= 6 && !root_scores_.empty();
        int best_previous = -kInfinity;
        double mass = 0;

        if (entropic) {
            for (const auto &entry_score : root_scores_) {
                best_previous = std::max(best_previous, entry_score.second);
            }

            for (const auto &entry_score : root_scores_) {
                mass += std::exp(-(best_previous - entry_score.second) / 100.0);
            }
        }

        new_root_scores_.clear();

        for (int i = 0; i < list.size(); ++i) {
            if (options_.move_ordering) {
                int pick = i;

                for (int j = i + 1; j < list.size(); ++j) {
                    if (scores[j] > scores[pick]) {
                        pick = j;
                    }
                }

                std::swap(list[i], list[pick]);
                std::swap(scores[i], scores[pick]);
            }

            const Move move = list[i];
            Position next = pos;

            if (!next.make_move(move)) {
                continue;
            }

            keys_.push_back(next.key());
            int score;

            if (searched == 0 || !options_.pvs) {
                score = -negamax(next, depth - 1, -beta, -alpha, 1, true, true, trace_id, move);
            } else {
                int root_reduction = 0;

                if (entropic && !move.is_capture() && !move.is_promotion() && depth >= 4) {
                    const int s = previous_score(move, best_previous);

                    if (std::exp(-(best_previous - s) / 100.0) / mass < 0.02) {
                        root_reduction = 2;
                    }
                }

                score = -negamax(next, depth - 1 - root_reduction, -alpha - 1, -alpha, 1, false,
                                 true, trace_id, move);

                if (score > alpha && root_reduction > 0 && !aborted_) {
                    score = -negamax(next, depth - 1, -alpha - 1, -alpha, 1, false, true, trace_id,
                                     move);
                }
                if (score > alpha && !aborted_) {
                    score = -negamax(next, depth - 1, -beta, -alpha, 1, true, true, trace_id, move);
                }
            }

            keys_.pop_back();
            ++searched;

            if (aborted_) {
                break;
            }

            new_root_scores_.emplace_back(move.data, score);

            if (score > best) {
                best = score;

                if (score > alpha) {
                    alpha = score;
                    root_best_ = move;
                    pv_[0][0] = move;
                    const int child_length = pv_length_[1];

                    for (int k = 0; k < child_length; ++k) {
                        pv_[0][k + 1] = pv_[1][k];
                    }

                    pv_length_[0] = child_length + 1;
                }
            }
        }

        if (!aborted_ && options_.root_entropy) {
            root_scores_ = new_root_scores_;
        }

        if (root_best_.is_null() && best > -kInfinity && !aborted_) {
            // Every move failed low against the window: cannot happen with a full window.
            root_best_ = list[0];
        }

        trace_leave(trace_id, best, list.size(), searched, false, false);
        return best;
    }

    int Searcher::negamax(const Position &pos, int depth, int alpha, int beta, int ply, bool pv,
                          bool allow_null, int trace_parent, Move trace_move) {
        pv_length_[ply] = 0;

        if (should_stop()) {
            return 0;
        }

        const bool in_check = pos.in_check();

        if (in_check && options_.check_extension && ply < kMaxPly - 8) {
            ++depth;
        }
        if (depth <= 0) {
            if (options_.quiescence) {
                return quiescence(pos, alpha, beta, ply, trace_parent, trace_move);
            }

            ++stats_.nodes;
            const int id = trace_enter(trace_parent, trace_move, ply, 0, alpha, beta, false, false);
            const int value = static_evaluation(pos);
            trace_leave(id, value, 0, 0, false, false);

            return value;
        }

        ++stats_.nodes;
        const int trace_id = trace_enter(trace_parent, trace_move, ply, depth, alpha, beta, false,
                                         trace_move.is_null() && trace_parent >= 0);

        // Draws that do not depend on the moves left to play.
        if (pos.halfmove_clock() >= 100 || pos.insufficient_material() || is_repetition(pos)) {
            trace_leave(trace_id, 0, 0, 0, false, false);
            return 0;
        }
        if (ply >= kMaxPly - 2) {
            const int value = static_evaluation(pos);
            trace_leave(trace_id, value, 0, 0, false, false);
            return value;
        }

        const int original_alpha = alpha;
        const Move excluded = excluded_[ply];
        Move tt_move;
        int tt_score = 0;
        int tt_depth = -1;
        uint8_t tt_bound = kBoundNone;

        if (options_.transposition_table) {
            ++stats_.tt_probes;

            TTEntry probed;

            if (const TTEntry *entry = tt_->probe(pos.key(), probed) ? &probed : nullptr) {
                ++stats_.tt_hits;
                tt_move = entry->move;
                tt_score = from_tt_score(entry->score, ply);
                tt_depth = entry->depth;
                tt_bound = entry->bound;

                if (!pv && excluded.is_null() && entry->depth >= depth) {
                    const int score = from_tt_score(entry->score, ply);

                    if (entry->bound == kBoundExact ||
                        (entry->bound == kBoundLower && score >= beta) ||
                        (entry->bound == kBoundUpper && score <= alpha)) {
                        ++stats_.tt_cutoffs;
                        trace_leave(trace_id, score, 0, 0, false, true);

                        return score;
                    }
                }
            }
        }

        // The static score is computed once, when a technique first needs it.
        int static_eval = 0;
        bool have_eval = false;
        auto eval = [&]() {
            if (!have_eval) {
                static_eval = static_evaluation(pos);
                have_eval = true;
            }

            return static_eval;
        };

        const bool quiet_node = !pv && !in_check && excluded.is_null() &&
                                std::abs(alpha) < kMateBound && std::abs(beta) < kMateBound;

        // Razoring: far below alpha at the last plies, quiescence decides.
        if (options_.razoring && quiet_node && depth <= 3 &&
            eval() + options_.razor_base + options_.razor_step * (depth - 1) < alpha) {
            const int value = quiescence(pos, alpha, beta, ply, trace_id, Move());

            if (aborted_) {
                return 0;
            }
            if (value <= alpha) {
                trace_leave(trace_id, value, 0, 0, false, false);
                return value;
            }
        }

        // Reverse futility: far above beta at the last plies, the node fails high.
        if (options_.reverse_futility && quiet_node && depth <= 4 && pos.has_non_pawn_material() &&
            eval() - options_.rfp_margin * depth >= beta) {
            const int value = eval() - options_.rfp_margin * depth;
            trace_leave(trace_id, value, 0, 0, true, false);
            return value;
        }

        // Internal iterative reduction: a deep node without a table move is searched shallower
        // now, and the pass that follows fills the table.
        if (options_.iir && depth >= options_.iir_depth && tt_move.is_null() && !in_check &&
            excluded.is_null()) {
            --depth;
        }

        // Inertness: a node far outside the window with no capture that gains material is
        // searched one ply shallower.
        if (options_.inertness && quiet_node && depth >= 3 && tt_move.is_null()) {
            const int value = eval();

            if (value >= beta + 60 || value + 60 <= alpha) {
                MoveList captures;
                generate_captures(pos, captures);
                bool tactical = false;

                for (int c = 0; c < captures.size() && !tactical; ++c) {
                    tactical = captures[c].is_promotion() || see(pos, captures[c]) > 0;
                }

                if (!tactical) {
                    --depth;
                }
            }
        }

        // Null move: if passing the turn still scores at least beta, the position is good
        // enough for the side to move that a real move would too.
        if (options_.null_move && allow_null && !pv && !in_check && depth >= 3 &&
            excluded.is_null() && pos.has_non_pawn_material() && eval() >= beta) {
            Position next = pos;
            next.make_null_move();
            keys_.push_back(next.key());
            int reduction = 2 + depth / 6;

            if (options_.null_margin) {
                reduction += std::min(3, (eval() - beta) / options_.null_margin_step);
            }

            const int score = -negamax(next, depth - 1 - reduction, -beta, -beta + 1, ply + 1,
                                       false, false, trace_id, Move());
            keys_.pop_back();

            if (aborted_) {
                return 0;
            }
            if (score >= beta) {
                ++stats_.null_cutoffs;
                trace_leave(trace_id, beta, 0, 0, true, false);
                return beta;
            }
        }

        // Singular extension: when every move but the table move fails low by a margin below
        // the table score, that move is forced and is searched one ply deeper.
        int extension = 0;

        if (options_.singular && depth >= options_.singular_depth && !tt_move.is_null() &&
            excluded.is_null() && !in_check && std::abs(tt_score) < kMateBound &&
            (tt_bound == kBoundLower || tt_bound == kBoundExact) && tt_depth >= depth - 3) {
            const int target = tt_score - 2 * depth;
            excluded_[ply] = tt_move;
            const int value = negamax(pos, (depth - 1) / 2, target - 1, target, ply, false, false,
                                      trace_id, Move());
            excluded_[ply] = Move();

            if (aborted_) {
                return 0;
            }
            if (value < target) {
                extension = 1;
            }
        }

        MoveList list;
        generate_pseudo_legal(pos, list);
        int scores[kMaxMoves];
        const Color us = pos.side_to_move();

        // The previous move, for the counter move and the continuation scores.
        const bool has_previous = options_.counter_history && !trace_move.is_null();
        const int previous =
            has_previous ? pos.piece_on(trace_move.to()) * kSquares + trace_move.to() : 0;

        // The piece and destination of the move that led here, by ply, for the two-ply table.
        if (options_.continuation2) {
            stack_[ply] = trace_move.is_null()
                              ? -1
                              : pos.piece_on(trace_move.to()) * kSquares + trace_move.to();
        }

        const int history_limit = options_.history_limit;
        const bool history_two =
            options_.continuation2 && options_.counter_history && ply >= 2 && stack_[ply - 1] >= 0;

        for (int i = 0; i < list.size(); ++i) {
            const Move move = list[i];
            int s = 0;

            if (options_.move_ordering) {
                if (!tt_move.is_null() && move == tt_move) {
                    s = kHashMoveScore;
                } else if (move.is_capture()) {
                    s = mvv_lva(pos, move, options_.mvv_scale);

                    if (options_.capture_history) {
                        s = kCaptureScore +
                            static_cast<int>(victim_type(pos, move)) * kCaptureHistoryVictim +
                            capture_history_[pos.piece_on(move.from())][move.to()]
                                            [victim_type(pos, move)] -
                            piece_type(pos.piece_on(move.from()));
                    }

                    if (options_.losing_captures_late && !move.is_promotion() &&
                        see(pos, move) < -options_.bad_capture_margin) {
                        s = kLosingCaptureScore + s - kCaptureScore;
                    }
                } else if (move.is_promotion()) {
                    s = options_.underpromotions_late && move.promotion_type() != kQueen
                            ? kUnderPromotionScore
                            : kPromotionScore;
                } else if (options_.killers_history) {
                    const bool counter = has_previous && move == counter_move_[previous];

                    if (options_.counter_first && counter) {
                        s = kKillerScore + 1;
                    } else if (move == killers_[ply][0]) {
                        s = kKillerScore;
                    } else if (options_.killer_count > 1 && move == killers_[ply][1]) {
                        s = kKillerScore - 1;
                    } else if (counter) {
                        s = kCounterMoveScore;
                    } else {
                        const int piece = pos.piece_on(move.from());
                        s = std::min(options_.history_piece ? piece_history_[piece][move.to()]
                                                            : history_[us][move.from()][move.to()],
                                     history_limit);

                        if (options_.transposition_history) {
                            s += position_history_[position_history_index(pos.key(), move)] * 4;
                        }

                        if (options_.policy) {
                            s += policy::score(pos, move) * options_.policy_scale;
                        }

                        if (has_previous) {
                            s += continuation_[static_cast<size_t>(previous) * 12 * kSquares +
                                               static_cast<size_t>(piece * kSquares + move.to())];
                        }

                        if (history_two) {
                            s += continuation2_[static_cast<size_t>(stack_[ply - 1]) * 12 *
                                                    kSquares +
                                                static_cast<size_t>(piece * kSquares + move.to())];
                        }
                    }
                }
            }

            scores[i] = s;
        }

        int best = -kInfinity;
        Move best_move;
        int legal_moves = 0;
        int searched = 0;
        bool cutoff = false;

        // Moves searched without a cutoff, to lower their scores when a later move cuts.
        Move tried_quiets[64];
        Move tried_captures[32];
        int tried_quiet_count = 0;
        int tried_capture_count = 0;

        for (int i = 0; i < list.size(); ++i) {
            if (options_.move_ordering) {
                int pick = i;

                for (int j = i + 1; j < list.size(); ++j) {
                    if (scores[j] > scores[pick]) {
                        pick = j;
                    }
                }

                std::swap(list[i], list[pick]);
                std::swap(scores[i], scores[pick]);
            }

            const Move move = list[i];

            if (!excluded.is_null() && move == excluded) {
                continue;
            }

            Position next = pos;

            if (!next.make_move(move)) {
                continue;
            }

            ++legal_moves;

            const bool quiet = !move.is_capture() && !move.is_promotion();

            // Quiet moves that are late or cannot reach alpha are not searched at the last plies.
            if (searched > 0 && quiet && !pv && !in_check && best > -kMateBound &&
                std::abs(alpha) < kMateBound && !next.in_check()) {
                if (options_.late_move_pruning && depth <= 4 &&
                    searched >= options_.lmp_base + depth * depth) {
                    continue;
                }

                if (options_.futility && depth <= 3) {
                    const int bound =
                        eval() + options_.futility_base + options_.futility_step * (depth - 1);

                    if (bound <= alpha) {
                        best = std::max(best, bound);
                        continue;
                    }
                }
            }

            keys_.push_back(next.key());

            const int move_extension = move == tt_move ? extension : 0;
            int score;

            if (searched == 0 || !options_.pvs) {
                score = -negamax(next, depth - 1 + move_extension, -beta, -alpha, ply + 1, pv, true,
                                 trace_id, move);
            } else {
                int reduction = 0;

                if (options_.lmr && depth >= 3 && searched >= 3 && quiet && !in_check &&
                    !next.in_check()) {
                    if (options_.lmr_table) {
                        reduction = lmr_table_[std::min(depth, 63)][std::min(searched, 63)];

                        if (pv) {
                            --reduction;
                        }
                        if (move == killers_[ply][0] || move == killers_[ply][1]) {
                            --reduction;
                        }
                        if (history_[us][move.from()][move.to()] > options_.history_limit / 4) {
                            --reduction;
                        }

                        reduction = std::clamp(reduction, 0, depth - 2);
                    } else {
                        reduction = 1 + (searched >= 8 ? 1 : 0);
                    }

                    if (reduction > 0) {
                        ++stats_.reductions;
                    }
                }

                score = -negamax(next, depth - 1 - reduction + move_extension, -alpha - 1, -alpha,
                                 ply + 1, false, true, trace_id, move);
                if (score > alpha && reduction > 0 && !aborted_) {
                    ++stats_.re_searches;
                    score = -negamax(next, depth - 1 + move_extension, -alpha - 1, -alpha, ply + 1,
                                     false, true, trace_id, move);
                }
                if (score > alpha && score < beta && !aborted_) {
                    ++stats_.re_searches;
                    score = -negamax(next, depth - 1 + move_extension, -beta, -alpha, ply + 1, true,
                                     true, trace_id, move);
                }
            }

            keys_.pop_back();
            ++searched;

            if (aborted_) {
                return 0;
            }

            if (score > best) {
                best = score;
                best_move = move;

                if (score > alpha) {
                    alpha = score;
                    pv_[ply][0] = move;
                    const int child_length = pv_length_[ply + 1];

                    for (int k = 0; k < child_length; ++k) {
                        pv_[ply][k + 1] = pv_[ply + 1][k];
                    }

                    pv_length_[ply] = child_length + 1;

                    if (alpha >= beta) {
                        cutoff = true;
                        ++stats_.beta_cutoffs;

                        if (searched == 1) {
                            ++stats_.first_move_cutoffs;
                        }
                        if (options_.killers_history && quiet) {
                            if (killers_[ply][0] != move) {
                                killers_[ply][1] = killers_[ply][0];
                                killers_[ply][0] = move;
                            }

                            const int bonus = depth * depth * options_.history_bonus;
                            const bool gravity = options_.history_gravity;

                            // Credits `moved` by `delta` in the quiet move tables.
                            auto credit = [&](Move moved, int delta) {
                                const int piece = pos.piece_on(moved.from());
                                int &h = options_.history_piece
                                             ? piece_history_[piece][moved.to()]
                                             : history_[us][moved.from()][moved.to()];
                                bump(h, delta, history_limit, gravity);

                                const size_t slot =
                                    static_cast<size_t>(piece * kSquares + moved.to());

                                if (has_previous) {
                                    int16_t &c = continuation_[static_cast<size_t>(previous) * 12 *
                                                                   kSquares +
                                                               slot];
                                    int value = c;
                                    bump(value, delta, options_.continuation_limit, gravity);
                                    c = static_cast<int16_t>(value);
                                }

                                if (history_two) {
                                    int16_t &c =
                                        continuation2_[static_cast<size_t>(stack_[ply - 1]) * 12 *
                                                           kSquares +
                                                       slot];
                                    int value = c;
                                    bump(value, delta, options_.continuation_limit, gravity);
                                    c = static_cast<int16_t>(value);
                                }
                            };

                            credit(move, bonus);

                            if (options_.history_malus) {
                                const int malus = bonus * options_.malus_percent / 100;

                                for (int t = 0; t < tried_quiet_count; ++t) {
                                    credit(tried_quiets[t], -malus);
                                }
                            }

                            if (options_.transposition_history) {
                                int16_t &p =
                                    position_history_[position_history_index(pos.key(), move)];
                                p = static_cast<int16_t>(std::min(p + depth * depth, 30000));
                            }

                            if (has_previous) {
                                counter_move_[previous] = move;
                            }
                        } else if (options_.capture_history && move.is_capture()) {
                            const int bonus = depth * depth * options_.history_bonus;
                            const bool gravity = options_.history_gravity;

                            auto credit = [&](Move moved, int delta) {
                                int &c = capture_history_[pos.piece_on(moved.from())][moved.to()]
                                                         [victim_type(pos, moved)];
                                bump(c, delta, kCaptureHistoryLimit, gravity);
                            };

                            credit(move, bonus);

                            if (options_.history_malus) {
                                const int malus = bonus * options_.malus_percent / 100;

                                for (int t = 0; t < tried_capture_count; ++t) {
                                    credit(tried_captures[t], -malus);
                                }
                            }
                        }

                        break;
                    }
                }
            }

            if (quiet) {
                if (tried_quiet_count < 64) {
                    tried_quiets[tried_quiet_count++] = move;
                }
            } else if (move.is_capture() && tried_capture_count < 32) {
                tried_captures[tried_capture_count++] = move;
            }
        }

        if (!excluded.is_null() && legal_moves == 0) {
            trace_leave(trace_id, alpha, 0, 0, false, false);
            return alpha;
        }

        if (legal_moves == 0) {
            best = in_check ? -(kMateScore - ply) : 0;
        }

        if (options_.transposition_table && excluded.is_null()) {
            const Bound bound = best >= beta            ? kBoundLower
                                : best > original_alpha ? kBoundExact
                                                        : kBoundUpper;
            tt_->store(pos.key(), best_move, to_tt_score(best, ply), depth, bound);
        }

        trace_leave(trace_id, best, list.size(), searched, cutoff, false);
        return best;
    }

    int Searcher::quiescence(const Position &pos, int alpha, int beta, int ply, int trace_parent,
                             Move trace_move) {
        if (should_stop()) {
            return 0;
        }

        ++stats_.nodes;
        ++stats_.quiescence_nodes;
        const int trace_id =
            trace_enter(trace_parent, trace_move, ply, 0, alpha, beta, true, false);

        if (ply >= kMaxPly - 2) {
            const int value = static_evaluation(pos);
            trace_leave(trace_id, value, 0, 0, false, false);
            return value;
        }

        const bool in_check = pos.in_check();
        int best = -kInfinity;

        if (!in_check) {
            best = static_evaluation(pos);

            if (best >= beta) {
                trace_leave(trace_id, best, 0, 0, true, false);
                return best;
            }

            alpha = std::max(alpha, best);
        }

        MoveList list;

        if (in_check) {
            generate_pseudo_legal(pos, list);
        } else {
            generate_captures(pos, list);
        }

        int scores[kMaxMoves];

        for (int i = 0; i < list.size(); ++i) {
            scores[i] =
                list[i].is_capture() ? mvv_lva(pos, list[i], options_.mvv_scale) : kPromotionScore;
        }

        int legal_moves = 0;
        int searched = 0;
        bool cutoff = false;

        for (int i = 0; i < list.size(); ++i) {
            int pick = i;

            for (int j = i + 1; j < list.size(); ++j) {
                if (scores[j] > scores[pick]) {
                    pick = j;
                }
            }

            std::swap(list[i], list[pick]);
            std::swap(scores[i], scores[pick]);

            // A capture that cannot reach alpha even with its victim and a margin is not searched.
            if (options_.delta_pruning && !in_check && list[i].is_capture() &&
                !list[i].is_promotion() && pos.has_non_pawn_material() &&
                best + kPieceValues[victim_type(pos, list[i])] + options_.delta_margin < alpha) {
                continue;
            }

            // A capture that loses material by the exchange on its square is not searched.
            if (options_.see && !in_check && list[i].is_capture() && !list[i].is_promotion() &&
                see(pos, list[i]) < 0) {
                continue;
            }

            Position next = pos;

            if (!next.make_move(list[i])) {
                continue;
            }

            ++legal_moves;
            ++searched;
            keys_.push_back(next.key());
            const int score = -quiescence(next, -beta, -alpha, ply + 1, trace_id, list[i]);
            keys_.pop_back();

            if (aborted_) {
                return 0;
            }
            if (score > best) {
                best = score;

                if (score > alpha) {
                    alpha = score;

                    if (alpha >= beta) {
                        cutoff = true;
                        break;
                    }
                }
            }
        }

        if (in_check && legal_moves == 0) {
            best = -(kMateScore - ply);
        }

        trace_leave(trace_id, best, list.size(), searched, cutoff, false);
        return best;
    }

}  // namespace chess
