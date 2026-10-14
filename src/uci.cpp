#include "uci.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <istream>
#include <ostream>
#include <random>
#include <sstream>

#include "book.hpp"
#include "movegen.hpp"
#include "timeman.hpp"
#include "version.hpp"

namespace chess::uci {

    namespace {

        const char *const kBenchPositions[] = {
#include "bench_positions.inc"
        };

        std::vector<std::string> split(const std::string &line) {
            std::istringstream stream(line);
            std::vector<std::string> tokens;
            std::string token;

            while (stream >> token) {
                tokens.push_back(token);
            }
            return tokens;
        }

        std::string lower(std::string text) {
            std::transform(text.begin(), text.end(), text.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return text;
        }

        /**
         * The number that follows `tokens[at]`, or `fallback` when it is missing or not a number.
         */
        int64_t number_after(const std::vector<std::string> &tokens, size_t at, int64_t fallback) {
            if (at + 1 >= tokens.size()) {
                return fallback;
            }

            char *end = nullptr;
            const long long value = std::strtoll(tokens[at + 1].c_str(), &end, 10);
            return (end != tokens[at + 1].c_str() && *end == '\0') ? value : fallback;
        }

        /**
         * A score as the protocol writes it: centipawns, or moves to mate with a sign.
         */
        std::string score_text(int score) {
            const Score display = to_display_score(score);
            return display.is_mate ? "mate " + std::to_string(display.value)
                                   : "cp " + std::to_string(display.value);
        }

    }  // namespace

    Engine::Engine(std::istream &in, std::ostream &out) : in_(in), out_(out) {
        make_searcher();
    }

    Engine::~Engine() {
        finish_search(true);
    }

    void Engine::send(const std::string &line) {
        std::lock_guard<std::mutex> guard(out_lock_);
        out_ << line << '\n';
        out_.flush();
    }

    void Engine::make_searcher() {
        SearchOptions options = SearchOptions::preset("tuned", 64);
        options.hash_mb = static_cast<size_t>(settings_.hash_mb);
        options.threads = settings_.threads;
        searcher_ = std::make_unique<Searcher>(options);
        searcher_->set_info_callback([this](const IterationInfo &info) {
            std::ostringstream line;
            const double ms = std::max(1.0, info.elapsed_ms);
            line << "info depth " << info.depth << " score " << score_text(info.score) << " nodes "
                 << info.nodes << " nps "
                 << static_cast<uint64_t>(static_cast<double>(info.nodes) * 1000.0 / ms) << " time "
                 << static_cast<int64_t>(info.elapsed_ms) << " pv";

            for (const Move move : info.pv) {
                line << ' ' << move_to_uci(move);
            }
            send(line.str());
        });
    }

    std::string banner() {
        return std::string("Meridian ") + kEngineVersion + " by Syed Taha";
    }

    std::string help() {
        return "Meridian is a UCI chess engine.\n"
               "\n"
               "It reads UCI commands on standard input and writes its replies to standard "
               "output.\n"
               "It is meant to be used through a chess program that speaks the protocol.\n"
               "\n"
               "Usage:\n"
               "  meridian               read UCI commands\n"
               "  meridian bench [N]     search the built-in positions to depth N (default 10)\n"
               "  meridian help          print this text\n"
               "  meridian version       print the version\n"
               "\n"
               "Options (setoption name <name> value <value>):\n"
               "  Hash           1 to 4096 MB, default 64\n"
               "  Threads        1 to 64, default 1\n"
               "  MoveOverhead   0 to 5000 ms, default 30\n"
               "  OwnBook        true or false, default false\n"
               "  Ponder         true or false, default false\n"
               "  Clear Hash     forget the transposition table\n";
    }

    int Engine::run() {
        std::string line;

        while (std::getline(in_, line)) {
            if (!handle(line)) {
                return 0;
            }
        }

        // The input ended. A search with limits is left to end, and an infinite one is stopped.
        finish_search(infinite_);
        return 0;
    }

    void Engine::print_identity() {
        send(std::string("id name Meridian ") + kEngineVersion);
        send("id author Syed Taha");
        send("option name Hash type spin default 64 min 1 max 4096");
        send("option name Threads type spin default 1 min 1 max 64");
        send("option name MoveOverhead type spin default 30 min 0 max 5000");
        send("option name OwnBook type check default false");
        send("option name Ponder type check default false");
        send("option name Clear Hash type button");
        send("uciok");
    }

