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

// Builds the opening book by searching the opening tree.
//
// Usage: build_book [--out src/book_data.inc] [--whitelist book/whitelist.txt]
//                   [--depth 12] [--plies 10] [--own-width 3] [--own-margin 15]
//                   [--reply-width 4] [--reply-margin 30] [--max-eval 60]
//                   [--threads 8] [--hash 32]
//
// The tree is explored once for each colour the engine can play. In a position where the
// engine moves, the moves within `own-margin` of the best are kept, at most `own-width`, and
// the whitelist restricts the choice where it names the position. In a position where the
// opponent moves, the moves within `reply-margin` are followed, at most `reply-width`. A
// position whose best score is farther than `max-eval` from equal is not followed. Every move
// is scored by a search of the position after it to `depth - 1` plies.
//
// The output lists the moves of the positions where the engine moves, in the form that
// `chess/book.hpp` includes.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <vector>

#include "../bench/common.hpp"
#include "book.hpp"
#include "movegen.hpp"
#include "search.hpp"

namespace {

    using namespace chess;

    struct Settings {
        int depth = 12;
        int plies = 10;
        int own_width = 3;
        int own_margin = 15;
        int reply_width = 4;
        int reply_margin = 30;
        int max_eval = 60;
        int threads = 8;
        size_t hash_mb = 32;
    };

    struct ScoredMove {
        Move move;
        int score = 0;  ///< Centipawns for the side that makes the move.
    };

    /**
     * What the search found in one position of the tree.
     */
    struct Expansion {
        bool followed = false;
        std::vector<ScoredMove> kept;
    };

    using Whitelist = std::unordered_map<uint64_t, std::vector<Move>>;

    Whitelist read_whitelist(const std::string &path) {
        std::ifstream in(path);
        Whitelist list;
        std::string line;

        while (std::getline(in, line)) {
            const size_t colon = line.find(':');

            if (line.empty() || line[0] == '#' || colon == std::string::npos) {
                continue;
            }

            Position pos;
            std::istringstream played(line.substr(0, colon));
            std::string text;

            while (played >> text) {
                const Move move = move_from_uci(pos, text);

                if (move.is_null() || !pos.make_move(move)) {
                    std::cerr << "illegal move " << text << " in whitelist line: " << line << "\n";
                    std::exit(1);
                }
            }

            std::istringstream allowed(line.substr(colon + 1));
            std::vector<Move> moves;

            while (allowed >> text) {
                const Move move = move_from_uci(pos, text);

                if (move.is_null()) {
                    std::cerr << "illegal move " << text << " in whitelist line: " << line << "\n";
                    std::exit(1);
                }
                moves.push_back(move);
            }

            list[pos.key()] = moves;
        }
        return list;
    }

    /**
     * Scores every legal move of `pos` and keeps the ones the settings allow.
     */
    Expansion expand(const Position &pos, bool own, const Settings &settings,
                     const Whitelist &whitelist, Searcher &searcher) {
        MoveList legal;
        generate_legal(pos, legal);
        std::vector<Move> allowed(legal.begin(), legal.end());

        if (own) {
            const auto found = whitelist.find(pos.key());

            if (found != whitelist.end()) {
                allowed = found->second;
            }
        }

        std::vector<ScoredMove> scored;
        searcher.clear();

        for (const Move move : allowed) {
            Position child = pos;
            child.make_move(move);
            MoveList replies;
            generate_legal(child, replies);
            int score = 0;

            if (replies.empty()) {
                // Checkmate is a win for the mover, and stalemate is a draw.
                score = child.in_check() ? kMateScore : 0;
            } else {
                score = -searcher.search(child).score;
            }
            scored.push_back({move, score});
        }

        std::stable_sort(
            scored.begin(), scored.end(),
            [](const ScoredMove &a, const ScoredMove &b) { return a.score > b.score; });

        Expansion result;

        if (scored.empty() || std::abs(scored.front().score) > settings.max_eval) {
            return result;
        }

        result.followed = true;
        const int margin = own ? settings.own_margin : settings.reply_margin;
        const size_t width = static_cast<size_t>(own ? settings.own_width : settings.reply_width);

        for (const ScoredMove &entry : scored) {
            if (result.kept.size() >= width || entry.score < scored.front().score - margin) {
                break;
            }
            result.kept.push_back(entry);
        }
        return result;
    }

    struct Node {
        Position pos;
        uint64_t key = 0;
    };

}  // namespace

