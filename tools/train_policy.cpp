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

// Trains the move policy of the search from games of the engine against itself.
//
// The engine plays games from random openings with a fixed depth, and every position with its
// chosen move is a sample. The policy is a log-linear model over the binary features of
// `policy.hpp`, trained to give the chosen move the highest probability among the legal
// moves of the position. The weights are written as `src/policy_weights.inc`.
//
// Usage: train_policy [--games 600] [--depth 6] [--config full+see] [--threads 12]
//                     [--epochs 12] [--out src/policy_weights.inc] [--seed 1]

#include <atomic>
#include <cmath>
#include <fstream>
#include <iostream>
#include <mutex>
#include <random>
#include <thread>

#include "../bench/common.hpp"
#include "movegen.hpp"
#include "policy.hpp"
#include "search.hpp"

namespace {

    using namespace chess;

    /**
     * One position with its legal moves as feature lists, and the index of the chosen move.
     */
    struct Sample {
        std::vector<std::vector<int>> moves;
        size_t chosen = 0;
    };

    /**
     * Plays one game and appends its samples.
     */
    void play(uint64_t seed, const std::string &config, int depth, std::vector<Sample> &out) {
        std::mt19937_64 random(seed);
        Position pos;
        std::vector<uint64_t> history;

        for (int ply = 0; ply < 8; ++ply) {
            MoveList legal;
            generate_legal(pos, legal);

            if (legal.empty()) {
                return;
            }

            history.push_back(pos.key());
            pos.make_move(legal[static_cast<int>(random() % static_cast<uint64_t>(legal.size()))]);
        }

        SearchOptions options = SearchOptions::preset(config, depth);
        options.hash_mb = 16;
        Searcher searcher(options);

        for (int ply = 0; ply < 200; ++ply) {
            MoveList legal;
            generate_legal(pos, legal);

            if (legal.empty() || pos.halfmove_clock() >= 100 || pos.insufficient_material()) {
                return;
            }

            const SearchResult result = searcher.search(pos, history);

            if (legal.size() > 1 && !result.best_move.is_null()) {
                Sample sample;

                for (int i = 0; i < legal.size(); ++i) {
                    int active[policy::kMaxActive];
                    const int count = policy::features(pos, legal[i], active);
                    sample.moves.emplace_back(active, active + count);

                    if (legal[i] == result.best_move) {
                        sample.chosen = static_cast<size_t>(i);
                    }
                }

                out.push_back(std::move(sample));
            }

            history.push_back(pos.key());
            pos.make_move(result.best_move);
        }
    }

    /**
     * Probabilities of the moves of a sample under the weights.
     */
    std::vector<double> probabilities(const Sample &sample, const std::vector<double> &weights) {
        std::vector<double> p(sample.moves.size());
        double largest = -1e300;

        for (size_t i = 0; i < p.size(); ++i) {
            double logit = 0;

            for (int f : sample.moves[i]) {
                logit += weights[static_cast<size_t>(f)];
            }

            p[i] = logit;
            largest = std::max(largest, logit);
        }

        double total = 0;

        for (double &value : p) {
            value = std::exp(value - largest);
            total += value;
        }

        for (double &value : p) {
            value /= total;
        }

        return p;
    }

}  // namespace

int main(int argc, char **argv) {
    const bench::Args args(argc, argv);
    const int games = args.get_int("games", 600);
    const int depth = args.get_int("depth", 6);
    const std::string config = args.get("config", "full+see");
    const int threads = std::max(1, args.get_int("threads", 12));
    const int epochs = args.get_int("epochs", 12);
    const std::string path = args.get("out", "src/policy_weights.inc");
    const uint64_t seed = static_cast<uint64_t>(args.get_int("seed", 1));

    std::vector<Sample> samples;
    std::mutex lock;
    std::atomic<int> next{0};
    std::vector<std::thread> pool;

    for (int t = 0; t < threads; ++t) {
        pool.emplace_back([&] {
            while (true) {
                const int game = next.fetch_add(1);

                if (game >= games) {
                    return;
                }

                std::vector<Sample> local;
                play(seed * 1000003ULL + static_cast<uint64_t>(game), config, depth, local);

                std::lock_guard<std::mutex> guard(lock);

                for (Sample &sample : local) {
                    samples.push_back(std::move(sample));
                }
            }
        });
    }

    for (auto &thread : pool) {
        thread.join();
    }

    std::mt19937_64 random(seed);
    std::shuffle(samples.begin(), samples.end(), random);

    const size_t held = samples.size() / 10;
    std::printf("%zu samples, %zu held out\n", samples.size(), held);

    std::vector<double> weights(policy::kFeatures, 0.0);

    auto evaluate = [&](size_t begin, size_t end) {
        double loss = 0;
        double uniform = 0;
        size_t top1 = 0;

        for (size_t i = begin; i < end; ++i) {
            const std::vector<double> p = probabilities(samples[i], weights);
            loss -= std::log(std::max(p[samples[i].chosen], 1e-12));
            uniform += std::log(static_cast<double>(p.size()));
            size_t best = 0;

            for (size_t m = 1; m < p.size(); ++m) {
                best = p[m] > p[best] ? m : best;
            }

            top1 += best == samples[i].chosen ? 1 : 0;
        }

        const double n = static_cast<double>(end - begin);
        std::printf("  loss %.3f (uniform %.3f), top-1 %.1f%%\n", loss / n, uniform / n,
                    100.0 * static_cast<double>(top1) / n);
    };

    for (int epoch = 0; epoch < epochs; ++epoch) {
        const double rate = 0.05 * (1.0 - static_cast<double>(epoch) / epochs);

        for (size_t i = held; i < samples.size(); ++i) {
            const Sample &sample = samples[i];
            const std::vector<double> p = probabilities(sample, weights);

            for (size_t m = 0; m < p.size(); ++m) {
                const double gradient = p[m] - (m == sample.chosen ? 1.0 : 0.0);

                for (int f : sample.moves[m]) {
                    weights[static_cast<size_t>(f)] -= rate * gradient;
                }
            }
        }

        std::printf("epoch %d, held out:", epoch + 1);
        evaluate(0, held);
    }

    std::ofstream out(path);

    for (size_t f = 0; f < weights.size(); ++f) {
        out << std::lround(weights[f] * 64) << (f + 1 < weights.size() ? ",\n" : "\n");
    }

    std::printf("wrote %s\n", path.c_str());
    return 0;
}
