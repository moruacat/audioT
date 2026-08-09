// resampler.cpp - Linear-interpolation audio resampler implementation
#include "dsp/resampler.h"

#include <cmath>
#include <cstddef>

namespace audiot {

Resampler::Resampler(int inputRate, int outputRate)
    : inputRate_(inputRate)
    , outputRate_(outputRate)
    , ratio_(1.0)
    , position_(0.0)
    , lastSample_(0.0f)
    , hasLastSample_(false)
{
    // ratio_ = inputRate / outputRate
    // For output sample i, the corresponding input position is pos = i * ratio_
    if (inputRate_ > 0 && outputRate_ > 0) {
        ratio_ = static_cast<double>(inputRate_) / static_cast<double>(outputRate_);
    }
}

std::vector<float> Resampler::resample(const std::vector<float>& input)
{
    std::vector<float> output;
    if (input.empty()) {
        return output;
    }
    if (input.size() == 1) {
        output.push_back(input[0]);
        return output;
    }

    // Last valid output index: i such that i*ratio_ <= size-2 (needs a next sample)
    const double lastPos = static_cast<double>(input.size() - 1);
    const size_t outCount = static_cast<size_t>(std::floor(lastPos / ratio_)) + 1;
    output.reserve(outCount);

    for (size_t i = 0; i < outCount; ++i) {
        const double pos = static_cast<double>(i) * ratio_;
        size_t i0 = static_cast<size_t>(std::floor(pos));
        if (i0 + 1 >= input.size()) {
            i0 = input.size() - 2;
        }
        const double frac = pos - std::floor(pos);
        const float s0 = input[i0];
        const float s1 = input[i0 + 1];
        output.push_back(static_cast<float>(s0 + (s1 - s0) * frac));
    }
    return output;
}

std::vector<float> Resampler::process(const float* input, int count)
{
    std::vector<float> output;
    if (input == nullptr || count <= 0) {
        return output;
    }

    // Virtual sample access: index -1 maps to the previous block's last sample
    auto sampleAt = [&](int idx) -> float {
        if (idx < 0) {
            return hasLastSample_ ? lastSample_ : input[0];
        }
        return input[idx];
    };

    // Emit output samples while the interpolation position stays inside
    // this block (the last input sample is held back for next block's
    // interpolation, so we stop before reaching count-1).
    double pos = position_;
    while (pos < static_cast<double>(count - 1)) {
        const int i0 = static_cast<int>(std::floor(pos));
        const double frac = pos - std::floor(pos);
        const float s0 = sampleAt(i0);
        const float s1 = sampleAt(i0 + 1);
        output.push_back(static_cast<float>(s0 + (s1 - s0) * frac));
        pos += ratio_;
    }

    // Carry fractional remainder into the next block's coordinate system
    position_ = pos - static_cast<double>(count);
    lastSample_ = input[count - 1];
    hasLastSample_ = true;
    return output;
}

void Resampler::reset()
{
    position_ = 0.0;
    lastSample_ = 0.0f;
    hasLastSample_ = false;
}

int Resampler::inputRate() const { return inputRate_; }
int Resampler::outputRate() const { return outputRate_; }
double Resampler::ratio() const { return ratio_; }

} // namespace audiot
