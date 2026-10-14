// Times the static evaluation, in nanoseconds per call, over the suite positions.
//
// Usage: bench_eval [--suite FILE] [--cpu N] [--reps N] [--out FILE]

#include <fstream>
#include <iostream>
#include <vector>

#include "common.hpp"
#include "eval.hpp"

int main(int argc, char **argv) {
    using namespace chess;
    const bench::Args args(argc, argv);
    const auto suite = bench::read_suite(args.get("suite", "bench/positions/suite.txt"));
    const int reps = args.get_int("reps", 9);

    if (!bench::pin_to_cpu(args.get_int("cpu", -1))) {
        std::cerr << "warning: could not pin to the requested CPU\n";
    }

    std::vector<Position> positions;

    for (const auto &entry : suite) {
        positions.push_back(*Position::from_fen(entry.fen));
    }

    const int rounds = 200000;
    std::vector<double> ns;

    for (int r = 0; r < reps; ++r) {
        int64_t sink = 0;
        const double start = bench::now_seconds();

        for (int i = 0; i < rounds; ++i) {
            sink += evaluate(positions[static_cast<size_t>(i) % positions.size()]);
        }

        const double seconds = bench::now_seconds() - start;
        bench::keep(sink);
        ns.push_back(seconds * 1e9 / rounds);
    }

    const bench::Summary summary = bench::summarize(ns);
    std::printf("evaluate  %6.1f ns per call (median of %zu, cv %.1f%%)\n", summary.median,
                summary.n, summary.cv_percent);

    const std::string out = args.get("out", "");

    if (!out.empty()) {
        std::ofstream(out) << "{\n  \"evaluate_ns\": " << bench::summary_json(summary, 1)
                           << "\n}\n";
    }
    return 0;
}
