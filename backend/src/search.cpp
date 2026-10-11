#include "chess/search.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>

#include "chess/eval.hpp"
#include "chess/movegen.hpp"

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
        constexpr int kHistoryLimit = 700000;

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

        int mvv_lva(const Position &pos, Move move) {
            const int victim = move.is_capture() ? static_cast<int>(victim_type(pos, move)) : 0;
            const int attacker = piece_type(pos.piece_on(move.from()));
            return kCaptureScore + victim * 16 - attacker;
        }

    }  // namespace

    SearchOptions SearchOptions::preset(std::string_view name, int max_depth) {
        SearchOptions o;
        o.max_depth = max_depth;
        o.move_ordering = false;
        o.killers_history = false;
        o.transposition_table = false;
        o.quiescence = false;
        o.iterative_deepening = false;
        o.pvs = false;
        o.null_move = false;
        o.lmr = false;
        o.check_extension = false;

        if (name == "baseline") {
            return o;
        }

        o.move_ordering = true;

        if (name == "ordering") {
            return o;
        }

        o.transposition_table = true;

        if (name == "tt") {
            return o;
        }

        o.quiescence = true;

        if (name == "quiescence") {
            return o;
        }

        o.iterative_deepening = true;
        o.pvs = true;

        if (name == "iterative") {
            return o;
        }

        o.killers_history = true;
        o.null_move = true;
        o.lmr = true;
        o.check_extension = true;

        return o;
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

    Searcher::Searcher(SearchOptions options) : options_(options), tt_(options.hash_mb) {
        clear();
    }

    void Searcher::clear() {
        tt_.resize(options_.hash_mb);
        std::memset(killers_, 0, sizeof(killers_));
        std::memset(history_, 0, sizeof(history_));
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
        if (stop_.load(std::memory_order_relaxed)) {
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

    SearchResult Searcher::search(const Position &root, const std::vector<uint64_t> &history) {
        SearchResult result;
        stats_ = SearchStats{};
        stop_.store(false);
        aborted_ = false;
        start_ns_ = now_ns();
        deadline_ns_ =
            options_.time_limit_ms > 0 ? start_ns_ + options_.time_limit_ms * 1000000 : 0;
        if (options_.transposition_table) {
            if (tt_.megabytes() != options_.hash_mb) {
                tt_.resize(options_.hash_mb);
            }

            tt_.new_generation();
        }

        for (auto &row : killers_) {
            row[0] = row[1] = Move();
        }

        keys_ = history;
        keys_.push_back(root.key());

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
            root_best_ = Move();

            // The recording describes the latest iteration only.
            if (trace_ != nullptr) {
                trace_->clear();
            }

            const int score = search_root(root, depth, -kInfinity, kInfinity);

            // A search cut short is only trusted for its first completed iteration or when
            // it found a move before being stopped on the first iteration.
            if (aborted_ && depth > first_depth) {
                break;
            }
            if (!root_best_.is_null()) {
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
        const TTEntry *entry = options_.transposition_table ? tt_.probe(pos.key()) : nullptr;

        for (int i = 0; i < list.size(); ++i) {
            const Move move = list[i];
            int s = 0;

            if (options_.move_ordering) {
                if (!root_order_.empty() && move == root_order_[0]) {
                    s = kHashMoveScore + 1;
                } else if (entry != nullptr && move == entry->move) {
                    s = kHashMoveScore;
                } else if (move.is_capture()) {
                    s = mvv_lva(pos, move);
                } else if (move.is_promotion()) {
                    s = kPromotionScore;
                }
            }

            scores[i] = s;
        }

        int best = -kInfinity;
        int searched = 0;

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
                score =
                    -negamax(next, depth - 1, -alpha - 1, -alpha, 1, false, true, trace_id, move);
                if (score > alpha && !aborted_) {
                    score = -negamax(next, depth - 1, -beta, -alpha, 1, true, true, trace_id, move);
                }
            }

            keys_.pop_back();
            ++searched;

            if (aborted_) {
                break;
            }
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
            const int value = evaluate(pos);
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
            const int value = evaluate(pos);
            trace_leave(trace_id, value, 0, 0, false, false);
            return value;
        }

        const int original_alpha = alpha;
        Move tt_move;

        if (options_.transposition_table) {
            ++stats_.tt_probes;

            if (const TTEntry *entry = tt_.probe(pos.key())) {
                ++stats_.tt_hits;
                tt_move = entry->move;

                if (!pv && entry->depth >= depth) {
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

        // Null move: if passing the turn still scores at least beta, the position is good
        // enough for the side to move that a real move would too.
        if (options_.null_move && allow_null && !pv && !in_check && depth >= 3 &&
            pos.has_non_pawn_material() && evaluate(pos) >= beta) {
            Position next = pos;
            next.make_null_move();
            keys_.push_back(next.key());
            const int reduction = 2 + depth / 6;
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

        MoveList list;
        generate_pseudo_legal(pos, list);
        int scores[kMaxMoves];
        const Color us = pos.side_to_move();

        for (int i = 0; i < list.size(); ++i) {
            const Move move = list[i];
            int s = 0;

            if (options_.move_ordering) {
                if (!tt_move.is_null() && move == tt_move) {
                    s = kHashMoveScore;
                } else if (move.is_capture()) {
                    s = mvv_lva(pos, move);
                } else if (move.is_promotion()) {
                    s = kPromotionScore;
                } else if (options_.killers_history) {
                    if (move == killers_[ply][0]) {
                        s = kKillerScore;
                    } else if (move == killers_[ply][1]) {
                        s = kKillerScore - 1;
                    } else {
                        s = std::min(history_[us][move.from()][move.to()], kHistoryLimit);
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

            ++legal_moves;
            keys_.push_back(next.key());

            const bool quiet = !move.is_capture() && !move.is_promotion();
            int score;

            if (searched == 0 || !options_.pvs) {
                score = -negamax(next, depth - 1, -beta, -alpha, ply + 1, pv, true, trace_id, move);
            } else {
                int reduction = 0;

                if (options_.lmr && depth >= 3 && searched >= 3 && quiet && !in_check &&
                    !next.in_check()) {
                    reduction = 1 + (searched >= 8 ? 1 : 0);
                    ++stats_.reductions;
                }

                score = -negamax(next, depth - 1 - reduction, -alpha - 1, -alpha, ply + 1, false,
                                 true, trace_id, move);
                if (score > alpha && reduction > 0 && !aborted_) {
                    ++stats_.re_searches;
                    score = -negamax(next, depth - 1, -alpha - 1, -alpha, ply + 1, false, true,
                                     trace_id, move);
                }
                if (score > alpha && score < beta && !aborted_) {
                    ++stats_.re_searches;
                    score = -negamax(next, depth - 1, -beta, -alpha, ply + 1, true, true, trace_id,
                                     move);
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

                            int &h = history_[us][move.from()][move.to()];
                            h = std::min(h + depth * depth, kHistoryLimit);
                        }

                        break;
                    }
                }
            }
        }

        if (legal_moves == 0) {
            best = in_check ? -(kMateScore - ply) : 0;
        }

        if (options_.transposition_table) {
            const Bound bound = best >= beta            ? kBoundLower
                                : best > original_alpha ? kBoundExact
                                                        : kBoundUpper;
            tt_.store(pos.key(), best_move, to_tt_score(best, ply), depth, bound);
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
            const int value = evaluate(pos);
            trace_leave(trace_id, value, 0, 0, false, false);
            return value;
        }

        const bool in_check = pos.in_check();
        int best = -kInfinity;

        if (!in_check) {
            best = evaluate(pos);

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
            scores[i] = list[i].is_capture() ? mvv_lva(pos, list[i]) : kPromotionScore;
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
