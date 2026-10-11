#!/usr/bin/env bash
# Prints a JSON description of the machine, toolchain and source state of a run.
#
# Usage: environment.sh [CPU]
#   CPU  the CPU the benchmarks are pinned to (default 8)

set -euo pipefail

cpu="${1:-8}"
repo="$(cd "$(dirname "$0")/../.." && pwd)"

read_file() { cat "$1" 2>/dev/null | tr -d '\n' || true; }
json_escape() { printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g'; }

model="$(grep -m1 'model name' /proc/cpuinfo | sed 's/.*: //')"
flags="$(grep -m1 '^flags' /proc/cpuinfo | sed 's/.*: //')"
interesting=""
for flag in avx avx2 fma bmi2 popcnt avx512f; do
    case " $flags " in *" $flag "*) interesting="$interesting\"$flag\", " ;; esac
done
interesting="[${interesting%, }]"

compiler="$(${CXX:-g++} --version 2>/dev/null | head -1 || true)"
cache="$repo/backend/build/CMakeCache.txt"
build_type=""
cxx_flags=""
if [ -f "$cache" ]; then
    build_type="$(grep -m1 '^CMAKE_BUILD_TYPE' "$cache" | sed 's/.*=//')"
    cxx_flags="$(grep -m1 '^CMAKE_CXX_FLAGS_RELEASE' "$cache" | sed 's/.*=//')"
fi
head="$(git -C "$repo" rev-parse HEAD 2>/dev/null || true)"
dirty=false
if [ -n "$(git -C "$repo" status --porcelain 2>/dev/null)" ]; then dirty=true; fi

cpufreq="/sys/devices/system/cpu/cpu$cpu/cpufreq"

cat <<JSON
{
  "timestamp": "$(date --iso-8601=seconds)",
  "cpu_model": "$(json_escape "$model")",
  "cpu_flags": $interesting,
  "pinned_cpu": $cpu,
  "pinned_cpu_max_khz": "$(read_file "$cpufreq/cpuinfo_max_freq")",
  "pinned_cpu_cur_khz_at_start": "$(read_file "$cpufreq/scaling_cur_freq")",
  "governor": "$(read_file "$cpufreq/scaling_governor")",
  "load_average": "$(cut -d' ' -f1-3 /proc/loadavg)",
  "logical_cpus": $(nproc --all),
  "kernel": "$(uname -r)",
  "compiler": "$(json_escape "$compiler")",
  "cmake_build_type": "$build_type",
  "cmake_cxx_flags_release": "$(json_escape "$cxx_flags")",
  "git_head": "$head",
  "git_dirty": $dirty
}
JSON
