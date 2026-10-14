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

// Plays one engine against another and estimates a rating.
//
// The candidate is this engine (a search preset, limited by time or depth) or an external
// engine given with --candidate-engine. The opponent is an external UCI engine such as
// Stockfish, limited in strength with UCI_LimitStrength and UCI_Elo (one level per --elo
// value), or one of this engine's presets with --opponent-config.
//
// Each opening is played twice with the colors swapped. Openings are random legal moves from
// the initial position, from a fixed seed, so a run can be repeated. A move that is not legal
// loses the game for the side that played it, and is counted.
//
// For each level the score and its Elo difference are printed. When the opponent is a UCI
// engine with an Elo scale, the rating estimate combines all levels: it is the rating at
// which the expected scores, from the logistic model, add up to the scores observed.
//
// An external candidate that is given only the current position (--candidate-stateless) is
// sent `position fen` with the position to move from and `go depth N`. This is the way the
// original desktop engine is played.
//
// `--eval FILE` gives this engine the evaluation numbers of a file written by `texel_tune`, and
// `--nnue FILE` a network written by `train_nnue`.
//
// Usage: uci_match [--engine PATH --elo 1500,1800,2100]
//                  [--opponent-config NAME --opponent-depth N --opponent-time MS]
//                  [--config full --depth N | --candidate-engine PATH --candidate-depth N
//                   --candidate-stateless] [--time 100] [--openings 25] [--random-plies 6]
//                  [--threads 8] [--cpu-base -1] [--seed 1] [--hash 16] [--opponent-hash 16]

#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <memory>
#include <mutex>
#include <set>
#include <thread>

#include "../bench/common.hpp"
#include "movegen.hpp"
#include "search.hpp"

namespace {

    using namespace chess;

    /**
     * Splits "a,b,c" into its parts, dropping empty ones.
     */
    std::vector<std::string> split(const std::string &text, char separator) {
        std::vector<std::string> parts;
        std::string part;
        std::istringstream in(text);

        while (std::getline(in, part, separator)) {
            if (!part.empty()) {
                parts.push_back(part);
            }
        }

        return parts;
    }

    /**
     * An engine in a child process, spoken to over pipes in UCI.
     */
    class UciEngine {
    public:
        /**
         * Starts `path`; `argument`, when not empty, is its one command line argument.
         */
        explicit UciEngine(const std::string &path, const std::string &argument = "") {
            int to_child[2];
            int from_child[2];

            if (pipe(to_child) != 0 || pipe(from_child) != 0) {
                throw std::runtime_error("pipe failed");
            }

            pid_ = fork();

            if (pid_ == 0) {
                // No engine may take more than 2 GiB of address space, so that one that
                // leaks cannot starve the machine.
                const rlim_t limit = static_cast<rlim_t>(2) << 30;
                const struct rlimit memory = {limit, limit};
                setrlimit(RLIMIT_AS, &memory);
                dup2(to_child[0], STDIN_FILENO);
                dup2(from_child[1], STDOUT_FILENO);
                close(to_child[0]);
                close(to_child[1]);
                close(from_child[0]);
                close(from_child[1]);
                execl(path.c_str(), path.c_str(), argument.empty() ? nullptr : argument.c_str(),
                      static_cast<char *>(nullptr));
                _exit(127);
            }

            close(to_child[0]);
            close(from_child[1]);
            write_ = fdopen(to_child[1], "w");
            read_ = fdopen(from_child[0], "r");

            if (pid_ < 0 || write_ == nullptr || read_ == nullptr) {
                throw std::runtime_error("could not start " + path);
            }
        }

        UciEngine(const UciEngine &) = delete;
        UciEngine &operator=(const UciEngine &) = delete;

        ~UciEngine() {
            if (write_ != nullptr) {
                send("quit");
                fclose(write_);
            }
            if (read_ != nullptr) {
                fclose(read_);
            }
            if (pid_ > 0) {
                int status = 0;
                waitpid(pid_, &status, 0);
            }
        }

        /**
         * Writes one line to the engine.
         */
        void send(const std::string &line) {
            fputs((line + "\n").c_str(), write_);
            fflush(write_);
        }

