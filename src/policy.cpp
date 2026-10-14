#include "policy.hpp"

namespace chess::policy {

    const int kWeights[kFeatures] = {
#include "policy_weights.inc"
    };

}  // namespace chess::policy
