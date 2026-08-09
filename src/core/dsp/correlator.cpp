#include "dsp/correlator.h"
#include <cmath>
#include <algorithm>

namespace audiot {

Correlator::Correlator(const std::vector<float>& pattern)
    : pattern_(pattern)
    , pattern_energy_(0.0f)
{
    // Precompute pattern energy for normalization
    for (size_t i = 0; i < pattern_.size(); ++i) {
        pattern_energy_ += pattern_[i] * pattern_[i];
    }
    if (pattern_energy_ < 1e-12f) {
        pattern_energy_ = 1e-12f; // Avoid division by zero
    }
}

Correlator::CorrelationResult Correlator::find(const std::vector<float>& input, float threshold) {
    CorrelationResult result;
    result.offset = 0;
    result.peak_value = 0.0f;
    result.found = false;

    if (pattern_.empty() || input.size() < pattern_.size()) {
        return result;
    }

    size_t search_len = input.size() - pattern_.size() + 1;
    size_t pat_len = pattern_.size();

    float best_corr = -1e30f;
    size_t best_offset = 0;

    // Sliding window cross-correlation
    for (size_t i = 0; i < search_len; ++i) {
        float corr = 0.0f;
        float input_energy = 0.0f;

        for (size_t j = 0; j < pat_len; ++j) {
            corr += input[i + j] * pattern_[j];
            input_energy += input[i + j] * input[i + j];
        }

        // Normalize correlation: corr / sqrt(pattern_energy * input_energy)
        float denom = std::sqrt(pattern_energy_ * input_energy);
        float norm_corr = (denom > 1e-12f) ? (corr / denom) : 0.0f;

        if (norm_corr > best_corr) {
            best_corr = norm_corr;
            best_offset = i;
        }
    }

    result.offset = best_offset;
    result.peak_value = best_corr;
    result.found = (best_corr >= threshold);

    return result;
}

} // namespace audiot
