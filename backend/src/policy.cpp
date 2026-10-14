#include "chess/policy.hpp"

namespace chess::policy {

    const int kWeights[kFeatures] = {
#include "chess/policy_weights.inc"
    };

}  // namespace chess::policy
