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

// Times move selection across search configurations, depths and game phases.
//
// Each configuration is searched from an empty transposition table, one position at a
// time. Rounds interleave the configurations, so drift in clock speed or temperature
// affects all of them equally, and the reported time is the median over rounds.
//
// Writes move_selection.csv with one row per configuration, depth and position.
//
// Usage: bench_search --configs baseline,full --depths 1,2,3,4 [--suite FILE] [--cpu N]
//                     [--eval FILE] [--nnue FILE]
//                     [--rounds N] [--out FILE]

#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "common.hpp"
#include "movegen.hpp"
#include "search.hpp"

namespace {

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

    struct Sample {
        std::vector<double> ms;
        chess::SearchResult last;
    };

}  // namespace

int main(int argc, char **argv) {
    using namespace chess;
    const bench::Args args(argc, argv);
    const auto configs = split(args.get("configs", "baseline,full"), ',');
    std::vector<int> depths;

    for (const std::string &d : split(args.get("depths", "1,2,3,4"), ',')) {
        depths.push_back(std::stoi(d));
    }

    const auto suite = bench::read_suite(args.get("suite", "bench/positions/suite.txt"));
    const int rounds = args.get_int("rounds", 3);
    const std::string out_path = args.get("out", "move_selection.csv");
    const int hash_mb = args.get_int("hash", 16);
    std::shared_ptr<EvalParams> eval;
    std::shared_ptr<const nnue::Network> network;

    if (args.has("nnue")) {
        network = nnue::load(args.get("nnue", ""));

        if (!network) {
            std::cerr << "cannot read the network\n";
            return 1;
        }
    }

    if (args.has("eval")) {
        eval = std::make_shared<EvalParams>(EvalParams::defaults());

        if (!eval->load(args.get("eval", ""))) {
            std::cerr << "cannot read evaluation numbers\n";
            return 1;
        }
    }

    if (!bench::pin_to_cpu(args.get_int("cpu", -1))) {
        std::cerr << "warning: could not pin to the requested CPU\n";
    }

    using Key = std::tuple<std::string, int, size_t>;  // config, depth, position index
    std::map<Key, Sample> samples;

    for (int round = 0; round < rounds; ++round) {
        for (const int depth : depths) {
            for (const std::string &config : configs) {
                SearchOptions options = SearchOptions::preset(config, depth);
                options.hash_mb = static_cast<size_t>(hash_mb);
                options.eval_params = eval;
                if (network) {
                    options.nnue = network;
                }
                Searcher searcher(options);

                for (size_t i = 0; i < suite.size(); ++i) {
                    const Position pos = *Position::from_fen(suite[i].fen);

                    searcher.clear();
                    const SearchResult result = searcher.search(pos);

                    Sample &sample = samples[{config, depth, i}];
                    sample.ms.push_back(result.elapsed_ms);
                    sample.last = result;
                }
            }
        }

        std::fprintf(stderr, "round %d of %d done\n", round + 1, rounds);
    }

    std::ofstream csv(out_path);
    csv << "engine,config,depth,region,position,move,score_cp,nodes,quiescence_nodes,ms,nps,"
           "tt_hits,beta_cutoffs,first_move_cutoffs\n";
    for (const std::string &config : configs) {
        for (const int depth : depths) {
            for (size_t i = 0; i < suite.size(); ++i) {
                const Sample &sample = samples[{config, depth, i}];
                const double ms = bench::summarize(sample.ms).median;
                const SearchStats &st = sample.last.stats;
                csv << "cpp," << config << "," << depth << "," << suite[i].region << ","
                    << suite[i].name << "," << move_to_uci(sample.last.best_move) << ","
                    << sample.last.score << "," << st.nodes << "," << st.quiescence_nodes << ","
                    << bench::num(ms, 3) << ","
                    << bench::num(ms > 0 ? static_cast<double>(st.nodes) / ms * 1000.0 : 0, 0)
                    << "," << st.tt_hits << "," << st.beta_cutoffs << "," << st.first_move_cutoffs
                    << "\n";
            }
        }
    }

    // A summary to the terminal: mean milliseconds per move by configuration, depth, region.
    std::printf("%-12s %5s %-11s %10s %12s %10s\n", "config", "depth", "region", "ms/move",
                "nodes/move", "knps");
    for (const std::string &config : configs) {
        for (const int depth : depths) {
            std::map<std::string, std::vector<size_t>> by_region;

            for (size_t i = 0; i < suite.size(); ++i) {
                by_region[suite[i].region].push_back(i);
            }

            for (const auto &[region, indices] : by_region) {
                double ms = 0;
                double nodes = 0;

                for (size_t i : indices) {
                    const Sample &sample = samples[{config, depth, i}];
                    ms += bench::summarize(sample.ms).median;
                    nodes += static_cast<double>(sample.last.stats.nodes);
                }

                ms /= static_cast<double>(indices.size());
                nodes /= static_cast<double>(indices.size());
                std::printf("%-12s %5d %-11s %10.2f %12.0f %10.0f\n", config.c_str(), depth,
                            region.c_str(), ms, nodes, ms > 0 ? nodes / ms : 0.0);
            }
        }
    }

    return 0;
}
