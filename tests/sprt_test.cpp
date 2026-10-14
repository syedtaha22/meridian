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

// Checks the sequential test: the ratio against hand values, and the error rates of the test
// against simulated pairs of games.

#include <cmath>
#include <cstdio>
#include <random>

#include "../tools/sprt.hpp"

namespace {

    int failures = 0;

    void check(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++failures;
        }
    }

    /**
     * Plays one game with the given chance of a win and of a draw, as 0, 1 or 2.
     */
    int game(std::mt19937_64 &random, double win, double draw) {
        const double u = std::uniform_real_distribution<double>(0, 1)(random);

        if (u < win) {
            return 2;
        }

        return u < win + draw ? 1 : 0;
    }

    /**
     * Runs the test on games of a given true strength until it stops or reaches `max_pairs`.
     * Returns the decision.
     */
    sprt::Decision simulate(std::mt19937_64 &random, double true_elo, double draw_rate, double elo0,
                            double elo1, int max_pairs) {
        const double s = sprt::score_of(true_elo);
        const double win = s - draw_rate / 2;
        sprt::PairCounts counts;

        for (int pair = 0; pair < max_pairs; ++pair) {
            counts.add(game(random, win, draw_rate), game(random, win, draw_rate));

            const sprt::Decision decision = sprt::decide(sprt::llr(counts, elo0, elo1), 0.05, 0.05);

            if (decision != sprt::Decision::kContinue) {
                return decision;
            }
        }

        return sprt::Decision::kContinue;
    }

    void test_ratio() {
        sprt::PairCounts empty;
        check(sprt::llr(empty, 0, 20) == 0, "no pairs give a ratio of zero");

        // Mirroring the results and the hypotheses changes the sign of the ratio.
        sprt::PairCounts counts;
        counts.pairs = {3, 9, 20, 14, 6};
        sprt::PairCounts mirrored;
        mirrored.pairs = {6, 14, 20, 9, 3};
        check(std::abs(sprt::llr(counts, 0, 20) + sprt::llr(mirrored, -20, 0)) < 1e-9,
              "mirrored results give the opposite ratio");

        sprt::PairCounts strong;
        strong.pairs = {10, 20, 100, 200, 100};
        sprt::PairCounts weak;
        weak.pairs = {100, 200, 100, 20, 10};
        check(sprt::llr(strong, 0, 20) > 0, "a winning record favors H1");
        check(sprt::llr(weak, 0, 20) < 0, "a losing record favors H0");

        check(sprt::decide(3.0, 0.05, 0.05) == sprt::Decision::kAcceptH1, "upper bound is 2.94");
        check(sprt::decide(-3.0, 0.05, 0.05) == sprt::Decision::kAcceptH0, "lower bound is -2.94");
        check(sprt::decide(0.0, 0.05, 0.05) == sprt::Decision::kContinue, "zero continues");
    }

    void test_error_rates() {
        std::mt19937_64 random(12345);
        const int runs = 2000;
        const double draw_rate = 0.5;

        int false_positive = 0;
        int detected = 0;
        int wrongly_rejected = 0;

        for (int i = 0; i < runs; ++i) {
            // The engine is exactly as strong as H0 says, and H1 must rarely be accepted.
            false_positive +=
                simulate(random, 0, draw_rate, 0, 20, 20000) == sprt::Decision::kAcceptH1 ? 1 : 0;

            // The engine is as strong as H1 says, and H1 should usually be accepted.
            sprt::Decision d = simulate(random, 20, draw_rate, 0, 20, 20000);
            detected += d == sprt::Decision::kAcceptH1 ? 1 : 0;
            wrongly_rejected += d == sprt::Decision::kAcceptH0 ? 1 : 0;
        }

        std::printf("under H0: H1 accepted in %.1f%% of %d runs\n", 100.0 * false_positive / runs,
                    runs);
        std::printf("under H1: H1 accepted in %.1f%%, H0 accepted in %.1f%%\n",
                    100.0 * detected / runs, 100.0 * wrongly_rejected / runs);

        // The approximation is not exact, so the limits are looser than alpha and beta.
        check(false_positive <= runs * 0.075, "false positives stay near alpha = 5%");
        check(wrongly_rejected <= runs * 0.075, "false negatives stay near beta = 5%");
        check(detected >= runs * 0.9, "a real gain of H1 is found");
    }

}  // namespace

int main() {
    test_ratio();
    test_error_rates();

    if (failures == 0) {
        std::printf("all sprt tests passed\n");
        return 0;
    }

    return 1;
}