        /**
         * Reads lines until one starts with `prefix`, and returns the lines read.
         */
        std::vector<std::string> read_until(const std::string &prefix) {
            std::vector<std::string> lines;
            char buffer[4096];

            while (fgets(buffer, sizeof(buffer), read_) != nullptr) {
                std::string line = buffer;

                while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
                    line.pop_back();
                }

                lines.push_back(line);

                if (line.rfind(prefix, 0) == 0) {
                    return lines;
                }
            }

            throw std::runtime_error("the engine closed its output");
        }

        /**
         * Handshake. Returns the smallest and largest UCI_Elo the engine accepts, or 0, 0.
         */
        std::pair<int, int> start(size_t hash_mb) {
            send("uci");
            int low = 0;
            int high = 0;

            for (const std::string &line : read_until("uciok")) {
                if (line.find("option name UCI_Elo ") != std::string::npos) {
                    const auto fields = split(line, ' ');

                    for (size_t i = 0; i + 1 < fields.size(); ++i) {
                        if (fields[i] == "min") {
                            low = std::stoi(fields[i + 1]);
                        } else if (fields[i] == "max") {
                            high = std::stoi(fields[i + 1]);
                        }
                    }
                }
            }

            if (hash_mb > 0) {
                send("setoption name Threads value 1");
                send("setoption name Hash value " + std::to_string(hash_mb));
            }
            return {low, high};
        }

        /**
         * Waits until the engine answers "isready".
         */
        void ready() {
            send("isready");
            read_until("readyok");
        }

        /**
         * Limits the engine to the given UCI_Elo.
         */
        void limit_strength(int elo) {
            send("setoption name UCI_LimitStrength value true");
            send("setoption name UCI_Elo value " + std::to_string(elo));
            ready();
        }

        /**
         * Tells the engine that a new game starts.
         */
        void new_game() {
            send("ucinewgame");
            ready();
        }

        /**
         * The move the engine plays, in UCI notation, for the position and limit given.
         */
        std::string best_move(const std::string &position_command, const std::string &go) {
            send(position_command);
            send(go);
            const auto lines = read_until("bestmove");
            const auto fields = split(lines.back(), ' ');

            return fields.size() > 1 ? fields[1] : "";
        }