int main(int argc, char **argv) {
    const bench::Args args(argc, argv);
    Settings settings;
    settings.depth = args.get_int("depth", settings.depth);
    settings.plies = args.get_int("plies", settings.plies);
    settings.own_width = args.get_int("own-width", settings.own_width);
    settings.own_margin = args.get_int("own-margin", settings.own_margin);
    settings.reply_width = args.get_int("reply-width", settings.reply_width);
    settings.reply_margin = args.get_int("reply-margin", settings.reply_margin);
    settings.max_eval = args.get_int("max-eval", settings.max_eval);
    settings.threads = args.get_int("threads", settings.threads);
    settings.hash_mb =
        static_cast<size_t>(args.get_int("hash", static_cast<int>(settings.hash_mb)));
    const std::string out_path = args.get("out", "src/book_data.inc");
    const Whitelist whitelist = read_whitelist(args.get("whitelist", "book/whitelist.txt"));

    // Entries by position, each with the weights found for it.
    std::map<uint64_t, std::vector<ScoredMove>> book;
    const double start = bench::now_seconds();

    for (const Color engine : {kWhite, kBlack}) {
        std::vector<Node> frontier;
        const Position root;
        frontier.push_back({root, root.key()});

        for (int ply = 0; ply < settings.plies && !frontier.empty(); ++ply) {
            const bool own = frontier.front().pos.side_to_move() == engine;
            std::vector<Expansion> results(frontier.size());
            std::atomic<size_t> next{0};
            std::vector<std::thread> workers;

            for (int t = 0; t < settings.threads; ++t) {
                workers.emplace_back([&] {
                    SearchOptions options = SearchOptions::preset("full", settings.depth - 1);
                    options.hash_mb = settings.hash_mb;
                    Searcher searcher(options);

                    for (size_t i = next++; i < frontier.size(); i = next++) {
                        results[i] = expand(frontier[i].pos, own, settings, whitelist, searcher);
                    }
                });
            }

            for (std::thread &worker : workers) {
                worker.join();
            }

            std::vector<Node> following;
            std::map<uint64_t, bool> seen;
            size_t kept = 0;

            for (size_t i = 0; i < frontier.size(); ++i) {
                for (const ScoredMove &entry : results[i].kept) {
                    ++kept;

                    if (own) {
                        book[frontier[i].key].push_back(entry);
                    }

                    Node child{frontier[i].pos, 0};
                    child.pos.make_move(entry.move);
                    child.key = child.pos.key();

                    if (seen.emplace(child.key, true).second) {
                        following.push_back(child);
                    }
                }
            }

            std::printf("%s ply %2d: %6zu positions, %6zu moves kept, %6zu next, %.0fs\n",
                        engine == kWhite ? "white" : "black", ply, frontier.size(), kept,
                        following.size(), bench::now_seconds() - start);
            std::fflush(stdout);
            frontier = std::move(following);
        }
    }

    // A position is in one tree as a move of the engine, so each has one set of moves; a
    // move found twice is written once.
    size_t entries = 0;
    std::ostringstream body;

    for (auto &[key, moves] : book) {
        std::stable_sort(moves.begin(), moves.end(), [](const ScoredMove &a, const ScoredMove &b) {
            return a.score > b.score;
        });
        moves.erase(std::unique(moves.begin(), moves.end(),
                                [](const ScoredMove &a, const ScoredMove &b) {
                                    return a.move.data == b.move.data;
                                }),
                    moves.end());
        const int best = moves.front().score;

        for (const ScoredMove &entry : moves) {
            const int gap = std::max(0, best - entry.score);
            const int weight =
                std::max(1, static_cast<int>(std::lround(100 * std::exp(-gap / 15.0))));
            char line[128];
            std::snprintf(line, sizeof line, "    {0x%016llxull, 0x%04x, %d, %d},\n",
                          static_cast<unsigned long long>(key), entry.move.data, weight,
                          entry.score);
            body << line;
            ++entries;
        }
    }

    std::ofstream out(out_path);
    out << "// Generated by tools/build_book: depth " << settings.depth << ", " << settings.plies
        << " plies,\n// own moves " << settings.own_width << " within " << settings.own_margin
        << "cp, replies " << settings.reply_width << " within " << settings.reply_margin
        << "cp, evaluation within " << settings.max_eval << "cp.\n";
    out << "constexpr int kBookDepth = " << settings.depth << ";\n";
    out << "constexpr BookEntry kBookData[] = {\n" << body.str() << "};\n";
    std::printf("%zu entries for %zu positions\n", entries, book.size());
    return 0;
}
