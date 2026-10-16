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

// Trains the network of the evaluation from self-play positions.
//
// Usage: train_nnue --data positions.txt [--out net.nnue] [--epochs 30] [--batch 16384]
//                   [--lr 0.001] [--lambda 0.7] [--max-score 2500] [--threads 24] [--limit 0]
//                   [--seed 1] [--init FILE] [--inc FILE]
//
// The data file has lines `FEN<TAB>result<TAB>game<TAB>score`, as `gen_positions` and
// `scripts/convert_lc0.py` write them. A position in check is left out.
//
// The target of a position is `lambda` times the expected points of the search score plus
// `1 - lambda` times the result of the game, from the side to move. The network (see
// `nnue.hpp`) is trained in floating point with Adam to minimise the squared error of the
// expected points it predicts, with the scale of the evaluation tuning. A position uses the king
// bucket of its inputs and the output layer of its number of pieces. A fifth of the games are
// held out. The weights of the epoch with the lowest held-out error are quantized and written, and
// the quantized network is compared with the floating point one on held-out positions.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <thread>

#include "../bench/common.hpp"
#include "eval.hpp"
#include "nnue.hpp"

namespace {

    using namespace chess;
    using namespace chess::nnue;

    constexpr int kMaxPieces = 32;

    // The scale of the logistic curve of the evaluation tuning, in base 10 over 400cp.
    constexpr double kCurve = 1.25;

    struct Sample {
        uint8_t count = 0;
        uint8_t side = 0;         // 0 White to move, 1 Black
        float target = 0;         // expected points for the side to move
        uint8_t kb[2] = {0, 0};   // king bucket of each point of view
        uint8_t mir[2] = {0, 0};  // mirror of each point of view
        uint16_t pieces[kMaxPieces];
    };

    // Fills the side, the pieces and the king contexts of a sample from a position.
    void fill_sample(const Position &pos, Sample &sample) {
        sample.side = pos.side_to_move() == kWhite ? 0 : 1;

        for (int sq = 0; sq < kSquares && sample.count < kMaxPieces; ++sq) {
            const Piece piece = pos.piece_on(static_cast<Square>(sq));

            if (piece != kNoPiece) {
                sample.pieces[sample.count++] = static_cast<uint16_t>(piece * 64 + sq);
            }
        }

        for (int p = 0; p < 2; ++p) {
            int kb = 0, mir = 0;
            king_context(p, pos.king_square(p == 0 ? kWhite : kBlack), kb, mir);
            sample.kb[p] = static_cast<uint8_t>(kb);
            sample.mir[p] = static_cast<uint8_t>(mir);
        }
    }

    double sigmoid(double x) {
        return 1.0 / (1.0 + std::exp(-x));
    }

    // The factor from the output of the network to the exponent of the logistic curve.
    const double kToLogit = kOutputScale * kCurve * std::log(10.0) / 400.0;

    struct Dataset {
        std::vector<Sample> train;
        std::vector<Sample> held;
        std::vector<std::string> held_fens;  // the first held-out positions, for the check
        double hand_loss = 0;                // squared error of the hand written evaluation
        size_t hand_count = 0;
    };

    Dataset load_samples(const std::string &path, double lambda, int max_score, size_t limit) {
        std::ifstream in(path);
        Dataset data;
        std::string line;
        size_t read = 0;

        while (std::getline(in, line) && (limit == 0 || read < limit)) {
            std::istringstream fields(line);
            std::string fen;
            std::string result;
            std::string game;
            std::string score;
            std::getline(fields, fen, '\t');
            std::getline(fields, result, '\t');
            std::getline(fields, game, '\t');
            std::getline(fields, score, '\t');

            if (score.empty()) {
                continue;
            }

            const int cp = std::stoi(score);

            if (std::abs(cp) > max_score) {
                continue;
            }

            const auto pos = Position::from_fen(fen);

            // A position in check has no quiet score, and the search does not evaluate it.
            if (!pos || pos->in_check()) {
                continue;
            }

            Sample sample;
            fill_sample(*pos, sample);

            const double white_result = std::stod(result);
            const double side_cp = sample.side == 0 ? cp : -cp;
            const double side_result = sample.side == 0 ? white_result : 1.0 - white_result;
            const double points = 1.0 / (1.0 + std::pow(10.0, -kCurve * side_cp / 400.0));
            sample.target = static_cast<float>(lambda * points + (1.0 - lambda) * side_result);
            ++read;

            if (std::stoi(game) % 5 == 0) {
                // The hand written evaluation on the same position and target.
                const double hand = 1.0 / (1.0 + std::pow(10.0, -kCurve * evaluate(*pos) / 400.0));
                data.hand_loss += (hand - sample.target) * (hand - sample.target);
                ++data.hand_count;

                if (data.held_fens.size() < 4000) {
                    data.held_fens.push_back(fen);
                }
                data.held.push_back(sample);
            } else {
                data.train.push_back(sample);
            }
        }
        return data;
    }

