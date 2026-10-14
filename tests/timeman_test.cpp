// Time management tests: the budget stays inside the clock and follows the clock.
#include <cstdio>
#include <string>

#include "timeman.hpp"

namespace {

    int failures = 0;

    void check(bool condition, const std::string &what) {
        if (!condition) {
            ++failures;
            std::printf("FAIL: %s\n", what.c_str());
        }
    }

    void test_invariants() {
        for (const int64_t time : {1, 5, 50, 200, 1000, 10000, 60000, 600000, 3600000}) {
            for (const int64_t increment : {0, 10, 1000, 30000}) {
                for (const int moves : {0, 1, 2, 10, 40, 100}) {
                    chess::Clock clock;
                    clock.time_ms = time;
                    clock.increment_ms = increment;
                    clock.moves_to_go = moves;
                    const chess::TimeBudget budget = chess::allocate_time(clock, 30);
                    const std::string name = "time " + std::to_string(time) + ", increment " +
                                             std::to_string(increment) + ", moves " +
                                             std::to_string(moves);
                    check(budget.soft_ms >= 1 && budget.hard_ms >= 1, "positive: " + name);
                    check(budget.soft_ms <= budget.hard_ms, "soft within hard: " + name);

                    // The hard limit leaves the move overhead, except when the clock is
                    // almost gone and one millisecond is the least that can be asked for.
                    if (time > 31) {
                        check(budget.hard_ms <= time - 30, "inside the clock: " + name);
                    }
                }
            }
        }
    }

    void test_scaling() {
        chess::Clock small;
        small.time_ms = 10000;
        chess::Clock large;
        large.time_ms = 100000;
        check(chess::allocate_time(large, 30).soft_ms > chess::allocate_time(small, 30).soft_ms,
              "more time gives a larger budget");

        chess::Clock plain;
        plain.time_ms = 60000;
        chess::Clock bonus = plain;
        bonus.increment_ms = 2000;
        check(chess::allocate_time(bonus, 30).soft_ms > chess::allocate_time(plain, 30).soft_ms,
              "an increment adds to the budget");

        chess::Clock many;
        many.time_ms = 60000;
        many.moves_to_go = 40;
        chess::Clock few = many;
        few.moves_to_go = 5;
        check(chess::allocate_time(few, 30).soft_ms > chess::allocate_time(many, 30).soft_ms,
              "fewer moves to go give a larger share");

        chess::Clock fixed;
        fixed.time_ms = 60000;
        fixed.move_time_ms = 500;
        const chess::TimeBudget by_move = chess::allocate_time(fixed, 30);
        check(by_move.hard_ms == 470 && by_move.soft_ms == 470, "a move time is used as given");

        chess::Clock sudden;
        sudden.time_ms = 60000;
        check(chess::allocate_time(sudden, 30).soft_ms == 59970 / 30,
              "sudden death spends 1/30 of the time");
    }

}  // namespace

int main() {
    test_invariants();
    test_scaling();

    if (failures == 0) {
        std::printf("all time management tests passed\n");
        return 0;
    }

    std::printf("%d failures\n", failures);
    return 1;
}
