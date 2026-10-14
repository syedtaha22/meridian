// Plays two search configurations against each other and reports the score.
//
// Each opening is played twice with the colors swapped, so that neither side gains from
// the opening or from moving first. Openings are random legal moves from the initial
// position, from a fixed seed, so a match can be repeated.
//
// Usage: play_match [--a full] [--b iterative] [--depth-a 64] [--depth-b 64]
//                   [--time-a 50] [--time-b 50] [--openings 50] [--random-plies 8]
//                   [--threads 8] [--cpu-base 0] [--seed 1] [--hash 16]
//                   [--hash-a 16] [--hash-b 16] [--book-a] [--book-b]
//                   [--eval-a FILE] [--eval-b FILE]
//
// Each side is limited by `--time-*` milliseconds per move and, when that is zero, by
// `--depth-*` plies.
//
// `--eval-a` and `--eval-b` give a side the evaluation numbers of a file written by
// `texel_tune`, in place of the evaluation of the engine.
//
// A side with `--book-a` or `--book-b` plays the built-in opening book while the game is in it.
// With `--random-plies 0` the games start from the initial position, and the two games of a
// pair share the random numbers that choose book moves.
//
// With `--sprt` the match stops as soon as a sequential probability ratio test decides between
// "the first configuration is `--elo0` stronger" (H0) and "it is `--elo1` stronger" (H1), with
// error probabilities `--alpha` and `--beta`. The match then has at most `--openings` pairs.
// Games are counted in the order they were started, and only whole pairs, so that a fast game
// cannot change the decision by finishing first. No decision is made before `--min-pairs`
// pairs.
//
// Usage of the test: play_match --a full --b ordering --sprt --elo0 0 --elo1 20 --openings 200

#include <atomic>
#include <cmath>
#include <iostream>
#include <memory>
#include <mutex>
#include <set>
#include <thread>

#include "../bench/common.hpp"
#include "book.hpp"
#include "movegen.hpp"
#include "search.hpp"
#include "sprt.hpp"

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
        bool book = false;                       ///< Play book moves while the game is in the book.
        std::shared_ptr<const EvalParams> eval;  ///< Tuned evaluation, or null for the default.
    };

    /**
     * The result of a game.
     */
    enum class Outcome { kWhiteWins, kBlackWins, kDraw };

    /**
     * Plays one game; returns the outcome from White's side.
     */
    Outcome play_game(const Position &start, const Side &white, const Side &black,
                      std::vector<uint64_t> history, uint64_t random) {
        auto make = [](const Side &side) {
            SearchOptions options = SearchOptions::preset(side.config, side.depth);
            options.time_limit_ms = side.time_ms;
            options.hash_mb = side.hash_mb;
            options.eval_params = side.eval;
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

            const Side &side = pos.side_to_move() == kWhite ? white : black;
            Move move;
            random = random * 6364136223846793005ULL + 1442695040888963407ULL;

            if (side.book) {
                const auto choice = Book::builtin().pick(pos, random >> 11);

                if (choice) {
                    move = choice->move;
                }
            }

            if (move.is_null()) {
                Searcher &searcher = pos.side_to_move() == kWhite ? white_searcher : black_searcher;
                move = searcher.search(pos, history).best_move;
            }

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

    const int hash = args.get_int("hash", 16);
    a.book = args.has("book-a");
    auto load_eval = [&args](const char *name) -> std::shared_ptr<const EvalParams> {
        const std::string path = args.get(name, "");

        if (path.empty()) {
            return nullptr;
        }

        auto params = std::make_shared<EvalParams>(EvalParams::defaults());

        if (!params->load(path)) {
            std::cerr << "cannot read evaluation numbers from " << path << "\n";
            std::exit(1);
        }
        return params;
    };
    a.eval = load_eval("eval-a");
    b.eval = load_eval("eval-b");
    b.book = args.has("book-b");
    a.hash_mb = static_cast<size_t>(args.get_int("hash-a", hash));
    b.hash_mb = static_cast<size_t>(args.get_int("hash-b", hash));

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
        uint64_t random;  ///< Chooses book moves; the two games of a pair share it.
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
        // With no random plies every pair starts from the initial position.
        if (random_plies > 0 && !seen.insert(pos.to_fen()).second) {
            continue;
        }

        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        jobs.push_back({pos, history, true, state});
        jobs.push_back({pos, history, false, state});
    }

    const bool use_sprt = args.has("sprt");
    const double elo0 = std::stod(args.get("elo0", "0"));
    const double elo1 = std::stod(args.get("elo1", "20"));
    const double alpha = std::stod(args.get("alpha", "0.05"));
    const double beta = std::stod(args.get("beta", "0.05"));
    const int min_pairs = args.get_int("min-pairs", 30);

    // The result of every game from the side of the first configuration: 0 loss, 1 draw, 2 win.
    // A game is counted when all games before it are done, so the order of counting is the order
    // of starting.
    std::vector<int> results(jobs.size(), -1);
    size_t counted = 0;
    int a_wins = 0;
    int b_wins = 0;
    int draws = 0;
    sprt::PairCounts pair_counts;
    sprt::Decision decision = sprt::Decision::kContinue;
    double ratio = 0;

    std::atomic<size_t> next{0};
    std::atomic<bool> stop{false};
    std::mutex lock;

    auto worker = [&](int id) {
        if (cpu_base >= 0) {
            bench::pin_to_cpu(cpu_base + id);
        }

        while (!stop.load()) {
            const size_t i = next.fetch_add(1);

            if (i >= jobs.size()) {
                return;
            }

            const Job &job = jobs[i];
            const Outcome outcome = job.a_is_white
                                        ? play_game(job.start, a, b, job.history, job.random)
                                        : play_game(job.start, b, a, job.history, job.random);
            int result = 1;

            if (outcome != Outcome::kDraw) {
                result = (outcome == Outcome::kWhiteWins) == job.a_is_white ? 2 : 0;
            }

            std::lock_guard<std::mutex> guard(lock);
            results[i] = result;

            // Count the games that are complete in order, and decide only after a whole pair.
            while (counted < results.size() && results[counted] >= 0 && !stop.load()) {
                const int r = results[counted];
                a_wins += r == 2;
                draws += r == 1;
                b_wins += r == 0;
                ++counted;

                if (counted % 2 == 0) {
                    pair_counts.add(results[counted - 2], results[counted - 1]);

                    if (use_sprt && static_cast<int>(counted / 2) >= min_pairs) {
                        ratio = sprt::llr(pair_counts, elo0, elo1);
                        decision = sprt::decide(ratio, alpha, beta);

                        if (decision != sprt::Decision::kContinue) {
                            stop.store(true);
                        }
                    }
                }
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
    auto elo = [](double s) {
        s = std::clamp(s, 0.0005, 0.9995);
        return -400 * std::log10(1 / s - 1);
    };

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

    if (use_sprt) {
        ratio = sprt::llr(pair_counts, elo0, elo1);
        const char *verdict = "no decision";

        if (decision == sprt::Decision::kAcceptH1) {
            verdict = "H1 accepted: the first configuration is stronger";
        } else if (decision == sprt::Decision::kAcceptH0) {
            verdict = "H0 accepted: not stronger by the margin";
        }

        std::printf("SPRT [%+.0f, %+.0f] Elo, alpha %.2f, beta %.2f: LLR %.2f (bounds %.2f, "
                    "%.2f) after %d games: %s\n",
                    elo0, elo1, alpha, beta, ratio, std::log(beta / (1 - alpha)),
                    std::log((1 - beta) / alpha), games, verdict);
    }

    return 0;
}