    private:
        pid_t pid_ = -1;
        FILE *write_ = nullptr;
        FILE *read_ = nullptr;
    };

    /**
     * How a player is made and limited.
     */
    struct PlayerSpec {
        std::string label;

        // This engine, by preset; otherwise the external engine at `path`.
        bool internal = true;
        std::string config = "full";
        std::string path;
        int depth = 0;        // plies; used when `time_ms` is 0
        int64_t time_ms = 0;  // milliseconds per move
        size_t hash_mb = 16;
        int elo = 0;             // UCI_Elo, when positive
        bool stateless = false;  // is sent the position to move from and not the game so far
        std::shared_ptr<const EvalParams> eval;     // tuned evaluation of this engine, or null
        std::shared_ptr<const nnue::Network> nnue;  // a network of this engine, or null
    };

    /**
     * One side of a game.
     */
    class Player {
    public:
        explicit Player(const PlayerSpec &spec) : spec_(spec) {
            if (spec.internal) {
                SearchOptions options =
                    SearchOptions::preset(spec.config, spec.depth > 0 ? spec.depth : 64);
                options.time_limit_ms = spec.time_ms;
                options.hash_mb = spec.hash_mb;
                options.eval_params = spec.eval;
                options.nnue = spec.nnue;
                searcher_ = std::make_unique<Searcher>(options);
            } else if (!spec.stateless) {
                engine_ = std::make_unique<UciEngine>(spec.path);
                engine_->start(spec.hash_mb);

                if (spec.elo > 0) {
                    engine_->limit_strength(spec.elo);
                }

                engine_->new_game();
            }
        }

        /**
         * The move to play. `start_fen` and `played` describe the game from the position it
         * was begun in, `history` holds the keys of the positions before `pos`.
         */
        Move choose(const Position &pos, const std::vector<uint64_t> &history,
                    const std::string &start_fen, const std::vector<std::string> &played) {
            if (searcher_) {
                return searcher_->search(pos, history).best_move;
            }

            std::string command = "position fen ";

            if (spec_.stateless) {
                command += pos.to_fen();
            } else {
                command += start_fen;

                if (!played.empty()) {
                    command += " moves";

                    for (const auto &m : played) {
                        command += " " + m;
                    }
                }
            }

            const std::string go = spec_.time_ms > 0
                                       ? "go movetime " + std::to_string(spec_.time_ms)
                                       : "go depth " + std::to_string(spec_.depth);
            std::string text;

            if (spec_.stateless) {
                // The process is started for this move alone: the original engine does not
                // free the boards it copies, so one process kept for a game grows to
                // gigabytes.
                UciEngine once(spec_.path, "play");
                text = once.best_move(command, go);
            } else {
                text = engine_->best_move(command, go);
            }

            Move move = move_from_uci(pos, text);

            if (move.is_null() && text.size() == 4) {
                // A pawn that reaches the last rank without a piece named becomes a queen.
                move = move_from_uci(pos, text + "q");
            }
            return move;
        }

    private:
        PlayerSpec spec_;
        std::unique_ptr<Searcher> searcher_;
        std::unique_ptr<UciEngine> engine_;
    };

    /**
     * The result of a game.
     */
    enum class Outcome { kWhiteWins, kBlackWins, kDraw };

    /**
     * The outcome of a game and the illegal moves played in it.
     */
    struct Result {
        Outcome outcome = Outcome::kDraw;
        bool candidate_illegal = false;
        bool opponent_illegal = false;
    };

    /**
     * Plays one game. Returns the outcome from White's side.
     */
    Result play_game(const PlayerSpec &candidate_spec, const PlayerSpec &opponent_spec,
                     const Position &start, std::vector<uint64_t> history, bool candidate_white) {
        Player candidate(candidate_spec);
        Player opponent(opponent_spec);

        const std::string start_fen = start.to_fen();
        std::vector<std::string> played;
        Position pos = start;
        Result result;

        for (int ply = 0; ply < 400; ++ply) {
            MoveList legal;
            generate_legal(pos, legal);

            if (legal.empty()) {
                if (!pos.in_check()) {
                    return result;
                }

                result.outcome =
                    pos.side_to_move() == kWhite ? Outcome::kBlackWins : Outcome::kWhiteWins;
                return result;
            }

            if (pos.halfmove_clock() >= 100 || pos.insufficient_material()) {
                return result;
            }

            int repeats = 0;

            for (uint64_t key : history) {
                repeats += key == pos.key();
            }
            if (repeats >= 2) {
                return result;
            }

            const bool candidate_moves = (pos.side_to_move() == kWhite) == candidate_white;
            Player &mover = candidate_moves ? candidate : opponent;
            Move move = mover.choose(pos, history, start_fen, played);

            if (move.is_null()) {
                // Not a legal move: the side that played it loses.
                (candidate_moves ? result.candidate_illegal : result.opponent_illegal) = true;
                result.outcome = (candidate_moves == candidate_white) ? Outcome::kBlackWins
                                                                      : Outcome::kWhiteWins;
                return result;
            }

            played.push_back(move_to_uci(move));
            history.push_back(pos.key());
            pos.make_move(move);
        }

        return result;
    }

    /**
     * Plays `plies` random legal moves from the initial position.
     *
     * @return False when the position reached has no legal move.
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

    /**
     * The games played against one Elo setting of the opponent.
     */
    struct Level {
        int elo = 0;
        int wins = 0;
        int draws = 0;
        int losses = 0;
        int candidate_illegal = 0;
        int opponent_illegal = 0;

        int games() const {
            return wins + draws + losses;
        }

        double score() const {
            return games() == 0 ? 0.5 : (wins + 0.5 * draws) / games();
        }
    };

    /**
     * The Elo difference that gives the score, with the score limited to 0.05% to 99.95%.
     */
    double elo_of(double score) {
        score = std::clamp(score, 0.0005, 0.9995);
        return -400 * std::log10(1 / score - 1);
    }

    /**
     * The expected score of `rating` against `opponent` on the logistic scale.
     */
    double expected(double rating, double opponent) {
        return 1 / (1 + std::pow(10, (opponent - rating) / 400));
    }

    /**
     * The player and its limit as text.
     */
    std::string describe(const PlayerSpec &spec) {
        std::string limit = spec.time_ms > 0 ? std::to_string(spec.time_ms) + "ms per move"
                                             : "depth " + std::to_string(spec.depth);
        return spec.label + " (" + limit + ")";
    }

}  // namespace