    bool Engine::handle(const std::string &line) {
        const std::vector<std::string> tokens = split(line);

        if (tokens.empty()) {
            return true;
        }

        const std::string &command = tokens[0];

        if (command == "uci") {
            print_identity();
        } else if (command == "isready") {
            send("readyok");
        } else if (command == "ucinewgame") {
            new_game();
        } else if (command == "setoption") {
            set_option(tokens);
        } else if (command == "position") {
            set_position(tokens);
        } else if (command == "go") {
            go(tokens);
        } else if (command == "stop") {
            pondering_.store(false);
            finish_search(true);
        } else if (command == "ponderhit") {
            ponder_hit();
        } else if (command == "bench") {
            bench(static_cast<int>(number_after(tokens, 0, 10)));
        } else if (command == "help" || command == "--help") {
            out_ << help() << std::flush;
        } else if (command == "quit") {
            finish_search(true);
            return false;
        } else if (command != "debug" && command != "register" && command[0] != '#') {
            send("Unknown command: '" + line + "'. Type help for more information.");
        }
        return true;
    }

    void Engine::new_game() {
        finish_search(true);
        searcher_->clear();
        position_ = Position();
        history_.clear();
    }

    void Engine::set_option(const std::vector<std::string> &tokens) {
        // setoption name <words> [value <words>]
        std::string name;
        std::string value;
        bool in_value = false;

        for (size_t i = 1; i < tokens.size(); ++i) {
            if (tokens[i] == "name" && !in_value && name.empty()) {
                continue;
            }
            if (tokens[i] == "value" && !in_value) {
                in_value = true;
                continue;
            }

            std::string &target = in_value ? value : name;
            target += (target.empty() ? "" : " ") + tokens[i];
        }

        finish_search(true);
        const std::string key = lower(name);
        const long long number = std::strtoll(value.c_str(), nullptr, 10);

        if (key == "hash") {
            settings_.hash_mb = static_cast<int>(std::clamp<long long>(number, 1, 4096));
            searcher_->options().hash_mb = static_cast<size_t>(settings_.hash_mb);
            searcher_->clear();
        } else if (key == "threads") {
            settings_.threads = static_cast<int>(std::clamp<long long>(number, 1, 64));
            searcher_->options().threads = settings_.threads;
        } else if (key == "moveoverhead") {
            settings_.move_overhead_ms = static_cast<int>(std::clamp<long long>(number, 0, 5000));
        } else if (key == "ownbook") {
            const std::string text = lower(value);
            settings_.own_book = text == "true" || text == "1";
        } else if (key == "clear hash") {
            searcher_->clear();
        }
    }

    void Engine::set_position(const std::vector<std::string> &tokens) {
        finish_search(true);

        size_t at = 1;
        Position position;

        if (at < tokens.size() && tokens[at] == "startpos") {
            ++at;
        } else if (at < tokens.size() && tokens[at] == "fen") {
            ++at;
            std::string fen;

            while (at < tokens.size() && tokens[at] != "moves") {
                fen += (fen.empty() ? "" : " ") + tokens[at];
                ++at;
            }

            const auto parsed = Position::from_fen(fen);

            if (!parsed) {
                send("info string invalid position: " + fen);
                return;
            }
            position = *parsed;
        } else {
            return;
        }

        std::vector<uint64_t> history;

        if (at < tokens.size() && tokens[at] == "moves") {
            for (++at; at < tokens.size(); ++at) {
                const Move move = move_from_uci(position, tokens[at]);

                if (move.is_null()) {
                    send("info string illegal move: " + tokens[at]);
                    return;
                }

                history.push_back(position.key());
                position.make_move(move);
            }
        }

        position_ = position;
        history_ = std::move(history);
    }

    void Engine::go(const std::vector<std::string> &tokens) {
        GoCommand command;

        for (size_t i = 1; i < tokens.size(); ++i) {
            const std::string &word = tokens[i];

            if (word == "wtime") {
                command.wtime = number_after(tokens, i, 0);
            } else if (word == "btime") {
                command.btime = number_after(tokens, i, 0);
            } else if (word == "winc") {
                command.winc = number_after(tokens, i, 0);
            } else if (word == "binc") {
                command.binc = number_after(tokens, i, 0);
            } else if (word == "movestogo") {
                command.moves_to_go = static_cast<int>(number_after(tokens, i, 0));
            } else if (word == "movetime") {
                command.move_time = number_after(tokens, i, 0);
            } else if (word == "depth") {
                command.depth = static_cast<int>(number_after(tokens, i, 0));
            } else if (word == "nodes") {
                command.nodes =
                    static_cast<uint64_t>(std::max<int64_t>(0, number_after(tokens, i, 0)));
            } else if (word == "mate") {
                // A mate in N moves is at most 2 * N - 1 plies deep, and the search ends as soon
                // as it has found the mate.
                command.depth =
                    static_cast<int>(std::max<int64_t>(1, number_after(tokens, i, 1))) * 2;
            } else if (word == "infinite") {
                command.infinite = true;
            } else if (word == "ponder") {
                command.ponder = true;
            } else if (word == "perft") {
                perft(static_cast<int>(std::max<int64_t>(1, number_after(tokens, i, 1))));
                return;
            } else if (word == "searchmoves") {
                for (size_t j = i + 1; j < tokens.size(); ++j) {
                    const Move move = move_from_uci(position_, tokens[j]);

                    if (move.is_null()) {
                        break;
                    }
                    command.search_moves.push_back(move);
                }
            }
        }

        if (command.ponder) {
            // The search runs without limits until `ponderhit`, which applies the clock.
            finish_search(true);
            pondered_ = command;
            pondered_.ponder = false;
            command.infinite = true;
            pondering_.store(true);
        } else {
            pondering_.store(false);
        }

        start_search(command);
    }

