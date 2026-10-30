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

#include "nnue.hpp"

#include <cstddef>
#include <iterator>

#if __has_include("nnue_net.inc")
#define CHESS_HAS_BUILTIN_NETWORK 1
#endif

namespace chess::nnue {

#ifdef CHESS_HAS_BUILTIN_NETWORK

    namespace {

#include "nnue_net.inc"

    }  // namespace

    std::shared_ptr<const Network> builtin() {
        static const std::shared_ptr<const Network> network =
            []() -> std::shared_ptr<const Network> {
            // A build with another width (CHESS_NNUE_HIDDEN) cannot hold this network.
            if (std::size(kBuiltinFeatureBias) != static_cast<size_t>(kHidden)) {
                return nullptr;
            }

            auto net = std::make_shared<Network>();

            for (int i = 0; i < kInputs; ++i) {
                for (int h = 0; h < kHidden; ++h) {
                    net->feature_weights[i][h] =
                        kBuiltinFeatureWeights[static_cast<size_t>(i) * kHidden + h];
                }
            }

            for (int h = 0; h < kHidden; ++h) {
                net->feature_bias[h] = kBuiltinFeatureBias[h];
            }

            for (int bucket = 0; bucket < kBuckets; ++bucket) {
                const size_t base = static_cast<size_t>(bucket) * 2 * kHidden;

                for (int h = 0; h < kHidden; ++h) {
                    net->output_weights[bucket][0][h] = kBuiltinOutputWeights[base + h];
                    net->output_weights[bucket][1][h] = kBuiltinOutputWeights[base + kHidden + h];
                }

                net->output_bias[bucket] = kBuiltinOutputBias[bucket];
            }

            return std::shared_ptr<const Network>(net);
        }();
        return network;
    }

#else

    std::shared_ptr<const Network> builtin() {
        return nullptr;
    }

#endif

}  // namespace chess::nnue
