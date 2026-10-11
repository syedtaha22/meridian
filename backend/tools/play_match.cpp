// Plays two search configurations against each other and reports the score.
//
// Each opening is played twice with the colors swapped, so that neither side gains from
// the opening or from moving first. Openings are random legal moves from the initial
// position, from a fixed seed, so a match can be repeated.
//
// Usage: play_match [--a full] [--b iterative] [--depth-a 64] [--depth-b 64]
//                   [--time-a 50] [--time-b 50] [--openings 50] [--random-plies 8]
//                   [--threads 8] [--cpu-base 0] [--seed 1] [--hash 16]
//
// Each side is limited by `--time-*` milliseconds per move and, when that is zero, by
// `--depth-*` plies.

#include <atomic>
#include <cmath>
#include <iostream>
#include <mutex>
#include <set>
#include <thread>

#include "../bench/common.hpp"
#include "chess/movegen.hpp"
#include "chess/search.hpp"

namespace {

    using namespace chess;

    /**
     * One configuration of the engine and its limits.
     */
    struct Side {
        std::string config;
        int depth = 64;
        int64_t time_ms = 50;
        size_t hash_mb = 16;
    };

    /**
     * The result of a game.
     */
    enum class Outcome { kWhiteWins, kBlackWins, kDraw };

    /**
     * Plays one game; returns the outcome from White's side.
     */
    Outcome play_game(const Position &start, const Side &white, const Side &black,
                      std::vector<uint64_t> history) {
        auto make = [](const Side &side) {
            SearchOptions options = SearchOptions::preset(side.config, side.depth);
            options.time_limit_ms = side.time_ms;
            options.hash_mb = side.hash_mb;
            return Searcher(options);
        };

        Searcher white_searcher = make(white);
        Searcher black_searcher = make(black);

        Position pos = start;

        for (int ply = 0; ply < 400; ++ply) {
            MoveList legal;
            generate_legal(pos, legal);

            if (legal.empty()) {
                if (!pos.in_check()) {
                    return Outcome::kDraw;
                }
                return pos.side_to_move() == kWhite ? Outcome::kBlackWins : Outcome::kWhiteWins;
            }

            if (pos.halfmove_clock() >= 100 || pos.insufficient_material()) {
                return Outcome::kDraw;
            }

            int repeats = 0;

            for (uint64_t key : history) {
                repeats += key == pos.key();
            }
            if (repeats >= 2) {
                return Outcome::kDraw;
            }

            Searcher &searcher = pos.side_to_move() == kWhite ? white_searcher : black_searcher;
            const SearchResult result = searcher.search(pos, history);
            Move move = result.best_move;

            if (move.is_null()) {
                move = legal[0];
            }

            history.push_back(pos.key());
            pos.make_move(move);
        }

        return Outcome::kDraw;
    }

    /**
     * Plays `plies` random legal moves from the initial position.
     */
    bool random_opening(uint64_t &state, int plies, Position &pos, std::vector<uint64_t> &history) {
        pos = Position();
        history.clear();

        for (int i = 0; i < plies; ++i) {
            MoveList legal;
            generate_legal(pos, legal);

            if (legal.empty()) {
                return false;
            }

            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            history.push_back(pos.key());
            pos.make_move(legal[static_cast<int>(state % static_cast<uint64_t>(legal.size()))]);
        }

        MoveList legal;
        generate_legal(pos, legal);
        return !legal.empty();
    }

}  // namespace

int main(int argc, char **argv) {
    const bench::Args args(argc, argv);

    Side a;
    Side b;

    a.config = args.get("a", "full");
    b.config = args.get("b", "iterative");

    a.depth = args.get_int("depth-a", 64);
    b.depth = args.get_int("depth-b", 64);

    a.time_ms = args.get_int("time-a", 50);
    b.time_ms = args.get_int("time-b", 50);

    a.hash_mb = b.hash_mb = static_cast<size_t>(args.get_int("hash", 16));

    const int openings = args.get_int("openings", 50);
    const int random_plies = args.get_int("random-plies", 8);

    const int threads = std::max(1, args.get_int("threads", 8));
    const int cpu_base = args.get_int("cpu-base", -1);

    uint64_t seed = 0x9E3779B97F4A7C15ULL * static_cast<uint64_t>(args.get_int("seed", 1));

    /**
     * One game to play: the opening and which side the first configuration has.
     */
    struct Job {
        Position start;
        std::vector<uint64_t> history;
        bool a_is_white;
    };

    std::vector<Job> jobs;
    std::set<std::string> seen;
    uint64_t state = seed | 1;

    while (static_cast<int>(jobs.size()) < openings * 2) {
        Position pos;
        std::vector<uint64_t> history;

        if (!random_opening(state, random_plies, pos, history)) {
            continue;
        }
        if (!seen.insert(pos.to_fen()).second) {
            continue;
        }

        jobs.push_back({pos, history, true});
        jobs.push_back({pos, history, false});
    }

    std::atomic<size_t> next{0};
    std::mutex lock;
    int a_wins = 0;
    int b_wins = 0;
    int draws = 0;

    auto worker = [&](int id) {
        if (cpu_base >= 0) {
            bench::pin_to_cpu(cpu_base + id);
        }
        while (true) {
            const size_t i = next.fetch_add(1);

            if (i >= jobs.size()) {
                return;
            }

            const Job &job = jobs[i];
            const Outcome outcome = job.a_is_white ? play_game(job.start, a, b, job.history)
                                                   : play_game(job.start, b, a, job.history);
            std::lock_guard<std::mutex> guard(lock);

            if (outcome == Outcome::kDraw) {
                ++draws;
            } else if ((outcome == Outcome::kWhiteWins) == job.a_is_white) {
                ++a_wins;
            } else {
                ++b_wins;
            }
        }
    };

    std::vector<std::thread> pool;

    for (int t = 0; t < threads; ++t) {
        pool.emplace_back(worker, t);
    }

    for (auto &t : pool) {
        t.join();
    }

    const int games = a_wins + b_wins + draws;
    const double score = (a_wins + 0.5 * draws) / games;

    // Normal approximation of the mean game score, with the draw-aware variance.
    double variance = 0;
    variance += a_wins * (1 - score) * (1 - score);
    variance += draws * (0.5 - score) * (0.5 - score);
    variance += b_wins * score * score;
    variance /= games;

    const double margin = 1.96 * std::sqrt(variance / games);
    auto elo = [](double s) { return s <= 0 || s >= 1 ? 0.0 : -400 * std::log10(1 / s - 1); };

    std::printf("%s (%s) vs %s (%s)\n", a.config.c_str(),
                a.time_ms > 0 ? (std::to_string(a.time_ms) + "ms").c_str()
                              : ("depth " + std::to_string(a.depth)).c_str(),
                b.config.c_str(),
                b.time_ms > 0 ? (std::to_string(b.time_ms) + "ms").c_str()
                              : ("depth " + std::to_string(b.depth)).c_str());
    std::printf("games %d: +%d =%d -%d  score %.3f  (95%% interval %.3f to %.3f)  Elo %+.0f "
                "(%+.0f to %+.0f)\n",
                games, a_wins, draws, b_wins, score, score - margin, score + margin, elo(score),
                elo(std::max(0.0001, score - margin)), elo(std::min(0.9999, score + margin)));
    return 0;
}