    void Engine::ponder_hit() {
        if (!pondering_.exchange(false)) {
            return;
        }

        // The search of the pondered position goes on as a search with the clock. The table
        // keeps what the first search learned.
        quiet_.store(true);
        finish_search(true);
        quiet_.store(false);
        start_search(pondered_);
    }

    void Engine::perft(int depth) {
        finish_search(true);
        MoveList legal;
        generate_legal(position_, legal);
        uint64_t total = 0;
        std::ostringstream report;

        for (const Move move : legal) {
            Position next = position_;
            next.make_move(move);
            const uint64_t nodes = depth > 1 ? chess::perft(next, depth - 1) : 1;
            total += nodes;
            report << move_to_uci(move) << ": " << nodes << '\n';
        }

        report << "\nNodes searched: " << total;
        send(report.str());
    }

    void Engine::start_search(const GoCommand &command) {
        finish_search(true);

        MoveList legal;
        generate_legal(position_, legal);

        if (legal.empty()) {
            send("bestmove 0000");
            return;
        }

        const bool timed = command.wtime > 0 || command.btime > 0 || command.move_time > 0;
        const bool limited = timed || command.depth > 0 || command.nodes > 0;
        const bool infinite = command.infinite || !limited;

        if (settings_.own_book && !infinite) {
            static thread_local std::mt19937_64 random{std::random_device{}()};
            const auto choice = Book::builtin().pick(position_, random());

            if (choice) {
                send("info string book move");
                send("bestmove " + move_to_uci(choice->move));
                return;
            }
        }

        SearchOptions &options = searcher_->options();
        options.max_depth = command.depth > 0 ? command.depth : 64;
        options.node_limit = command.nodes;
        options.root_moves = command.search_moves;
        options.time_limit_ms = 0;
        options.soft_time_limit_ms = 0;

        if (timed && !command.infinite) {
            Clock clock;
            clock.move_time_ms = command.move_time;
            clock.moves_to_go = command.moves_to_go;

            if (position_.side_to_move() == kWhite) {
                clock.time_ms = command.wtime;
                clock.increment_ms = command.winc;
            } else {
                clock.time_ms = command.btime;
                clock.increment_ms = command.binc;
            }

            const TimeBudget budget = allocate_time(clock, settings_.move_overhead_ms);
            options.time_limit_ms = budget.hard_ms;
            options.soft_time_limit_ms = budget.soft_ms;
        }

        infinite_ = infinite;
        stop_requested_.store(false);
        searching_.store(true);

        const Position position = position_;
        const std::vector<uint64_t> history = history_;

        worker_ = std::thread([this, position, history, infinite] {
            const SearchResult result = searcher_->search(position, history);

            // The protocol does not allow a `bestmove` before `stop` in an infinite search.
            if (infinite) {
                std::unique_lock<std::mutex> lock(wait_lock_);
                wait_.wait(lock, [this] { return stop_requested_.load(); });
            }

            std::string reply =
                "bestmove " +
                (result.best_move.is_null() ? std::string("0000") : move_to_uci(result.best_move));

            if (result.pv.size() >= 2) {
                reply += " ponder " + move_to_uci(result.pv[1]);
            }

            if (!quiet_.load()) {
                send(reply);
            }

            searching_.store(false);
        });
    }

    void Engine::finish_search(bool stop_it) {
        if (!worker_.joinable()) {
            return;
        }

        if (stop_it) {
            // A `stop` that arrives before the search has begun is cleared by it, so the
            // request is repeated until the search is over.
            stop_requested_.store(true);

            while (searching_.load()) {
                searcher_->stop();
                wait_.notify_all();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            wait_.notify_all();
        }

        worker_.join();
    }

    uint64_t Engine::bench(int depth) {
        finish_search(true);
        SearchOptions options = SearchOptions::preset("tuned", std::max(1, depth));
        options.hash_mb = 16;
        Searcher searcher(options);
        uint64_t nodes = 0;
        double ms = 0;

        for (const char *fen : kBenchPositions) {
            const auto position = Position::from_fen(fen);

            if (!position) {
                continue;
            }

            searcher.clear();
            const SearchResult result = searcher.search(*position);
            nodes += result.stats.nodes;
            ms += result.elapsed_ms;
        }

        std::ostringstream report;
        report << "Total time (ms) : " << static_cast<int64_t>(ms) << '\n'
               << "Nodes searched  : " << nodes << '\n'
               << "Nodes/second    : "
               << static_cast<uint64_t>(static_cast<double>(nodes) * 1000.0 / std::max(1.0, ms));
        send(report.str());
        return nodes;
    }

}  // namespace chess::uci
