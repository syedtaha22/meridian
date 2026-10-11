// Times move generation: perft throughput, legal move generation, make_move and the
// slider attack tables. Writes a JSON report.
//
// Usage: bench_rules [--perft FILE] [--cpu N] [--reps N] [--out FILE]

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "chess/attacks.hpp"
#include "chess/movegen.hpp"
#include "common.hpp"

namespace {

    struct PerftEntry {
        std::string name;
        std::string fen;
        std::vector<uint64_t> counts;
    };

    std::vector<PerftEntry> read_perft(const std::string &path) {
        std::ifstream in(path);
        std::vector<PerftEntry> entries;
        std::string line;

        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') {
                continue;
            }

            std::istringstream fields(line);
            PerftEntry entry;
            std::string counts;
            std::getline(fields, entry.name, '\t');
            std::getline(fields, entry.fen, '\t');
            std::getline(fields, counts, '\t');
            std::istringstream parts(counts);
            std::string part;

            while (std::getline(parts, part, ',')) {
                entry.counts.push_back(std::stoull(part));
            }

            entries.push_back(entry);
        }

        return entries;
    }

    /**
     * Deterministic occupancies for the attack table microbenchmark.
     */
    std::vector<chess::Bitboard> random_occupancies(size_t n) {
        std::vector<chess::Bitboard> out(n);
        uint64_t state = 0x2545F4914F6CDD1DULL;

        for (auto &occ : out) {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;

            const uint64_t a = state;

            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            occ = a & state;
        }

        return out;
    }

}  // namespace

