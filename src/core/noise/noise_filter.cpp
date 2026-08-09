// noise_filter.cpp - Adaptive noise filter implementation (LMS/NLMS)
#include "noise/noise_filter.h"
#include <cmath>
#include <algorithm>
#include <numeric>

namespace audiot {

AdaptiveNoiseFilter::AdaptiveNoiseFilter(int filterLength, float mu)
    : filterLength_(filterLength)
    , mu_(mu)
    , nlms_(false)
    , bufferPos_(0)
{
    weights_.resize(static_cast<size_t>(filterLength), 0.0f);
    buffer_.resize(static_cast<size_t>(filterLength), 0.0f);
}

float AdaptiveNoiseFilter::process(float input) {
    // Store input in circular buffer
    buffer_[static_cast<size_t>(bufferPos_)] = input;

    // Compute filter output: y = sum(w[i] * buffer[i])
    // The buffer contains delayed versions of the input
    float y = 0.0f;
    for (int i = 0; i < filterLength_; ++i) {
        int idx = (bufferPos_ - i + filterLength_) % filterLength_;
        y += weights_[static_cast<size_t>(i)] * buffer_[static_cast<size_t>(idx)];
    }

    // Error signal: difference between input and filter estimate
    // For noise cancellation, the filter learns to predict periodic components
    float error = input - y;

    // Update weights using LMS or NLMS
    float power = 0.0f;
    if (nlms_) {
        // Compute input power for normalization
        for (int i = 0; i < filterLength_; ++i) {
            int idx = (bufferPos_ - i + filterLength_) % filterLength_;
            float val = buffer_[static_cast<size_t>(idx)];
            power += val * val;
        }
        // Add small epsilon to prevent division by zero
        power += 1e-6f;
    }

    float step = nlms_ ? (mu_ / power) : mu_;

    for (int i = 0; i < filterLength_; ++i) {
        int idx = (bufferPos_ - i + filterLength_) % filterLength_;
        weights_[static_cast<size_t>(i)] += step * error * buffer_[static_cast<size_t>(idx)];
    }

    // Advance buffer position
    bufferPos_ = (bufferPos_ + 1) % filterLength_;

    return error;
}

std::vector<float> AdaptiveNoiseFilter::process(const std::vector<float>& input) {
    std::vector<float> output(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        output[i] = process(input[i]);
    }
    return output;
}

void AdaptiveNoiseFilter::reset() {
    std::fill(weights_.begin(), weights_.end(), 0.0f);
    std::fill(buffer_.begin(), buffer_.end(), 0.0f);
    bufferPos_ = 0;
}

void AdaptiveNoiseFilter::setStepSize(float mu) {
    mu_ = mu;
}

void AdaptiveNoiseFilter::setNLMS(bool enable) {
    nlms_ = enable;
}

} // namespace audiot
