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

#pragma once

// Helpers shared by the benchmark programs: pinning, timing, statistics, suites.

#include <sched.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace bench {

    /**
     * @brief Pins the calling thread to one CPU, so that runs do not migrate.
     *
     * @param cpu CPU number; negative leaves the thread unpinned.
     * @return True when the thread is pinned or no pin was asked for.
     */
    inline bool pin_to_cpu(int cpu) {
        if (cpu < 0) {
            return true;
        }

        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(cpu, &set);
        return sched_setaffinity(0, sizeof(set), &set) == 0;
    }

    /**
     * Monotonic time in seconds.
     */
    inline double now_seconds() {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    /**
     * Summary of repeated measurements.
     */
    struct Summary {
        size_t n = 0;
        double median = 0;
        double minimum = 0;
        double p25 = 0;
        double p75 = 0;
        double mean = 0;
        double cv_percent = 0;
    };

    /**
     * @brief Summarizes samples: median, minimum, quartiles, mean and coefficient of variation.
     *
     * @param samples Measurements; not empty.
     * @return The summary.
     */
    inline Summary summarize(std::vector<double> samples) {
        Summary s;
        std::sort(samples.begin(), samples.end());
        s.n = samples.size();
        s.minimum = samples.front();
        auto quantile = [&samples](double q) {
            const double position = q * static_cast<double>(samples.size() - 1);
            const size_t low = static_cast<size_t>(std::floor(position));
            const size_t high = std::min(low + 1, samples.size() - 1);
            return samples[low] + (samples[high] - samples[low]) * (position - low);
        };

        s.median = quantile(0.5);
        s.p25 = quantile(0.25);
        s.p75 = quantile(0.75);
        double sum = 0;

        for (double x : samples) {
            sum += x;
        }

        s.mean = sum / static_cast<double>(samples.size());
        double squares = 0;

        for (double x : samples) {
            squares += (x - s.mean) * (x - s.mean);
        }

        s.cv_percent = s.mean == 0 ? 0 : std::sqrt(squares / samples.size()) / s.mean * 100;
        return s;
    }

    /**
     * One line of a suite file.
     */
    struct Entry {
        std::string region;
        std::string name;
        std::string fen;
    };

    /**
     * @brief Reads a tab separated suite file: region, name, FEN.
     *
     * @param path File to read; lines starting with '#' are skipped.
     * @return The entries in file order.
     */
    inline std::vector<Entry> read_suite(const std::string &path) {
        std::ifstream in(path);
        std::vector<Entry> entries;
        std::string line;

        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') {
                continue;
            }

            std::istringstream fields(line);
            Entry entry;
            std::getline(fields, entry.region, '\t');
            std::getline(fields, entry.name, '\t');
            std::getline(fields, entry.fen, '\t');
            entries.push_back(entry);
        }

        return entries;
    }

    /**
     * Command line options of the form `--name value`, with defaults.
     */
    class Args {
    public:
        Args(int argc, char **argv) {
            for (int i = 1; i < argc; ++i) {
                values_.push_back(argv[i]);
            }
        }

        std::string get(const std::string &name, const std::string &fallback) const {
            for (size_t i = 0; i + 1 < values_.size(); ++i) {
                if (values_[i] == "--" + name) {
                    return values_[i + 1];
                }
            }
            return fallback;
        }

        int get_int(const std::string &name, int fallback) const {
            return std::stoi(get(name, std::to_string(fallback)));
        }

        bool has(const std::string &name) const {
            return std::find(values_.begin(), values_.end(), "--" + name) != values_.end();
        }

    private:
        std::vector<std::string> values_;
    };

    /**
     * A JSON number with a fixed precision.
     */
    inline std::string num(double value, int precision = 3) {
        std::ostringstream out;
        out << std::fixed << std::setprecision(precision) << value;
        return out.str();
    }

    /**
     * A JSON array of numbers.
     */
    inline std::string array(const std::vector<double> &values, int precision = 3) {
        std::string out = "[";

        for (size_t i = 0; i < values.size(); ++i) {
            out += (i ? ", " : "") + num(values[i], precision);
        }
        return out + "]";
    }

    /**
     * Quotes a string for JSON.
     */
    inline std::string quote(const std::string &text) {
        std::string out = "\"";

        for (char c : text) {
            if (c == '"' || c == '\\') {
                out += '\\';
            }

            out += c;
        }

        return out + "\"";
    }

    /**
     * A JSON object of summary statistics.
     */
    inline std::string summary_json(const Summary &s, int precision = 3) {
        return "{\"n\": " + std::to_string(s.n) + ", \"median\": " + num(s.median, precision) +
               ", \"min\": " + num(s.minimum, precision) + ", \"p25\": " + num(s.p25, precision) +
               ", \"p75\": " + num(s.p75, precision) + ", \"mean\": " + num(s.mean, precision) +
               ", \"cv_percent\": " + num(s.cv_percent, 2) + "}";
    }

    /**
     * Keeps a value alive so that the optimizer cannot delete the work producing it.
     */
    template <typename T> inline void keep(const T &value) {
        asm volatile("" : : "g"(&value) : "memory");
    }

}  // namespace bench