int main(int argc, char **argv) {
    using namespace chess;
    const bench::Args args(argc, argv);
    const std::string perft_file = args.get("perft", "bench/positions/perft.txt");
    const int reps = args.get_int("reps", 7);
    const std::string out_path = args.get("out", "");

    if (!bench::pin_to_cpu(args.get_int("cpu", -1))) {
        std::cerr << "warning: could not pin to the requested CPU\n";
    }

    std::ostringstream json;
    json << "{\n";
#ifdef CHESS_RAY_ATTACKS
    json << "  \"attacks\": \"rays\",\n";
#else
    json << "  \"attacks\": \"magic\",\n";
#endif

    // Perft: the shallowest depth with at least two million leaves, so each run takes
    // tens of milliseconds and start-up cost is negligible.
    json << "  \"perft\": [\n";
    std::vector<double> all_nps;
    const auto entries = read_perft(perft_file);

    for (size_t e = 0; e < entries.size(); ++e) {
        const PerftEntry &entry = entries[e];
        size_t depth_index = 0;

        while (depth_index + 1 < entry.counts.size() && entry.counts[depth_index] < 2000000) {
            ++depth_index;
        }

        const int depth = static_cast<int>(depth_index) + 1;
        const Position pos = *Position::from_fen(entry.fen);

        std::vector<double> seconds;
        uint64_t nodes = 0;

        for (int r = 0; r < reps; ++r) {
            const double start = bench::now_seconds();
            nodes = perft(pos, depth);
            seconds.push_back(bench::now_seconds() - start);
        }

        std::vector<double> nps;

        for (double s : seconds) {
            nps.push_back(static_cast<double>(nodes) / s);
        }

        const bench::Summary summary = bench::summarize(nps);
        all_nps.push_back(summary.median);
        const bool correct = nodes == entry.counts[depth_index];
        json << "    {\"name\": " << bench::quote(entry.name) << ", \"depth\": " << depth
             << ", \"nodes\": " << nodes << ", \"correct\": " << (correct ? "true" : "false")
             << ", \"seconds\": " << bench::array(seconds, 4)
             << ", \"nps\": " << bench::summary_json(summary, 0) << "}"
             << (e + 1 < entries.size() ? "," : "") << "\n";
        std::printf("perft %-12s depth %d  %12llu nodes  %7.2f Mnps%s\n", entry.name.c_str(), depth,
                    static_cast<unsigned long long>(nodes), summary.median / 1e6,
                    correct ? "" : "  WRONG COUNT");
    }

    json << "  ],\n";
    json << "  \"perft_nps_median_of_positions\": "
         << bench::num(bench::summarize(all_nps).median, 0) << ",\n";

    // Legal move generation and make_move over the positions the suite uses.
    const auto suite = bench::read_suite(args.get("suite", "bench/positions/suite.txt"));
    std::vector<Position> positions;

    for (const auto &entry : suite) {
        positions.push_back(*Position::from_fen(entry.fen));
    }
    if (!positions.empty()) {
        const int rounds = 20000;
        std::vector<double> gen_ns;
        std::vector<double> make_ns;

        for (int r = 0; r < reps; ++r) {
            uint64_t moves = 0;
            double start = bench::now_seconds();

            for (int i = 0; i < rounds; ++i) {
                for (const Position &p : positions) {
                    MoveList list;
                    generate_legal(p, list);
                    moves += static_cast<uint64_t>(list.size());
                }
            }

            double seconds = bench::now_seconds() - start;
            bench::keep(moves);
            gen_ns.push_back(seconds * 1e9 / (static_cast<double>(rounds) * positions.size()));

            // make_move on every pseudo-legal move, which is what search does.
            uint64_t made = 0;
            uint64_t attempts = 0;
            start = bench::now_seconds();

            for (int i = 0; i < rounds / 4; ++i) {
                for (const Position &p : positions) {
                    MoveList list;
                    generate_pseudo_legal(p, list);

                    for (const Move m : list) {
                        Position next = p;
                        made += next.make_move(m);
                        ++attempts;
                    }
                }
            }

            seconds = bench::now_seconds() - start;
            bench::keep(made);
            make_ns.push_back(seconds * 1e9 / static_cast<double>(attempts));
        }

        json << "  \"legal_movegen_ns_per_position\": "
             << bench::summary_json(bench::summarize(gen_ns), 1) << ",\n";
        json << "  \"copy_make_ns_per_move\": " << bench::summary_json(bench::summarize(make_ns), 2)
             << ",\n";
        std::printf("legal move generation  %8.1f ns per position\n",
                    bench::summarize(gen_ns).median);
        std::printf("copy + make_move       %8.2f ns per move\n", bench::summarize(make_ns).median);
    }

    // Slider attack lookups: ray walking against magic bitboards, on identical occupancies.
    const auto occupancies = random_occupancies(4096);
    auto time_lookup = [&](auto &&function) {
        std::vector<double> ns;

        for (int r = 0; r < reps; ++r) {
            uint64_t sink = 0;
            const int rounds = 200;
            const double start = bench::now_seconds();

            for (int i = 0; i < rounds; ++i) {
                for (size_t j = 0; j < occupancies.size(); ++j) {
                    sink += function(static_cast<Square>(j & 63), occupancies[j]);
                }
            }

            const double seconds = bench::now_seconds() - start;
            bench::keep(sink);
            ns.push_back(seconds * 1e9 / (rounds * static_cast<double>(occupancies.size())));
        }

        return bench::summarize(ns);
    };

    const auto rook_rays =
        time_lookup([](Square s, Bitboard o) { return rook_attacks_rays(s, o); });
    const auto rook_table = time_lookup([](Square s, Bitboard o) { return rook_attacks(s, o); });
    const auto bishop_rays =
        time_lookup([](Square s, Bitboard o) { return bishop_attacks_rays(s, o); });
    const auto bishop_table =
        time_lookup([](Square s, Bitboard o) { return bishop_attacks(s, o); });
    json << "  \"slider_ns_per_lookup\": {\n"
         << "    \"rook_rays\": " << bench::summary_json(rook_rays, 2) << ",\n"
         << "    \"rook_dispatch\": " << bench::summary_json(rook_table, 2) << ",\n"
         << "    \"bishop_rays\": " << bench::summary_json(bishop_rays, 2) << ",\n"
         << "    \"bishop_dispatch\": " << bench::summary_json(bishop_table, 2) << "\n  }\n}\n";
    std::printf("rook lookup   rays %6.2f ns   dispatch %6.2f ns\n", rook_rays.median,
                rook_table.median);
    std::printf("bishop lookup rays %6.2f ns   dispatch %6.2f ns\n", bishop_rays.median,
                bishop_table.median);

    if (!out_path.empty()) {
        std::ofstream(out_path) << json.str();
    }
    return 0;
}