    /**
     * The weights in floating point.
     */
    struct Model {
        std::vector<float> w = std::vector<float>(kInputs * kHidden);
        std::vector<float> b = std::vector<float>(kHidden);
        std::vector<float> wo = std::vector<float>(kBuckets * 2 * kHidden);
        std::vector<float> bo = std::vector<float>(kBuckets);
    };

    struct Gradient {
        std::vector<float> w = std::vector<float>(kInputs * kHidden);
        std::vector<float> b = std::vector<float>(kHidden);
        std::vector<float> wo = std::vector<float>(kBuckets * 2 * kHidden);
        std::vector<float> bo = std::vector<float>(kBuckets);
        double loss = 0;

        void clear() {
            std::fill(w.begin(), w.end(), 0.0f);
            std::fill(b.begin(), b.end(), 0.0f);
            std::fill(wo.begin(), wo.end(), 0.0f);
            std::fill(bo.begin(), bo.end(), 0.0f);
            loss = 0;
        }
    };

    /**
     * Forward pass of one sample. Returns the output of the network (in units of 400cp) and
     * leaves the accumulators of both sides in `acc` ([0] for the side to move).
     */
    float forward(const Model &m, const Sample &s, float acc[2][kHidden]) {
        // Accumulator of the white point of view and of the black one.
        float white[kHidden] = {};
        float black[kHidden] = {};
        std::copy(m.b.begin(), m.b.end(), white);
        std::copy(m.b.begin(), m.b.end(), black);

        for (int i = 0; i < s.count; ++i) {
            const int piece = s.pieces[i] / 64;
            const int square = s.pieces[i] % 64;
            const float *wr =
                &m.w[static_cast<size_t>(king_feature(0, piece, square, s.kb[0], s.mir[0])) *
                     kHidden];
            const float *br =
                &m.w[static_cast<size_t>(king_feature(1, piece, square, s.kb[1], s.mir[1])) *
                     kHidden];

            for (int h = 0; h < kHidden; ++h) {
                white[h] += wr[h];
                black[h] += br[h];
            }
        }

        const float *us = s.side == 0 ? white : black;
        const float *them = s.side == 0 ? black : white;
        const int bucket = bucket_of(s.count);
        const float *wo = &m.wo[static_cast<size_t>(bucket) * 2 * kHidden];
        float out = m.bo[bucket];

        for (int h = 0; h < kHidden; ++h) {
            acc[0][h] = us[h];
            acc[1][h] = them[h];
            out += std::clamp(us[h], 0.0f, 1.0f) * wo[h] +
                   std::clamp(them[h], 0.0f, 1.0f) * wo[kHidden + h];
        }
        return out;
    }

