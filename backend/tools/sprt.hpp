#pragma once

// A sequential probability ratio test on the results of games played in pairs.

#include <algorithm>
#include <array>
#include <cmath>

namespace sprt {

    /**
     * The expected score of a player that is `elo` points stronger than its opponent.
     */
    inline double score_of(double elo) {
        return 1 / (1 + std::pow(10, -elo / 400));
    }

    /**
     * Counts of the results of pairs of games. Each opening is played twice with the colors
     * swapped, and a pair scores 0, 0.5, 1, 1.5 or 2 points for the engine under test.
     * `pairs[k]` counts the pairs that scored k half points.
     */
    struct PairCounts {
        std::array<double, 5> pairs{};

        /**
         * Adds a pair from the results of its two games, each 0 for a loss, 1 for a draw and
         * 2 for a win.
         */
        void add(int first, int second) {
            pairs[static_cast<size_t>(first + second)] += 1;
        }

        double total() const {
            double sum = 0;

            for (double count : pairs) {
                sum += count;
            }

            return sum;
        }
    };

    /**
     * @brief The log likelihood ratio of "the engine is `elo1` stronger" against "it is `elo0`
     *        stronger".
     *
     * Uses the normal approximation of the mean score of a pair, with the variance of the pair
     * scores, so that the correlation between the two games of an opening is included.
     *
     * @param counts Results of the pairs so far.
     * @param elo0 Elo difference of the null hypothesis.
     * @param elo1 Elo difference of the alternative.
     * @return The ratio, or 0 when there is no pair.
     */
    inline double llr(const PairCounts &counts, double elo0, double elo1) {
        const double n = counts.total();

        if (n <= 0) {
            return 0;
        }

        // Every category gets half a pair. Without it, the variance of the first pairs is far
        // too small, and the test stops early on noise (simulated: 36% false positives).
        std::array<double, 5> c = counts.pairs;

        for (double &count : c) {
            count += 0.5;
        }

        double total = 0;
        double mean = 0;

        for (size_t k = 0; k < 5; ++k) {
            total += c[k];
            mean += c[k] * static_cast<double>(k) / 4;
        }

        mean /= total;
        double variance = 0;

        for (size_t k = 0; k < 5; ++k) {
            const double x = static_cast<double>(k) / 4 - mean;
            variance += c[k] * x * x;
        }

        variance /= total;

        const double s0 = score_of(elo0);
        const double s1 = score_of(elo1);

        return n * (s1 - s0) * (2 * mean - s0 - s1) / (2 * variance);
    }

    /**
     * The result of a test.
     */
    enum class Decision { kContinue, kAcceptH0, kAcceptH1 };

    /**
     * @brief Compares the ratio with the stopping bounds of the test.
     *
     * @param value The log likelihood ratio.
     * @param alpha Probability of accepting H1 when H0 is true.
     * @param beta Probability of accepting H0 when H1 is true.
     */
    inline Decision decide(double value, double alpha, double beta) {
        if (value >= std::log((1 - beta) / alpha)) {
            return Decision::kAcceptH1;
        }

        if (value <= std::log(beta / (1 - alpha))) {
            return Decision::kAcceptH0;
        }

        return Decision::kContinue;
    }

}  // namespace sprt
