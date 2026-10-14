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

// Tunes the numbers of the evaluation by the Texel method.
//
// Usage: texel_tune --data positions.txt [--init FILE] [--out params.txt]
//                   [--groups material,pst,king] [--epochs 40] [--threads 24] [--step 1]
//                   [--limit 0] [--k 0]
//
// The data file has lines `FEN<TAB>result<TAB>game`, as `gen_positions` writes them. The
// evaluation of a position is turned into an expected result with a logistic curve, and the
// tuner changes one number at a time by `--step`, keeping a change when it lowers the mean
// squared error against the results. The scale `--k` of the curve is fitted first when it is 0.
// A fifth of the games, chosen by game number, are held out: the error on them is printed
// after every epoch, and the numbers with the lowest held-out error are written.
//
// Groups: material, pst, king, phase_weight, bishop_pair, passed, doubled, isolated, rook_open,
// rook_half, mobility, tempo. The value of a pawn is fixed at 100.

#include <atomic>
#include <cmath>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <thread>

#include "../bench/common.hpp"
#include "eval.hpp"

namespace {

    using namespace chess;

    struct Sample {
        Position pos;
        float result;
    };

    struct Dataset {
        std::vector<Sample> train;
        std::vector<Sample> held;
    };

    Dataset load(const std::string &path, size_t limit) {
        std::ifstream in(path);
        Dataset data;
        std::string line;

        while (std::getline(in, line) &&
               (limit == 0 || data.train.size() + data.held.size() < limit)) {
            std::istringstream fields(line);
            std::string fen;
            std::string result;
            std::string game;
            std::getline(fields, fen, '\t');
            std::getline(fields, result, '\t');
            std::getline(fields, game, '\t');
            const auto pos = Position::from_fen(fen);

            if (!pos) {
                continue;
            }

            Sample sample{*pos, std::stof(result)};
            (std::stoi(game) % 5 == 0 ? data.held : data.train).push_back(sample);
        }
        return data;
    }

    double sigmoid(double score, double k) {
        return 1.0 / (1.0 + std::pow(10.0, -k * score / 400.0));
    }

    /**
     * Mean squared error of the expected results against the results.
     */
    double error(const std::vector<Sample> &samples, const EvalParams &params, double k,
                 int threads) {
        std::vector<double> sums(static_cast<size_t>(threads), 0.0);
        std::vector<std::thread> pool;

        for (int t = 0; t < threads; ++t) {
            pool.emplace_back([&, t] {
                double sum = 0;

                for (size_t i = static_cast<size_t>(t); i < samples.size();
                     i += static_cast<size_t>(threads)) {
                    const Sample &sample = samples[i];
                    int score = evaluate(sample.pos, params);

                    if (sample.pos.side_to_move() == kBlack) {
                        score = -score;
                    }

                    const double diff = sample.result - sigmoid(score, k);
                    sum += diff * diff;
                }
                sums[static_cast<size_t>(t)] = sum;
            });
        }

        for (std::thread &thread : pool) {
            thread.join();
        }

        double total = 0;

        for (double sum : sums) {
            total += sum;
        }
        return total / static_cast<double>(samples.size());
    }

}  // namespace

int main(int argc, char **argv) {
    const bench::Args args(argc, argv);
    const std::string data_path = args.get("data", "positions.txt");
    const std::string out_path = args.get("out", "params.txt");
    const int threads = std::max(1, args.get_int("threads", 24));
    const int epochs = args.get_int("epochs", 40);
    const int step = std::max(1, args.get_int("step", 1));
    const size_t limit = static_cast<size_t>(args.get_int("limit", 0));
    double k = std::stod(args.get("k", "0"));

    std::set<std::string> groups;
    {
        std::istringstream list(args.get("groups", "material,pst,king"));
        std::string name;

        while (std::getline(list, name, ',')) {
            groups.insert(name);
        }
    }

    EvalParams params = EvalParams::defaults();

    if (args.has("init") && !params.load(args.get("init", ""))) {
        std::cerr << "cannot read " << args.get("init", "") << "\n";
        return 1;
    }

    Dataset data = load(data_path, limit);
    std::printf("%zu training and %zu held-out positions\n", data.train.size(), data.held.size());

    if (data.train.empty() || data.held.empty()) {
        return 1;
    }

    if (k <= 0) {
        // Golden section search of the scale of the curve.
        double low = 0.05;
        double high = 3.0;
        const double phi = 0.6180339887;

        for (int i = 0; i < 24; ++i) {
            const double a = high - phi * (high - low);
            const double b = low + phi * (high - low);

            if (error(data.train, params, a, threads) < error(data.train, params, b, threads)) {
                high = b;
            } else {
                low = a;
            }
        }
        k = (low + high) / 2;
    }

    std::printf("scale k = %.4f\n", k);

    // The numbers that are tuned, as pointers into the parameters.
    std::vector<int *> tuned;
    std::vector<std::string> names;
    params.visit([&](const char *name, int *values, int count) {
        if (!groups.count(name)) {
            return;
        }

        for (int i = 0; i < count; ++i) {
            const bool pawn_value = std::string(name) == "material" && (i == 0 || i == 5);

            if (!pawn_value) {
                tuned.push_back(&values[i]);
                names.push_back(name);
            }
        }
    });
    std::printf("%zu numbers in groups:", tuned.size());

    for (const std::string &group : groups) {
        std::printf(" %s", group.c_str());
    }

    std::printf("\n");

    double best = error(data.train, params, k, threads);
    double held = error(data.held, params, k, threads);
    double best_held = held;
    EvalParams kept = params;
    std::printf("start: train %.6f held %.6f\n", best, held);
    int worse = 0;

    for (int epoch = 1; epoch <= epochs; ++epoch) {
        int changed = 0;

        for (int *value : tuned) {
            for (const int direction : {1, -1}) {
                const int before = *value;
                *value = before + direction * step;
                const double trial = error(data.train, params, k, threads);

                if (trial < best) {
                    best = trial;
                    ++changed;
                    break;
                }
                *value = before;
            }
        }

        held = error(data.held, params, k, threads);
        std::printf("epoch %2d: %d changed, train %.6f held %.6f\n", epoch, changed, best, held);
        std::fflush(stdout);

        if (held < best_held) {
            best_held = held;
            kept = params;
            worse = 0;
            kept.save(out_path);
        } else if (++worse >= 2) {
            break;
        }

        if (changed == 0) {
            break;
        }
    }

    kept.save(out_path);
    std::printf("held-out error %.6f, written to %s\n", best_held, out_path.c_str());
    return 0;
}