    /**
     * The squared error of the expected points, and when `g` is given, adds the gradient.
     */
    double step(const Model &m, const Sample &s, Gradient *g, double scale) {
        float acc[2][kHidden];
        const float out = forward(m, s, acc);
        const double prediction = sigmoid(static_cast<double>(out) * kToLogit);
        const double error = prediction - s.target;

        if (g != nullptr) {
            const double d_out = 2.0 * error * prediction * (1.0 - prediction) * kToLogit * scale;
            const int bucket = bucket_of(s.count);
            const float *wo = &m.wo[static_cast<size_t>(bucket) * 2 * kHidden];
            float *gwo = &g->wo[static_cast<size_t>(bucket) * 2 * kHidden];
            g->bo[bucket] += static_cast<float>(d_out);

            float d_us[kHidden];
            float d_them[kHidden];

            for (int h = 0; h < kHidden; ++h) {
                const float a = std::clamp(acc[0][h], 0.0f, 1.0f);
                const float c = std::clamp(acc[1][h], 0.0f, 1.0f);
                gwo[h] += static_cast<float>(d_out) * a;
                gwo[kHidden + h] += static_cast<float>(d_out) * c;
                d_us[h] = (acc[0][h] > 0.0f && acc[0][h] < 1.0f) ? static_cast<float>(d_out) * wo[h]
                                                                 : 0.0f;
                d_them[h] = (acc[1][h] > 0.0f && acc[1][h] < 1.0f)
                                ? static_cast<float>(d_out) * wo[kHidden + h]
                                : 0.0f;
                g->b[h] += d_us[h] + d_them[h];
            }

            const float *d_white = s.side == 0 ? d_us : d_them;
            const float *d_black = s.side == 0 ? d_them : d_us;

            for (int i = 0; i < s.count; ++i) {
                const int piece = s.pieces[i] / 64;
                const int square = s.pieces[i] % 64;
                float *wr =
                    &g->w[static_cast<size_t>(king_feature(0, piece, square, s.kb[0], s.mir[0])) *
                          kHidden];
                float *br =
                    &g->w[static_cast<size_t>(king_feature(1, piece, square, s.kb[1], s.mir[1])) *
                          kHidden];

                for (int h = 0; h < kHidden; ++h) {
                    wr[h] += d_white[h];
                    br[h] += d_black[h];
                }
            }
        }
        return error * error;
    }