int main(int argc, char **argv) {
    const bench::Args args(argc, argv);

    PlayerSpec candidate;
    const std::string candidate_path = args.get("candidate-engine", "");
    candidate.internal = candidate_path.empty();
    candidate.path = candidate_path;
    candidate.config = args.get("config", "full");
    candidate.stateless = args.has("candidate-stateless");

    candidate.time_ms = args.get_int("time", 100);
    candidate.depth = args.get_int("depth", 0);

    if (!candidate_path.empty()) {
        candidate.label = args.get("candidate-label", "external engine");
        candidate.depth = args.get_int("candidate-depth", candidate.depth);

        if (candidate.depth > 0) {
            candidate.time_ms = 0;
        }
    } else {
        candidate.label = candidate.config;

        if (candidate.depth > 0) {
            candidate.time_ms = 0;
        }
    }

    candidate.hash_mb = static_cast<size_t>(args.get_int("hash", 16));

    if (args.has("eval")) {
        auto params = std::make_shared<EvalParams>(EvalParams::defaults());

        if (!params->load(args.get("eval", ""))) {
            std::cerr << "cannot read evaluation numbers\n";
            return 1;
        }
        candidate.eval = params;
    }

    if (args.has("nnue")) {
        candidate.nnue = nnue::load(args.get("nnue", ""));

        if (!candidate.nnue) {
            std::cerr << "cannot read the network\n";
            return 1;
        }
    }

    PlayerSpec base;
    const bool opponent_internal = args.has("opponent-config");
    const std::string engine_path = args.get("engine", "");

    if (!opponent_internal && engine_path.empty()) {
        std::cerr << "usage: uci_match --engine PATH [--elo 1500,1800] | --opponent-config NAME\n"
                     "                 [--config NAME | --candidate-engine PATH] [--time MS]\n"
                     "                 [--depth N] [--openings N] [--random-plies N]\n"
                     "                 [--threads N] [--cpu-base CPU] [--seed N] [--hash MB]\n";
        return 1;
    }

    base.internal = opponent_internal;
    base.path = engine_path;
    base.config = args.get("opponent-config", "full");

    base.depth = args.get_int("opponent-depth", 0);
    base.time_ms = base.depth > 0 ? 0 : args.get_int("opponent-time", args.get_int("time", 100));

    base.hash_mb = static_cast<size_t>(args.get_int("opponent-hash", 16));
    base.label = opponent_internal ? base.config : "UCI engine";

    int minimum = 0;
    int maximum = 0;

    if (!opponent_internal) {
        UciEngine probe(engine_path);
        std::tie(minimum, maximum) = probe.start(16);
    }

    std::vector<Level> levels;

    if (opponent_internal) {
        levels.push_back(Level{});
    } else {
        for (const std::string &text : split(args.get("elo", "1500,1800,2100"), ',')) {
            Level level;
            level.elo = std::stoi(text);

            if (maximum > 0 && (level.elo < minimum || level.elo > maximum)) {
                std::cerr << "UCI_Elo " << level.elo << " is outside " << minimum << " to "
                          << maximum << "\n";
                return 1;
            }

            levels.push_back(level);
        }
    }

    const int openings = args.get_int("openings", 25);
    const int random_plies = args.get_int("random-plies", 6);

    const int threads = std::max(1, args.get_int("threads", 8));
    const int cpu_base = args.get_int("cpu-base", -1);

    uint64_t state = (0x9E3779B97F4A7C15ULL * static_cast<uint64_t>(args.get_int("seed", 1))) | 1;

    struct Job {
        size_t level;
        Position start;
        std::vector<uint64_t> history;
        bool candidate_white;
    };

    std::vector<Job> jobs;
    std::set<std::string> seen;
    std::vector<std::pair<Position, std::vector<uint64_t>>> starts;

    while (static_cast<int>(starts.size()) < openings) {
        Position pos;
        std::vector<uint64_t> history;

        if (random_opening(state, random_plies, pos, history) && seen.insert(pos.to_fen()).second) {
            starts.push_back({pos, history});
        }
    }

    for (size_t level = 0; level < levels.size(); ++level) {
        for (const auto &[pos, history] : starts) {
            jobs.push_back({level, pos, history, true});
            jobs.push_back({level, pos, history, false});
        }
    }

    std::atomic<size_t> next{0};
    std::mutex lock;
    size_t finished = 0;

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
            PlayerSpec opponent = base;
            opponent.elo = levels[job.level].elo;
            Result result;
            try {
                result =
                    play_game(candidate, opponent, job.start, job.history, job.candidate_white);
            } catch (const std::exception &error) {
                std::lock_guard<std::mutex> guard(lock);
                std::cerr << "game failed: " << error.what() << "\n";
            }

            std::lock_guard<std::mutex> guard(lock);
            Level &level = levels[job.level];

            if (result.outcome == Outcome::kDraw) {
                ++level.draws;
            } else if ((result.outcome == Outcome::kWhiteWins) == job.candidate_white) {
                ++level.wins;
            } else {
                ++level.losses;
            }

            level.candidate_illegal += result.candidate_illegal;
            level.opponent_illegal += result.opponent_illegal;
            ++finished;

            if (finished % 10 == 0) {
                std::cerr << finished << " of " << jobs.size() << " games\n";
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

    base.elo = 0;
    std::printf("%s against %s\n", describe(candidate).c_str(), describe(base).c_str());
    std::printf("%-9s %6s %6s %6s %6s %8s %8s %24s\n", "opponent", "games", "wins", "draws",
                "losses", "score", "illegal", "rating from this level");
    double total_games = 0;

    for (const Level &level : levels) {
        const double s = level.score();
        const double n = level.games();

        double variance = 0;
        variance += level.wins * (1 - s) * (1 - s);
        variance += level.draws * (0.5 - s) * (0.5 - s);
        variance += level.losses * s * s;

        const double margin = n > 0 ? 1.96 * std::sqrt(variance / n / n) : 0;
        const std::string name = opponent_internal ? "preset" : std::to_string(level.elo);
        std::printf("%-9s %6d %6d %6d %6d %7.3f %4d/%-3d ", name.c_str(), level.games(), level.wins,
                    level.draws, level.losses, s, level.candidate_illegal, level.opponent_illegal);

        if (opponent_internal) {
            std::printf("Elo %+.0f (%+.0f to %+.0f)\n", elo_of(s), elo_of(s - margin),
                        elo_of(s + margin));
        } else {
            std::printf("%12.0f (%.0f to %.0f)\n", level.elo + elo_of(s),
                        level.elo + elo_of(s - margin), level.elo + elo_of(s + margin));
        }

        total_games += n;
    }

    std::printf("illegal moves are counted as candidate/opponent, per level\n");

    if (opponent_internal) {
        return 0;
    }

    // The rating at which the expected scores equal the observed scores, found by bisection,
    // and its standard error from the information of the logistic model.
    double low = -1000;
    double high = 4500;

    for (int i = 0; i < 80; ++i) {
        const double mid = (low + high) / 2;
        double residual = 0;

        for (const Level &level : levels) {
            residual += level.games() * (level.score() - expected(mid, level.elo));
        }

        (residual > 0 ? low : high) = mid;
    }

    const double rating = (low + high) / 2;
    double information = 0;

    for (const Level &level : levels) {
        const double p = expected(rating, level.elo);
        information += level.games() * p * (1 - p);
    }

    const double scale = std::log(10) / 400;
    const double error = information > 0 ? 1.96 / (scale * std::sqrt(information)) : 0;
    std::printf("\nEstimated rating: %.0f (95%% interval %.0f to %.0f) over %.0f games\n", rating,
                rating - error, rating + error, total_games);
    return 0;
}