    double mean_loss(const Model &m, const std::vector<Sample> &samples, int threads) {
        std::vector<double> sums(static_cast<size_t>(threads), 0.0);
        std::vector<std::thread> pool;

        for (int t = 0; t < threads; ++t) {
            pool.emplace_back([&, t] {
                double sum = 0;

                for (size_t i = static_cast<size_t>(t); i < samples.size();
                     i += static_cast<size_t>(threads)) {
                    sum += step(m, samples[i], nullptr, 0);
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

    struct Adam {
        std::vector<float> m;
        std::vector<float> v;

        explicit Adam(size_t size) : m(size, 0.0f), v(size, 0.0f) {
        }

        void update(std::vector<float> &params, const std::vector<float> &grad, double lr,
                    int step_number) {
            const double beta1 = 0.9;
            const double beta2 = 0.999;
            const double c1 = 1.0 - std::pow(beta1, step_number);
            const double c2 = 1.0 - std::pow(beta2, step_number);

            for (size_t i = 0; i < params.size(); ++i) {
                m[i] = static_cast<float>(beta1 * m[i] + (1 - beta1) * grad[i]);
                v[i] = static_cast<float>(beta2 * v[i] + (1 - beta2) * grad[i] * grad[i]);
                params[i] -= static_cast<float>(lr * (m[i] / c1) / (std::sqrt(v[i] / c2) + 1e-8));
            }
        }
    };

    int16_t quantize(double value, double scale) {
        return static_cast<int16_t>(std::clamp(std::lround(value * scale), -32767L, 32767L));
    }

    Network quantized(const Model &m) {
        Network net;

        for (int i = 0; i < kInputs; ++i) {
            for (int h = 0; h < kHidden; ++h) {
                net.feature_weights[i][h] =
                    quantize(m.w[static_cast<size_t>(i) * kHidden + h], kQuantInput);
            }
        }

        for (int h = 0; h < kHidden; ++h) {
            net.feature_bias[h] = quantize(m.b[h], kQuantInput);
        }

        for (int bucket = 0; bucket < kBuckets; ++bucket) {
            const size_t base = static_cast<size_t>(bucket) * 2 * kHidden;

            for (int h = 0; h < kHidden; ++h) {
                net.output_weights[bucket][0][h] = quantize(m.wo[base + h], kQuantOutput);
                net.output_weights[bucket][1][h] = quantize(m.wo[base + kHidden + h], kQuantOutput);
            }

            net.output_bias[bucket] =
                static_cast<int32_t>(std::lround(m.bo[bucket] * kQuantInput * kQuantOutput));
        }
        return net;
    }

}  // namespace

int main(int argc, char **argv) {
    const bench::Args args(argc, argv);
    const std::string data_path = args.get("data", "positions.txt");
    const std::string out_path = args.get("out", "net.nnue");
    const int epochs = args.get_int("epochs", 30);
    const size_t batch = static_cast<size_t>(args.get_int("batch", 16384));
    const double lr0 = std::stod(args.get("lr", "0.001"));
    const double lambda = std::stod(args.get("lambda", "0.7"));
    const int max_score = args.get_int("max-score", 2500);
    const int threads = std::max(1, args.get_int("threads", 24));
    const size_t limit = static_cast<size_t>(args.get_int("limit", 0));
    const uint64_t seed = static_cast<uint64_t>(args.get_int("seed", 1));

    Dataset data = load_samples(data_path, lambda, max_score, limit);
    std::printf("%zu training and %zu held-out positions\n", data.train.size(), data.held.size());
    std::printf("the hand written evaluation: held-out loss %.5f\n",
                data.hand_loss / static_cast<double>(std::max<size_t>(1, data.hand_count)));

    if (data.train.empty() || data.held.empty()) {
        return 1;
    }

    Model model;
    std::mt19937_64 random(seed);
    std::normal_distribution<float> normal(0.0f, 1.0f);

    if (args.has("init")) {
        const auto start = load(args.get("init", ""));

        if (!start) {
            std::cerr << "cannot read the network to start from\n";
            return 1;
        }

        for (int i = 0; i < kInputs; ++i) {
            for (int h = 0; h < kHidden; ++h) {
                model.w[static_cast<size_t>(i) * kHidden + h] =
                    static_cast<float>(start->feature_weights[i][h]) / kQuantInput;
            }
        }

        for (int h = 0; h < kHidden; ++h) {
            model.b[h] = static_cast<float>(start->feature_bias[h]) / kQuantInput;
        }

        for (int bucket = 0; bucket < kBuckets; ++bucket) {
            const size_t base = static_cast<size_t>(bucket) * 2 * kHidden;

            for (int h = 0; h < kHidden; ++h) {
                model.wo[base + h] =
                    static_cast<float>(start->output_weights[bucket][0][h]) / kQuantOutput;
                model.wo[base + kHidden + h] =
                    static_cast<float>(start->output_weights[bucket][1][h]) / kQuantOutput;
            }

            model.bo[bucket] =
                static_cast<float>(start->output_bias[bucket]) / (kQuantInput * kQuantOutput);
        }
    } else {
        // Weights of a few features add up to about one, so that the units start in the range
        // where they are not clipped.
        for (float &w : model.w) {
            w = normal(random) * 0.1f;
        }
        for (float &w : model.wo) {
            w = normal(random) * 0.1f;
        }
    }

    Adam adam_w(model.w.size());
    Adam adam_b(model.b.size());
    Adam adam_wo(model.wo.size());
    Adam adam_bo(model.bo.size());

    std::vector<size_t> order(data.train.size());

    for (size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }

    std::vector<Gradient> gradients(static_cast<size_t>(threads));
    Gradient total;
    double best_held = 1e9;
    int step_number = 0;
    std::printf("start: held-out loss %.5f\n", mean_loss(model, data.held, threads));

    for (int epoch = 1; epoch <= epochs; ++epoch) {
        std::shuffle(order.begin(), order.end(), random);
        const double lr = lr0 * std::pow(0.1, static_cast<double>(epoch - 1) / epochs);
        double train_loss = 0;
        size_t seen = 0;

        for (size_t start = 0; start + batch <= order.size(); start += batch) {
            const double scale = 1.0 / static_cast<double>(batch);
            std::vector<std::thread> pool;

            for (int t = 0; t < threads; ++t) {
                pool.emplace_back([&, t] {
                    Gradient &g = gradients[static_cast<size_t>(t)];
                    g.clear();

                    for (size_t i = static_cast<size_t>(t); i < batch;
                         i += static_cast<size_t>(threads)) {
                        g.loss += step(model, data.train[order[start + i]], &g, scale);
                    }
                });
            }

            for (std::thread &thread : pool) {
                thread.join();
            }

            total.clear();

            for (const Gradient &g : gradients) {
                for (size_t i = 0; i < total.w.size(); ++i) {
                    total.w[i] += g.w[i];
                }
                for (size_t i = 0; i < total.b.size(); ++i) {
                    total.b[i] += g.b[i];
                }
                for (size_t i = 0; i < total.wo.size(); ++i) {
                    total.wo[i] += g.wo[i];
                }
                for (size_t i = 0; i < total.bo.size(); ++i) {
                    total.bo[i] += g.bo[i];
                }
                total.loss += g.loss;
            }

            ++step_number;
            adam_w.update(model.w, total.w, lr, step_number);
            adam_b.update(model.b, total.b, lr, step_number);
            adam_wo.update(model.wo, total.wo, lr, step_number);
            adam_bo.update(model.bo, total.bo, lr, step_number);

            train_loss += total.loss;
            seen += batch;
        }

        const double held = mean_loss(model, data.held, threads);
        std::printf("epoch %2d: lr %.5f train %.5f held %.5f\n", epoch, lr,
                    train_loss / static_cast<double>(std::max<size_t>(1, seen)), held);
        std::fflush(stdout);

        if (held < best_held) {
            best_held = held;
            const Network net = quantized(model);
            save(net, out_path);
        }
    }

    // The integer network against the floating point one, on held-out positions.
    const auto net = load(out_path);

    if (net) {
        double difference = 0;
        double magnitude = 0;
        size_t count = 0;
        Accumulator acc;

        for (const std::string &fen : data.held_fens) {
            const auto pos = Position::from_fen(fen);

            if (!pos) {
                continue;
            }

            refresh(*net, *pos, acc);
            const int cp = evaluate(*net, acc, pos->side_to_move());

            Sample sample;
            fill_sample(*pos, sample);

            float scratch[2][kHidden];
            const double reference = forward(model, sample, scratch) * kOutputScale;
            difference += std::abs(cp - reference);
            magnitude += std::abs(reference);
            ++count;
        }

        std::printf("quantized against float: mean difference %.2fcp over %zu positions (mean "
                    "magnitude %.1fcp)\n",
                    difference / static_cast<double>(std::max<size_t>(1, count)), count,
                    magnitude / static_cast<double>(std::max<size_t>(1, count)));
    }

    std::printf("best held-out loss %.5f, written to %s\n", best_held, out_path.c_str());

    // The network as C++ source, for `src/nnue_net.inc`, 12 numbers to a line.
    if (args.has("inc") && net) {
        std::ofstream inc(args.get("inc", ""));
        inc << "// A network written by tools/train_nnue --inc: " << kInputs << " inputs, "
            << kHidden << " values for each side,\n// " << kBuckets << " output layers.\n\n";

        auto numbers = [&inc](const char *name, const std::vector<int> &values) {
            inc << "constexpr int16_t " << name << "[] = {\n";

            for (size_t i = 0; i < values.size(); ++i) {
                inc << (i % 12 == 0 ? "   " : "") << ' ' << values[i] << ',';
                inc << ((i % 12 == 11 || i + 1 == values.size()) ? "\n" : "");
            }

            inc << "};\n\n";
        };

        std::vector<int> weights;

        for (int i = 0; i < kInputs; ++i) {
            for (int h = 0; h < kHidden; ++h) {
                weights.push_back(net->feature_weights[i][h]);
            }
        }

        std::vector<int> bias(net->feature_bias, net->feature_bias + kHidden);
        std::vector<int> output;

        for (int bucket = 0; bucket < kBuckets; ++bucket) {
            for (int side = 0; side < 2; ++side) {
                for (int h = 0; h < kHidden; ++h) {
                    output.push_back(net->output_weights[bucket][side][h]);
                }
            }
        }

        numbers("kBuiltinFeatureWeights", weights);
        numbers("kBuiltinFeatureBias", bias);
        numbers("kBuiltinOutputWeights", output);
        inc << "constexpr int32_t kBuiltinOutputBias[] = {";

        for (int bucket = 0; bucket < kBuckets; ++bucket) {
            inc << (bucket == 0 ? "" : ", ") << net->output_bias[bucket];
        }

        inc << "};\n";
        std::printf("source written to %s\n", args.get("inc", "").c_str());
    }
    return 0;
}
