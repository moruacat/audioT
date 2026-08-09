// notch_filter.cpp - Notch filter implementation
#include "noise/notch_filter.h"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

namespace audiot {

// ---- NotchFilter ----

NotchFilter::NotchFilter()
    : b0_(1.0f), b1_(0.0f), b2_(0.0f), a1_(0.0f), a2_(0.0f)
    , x1_(0.0f), x2_(0.0f), y1_(0.0f), y2_(0.0f)
    , sampleRate_(44100.0f), qFactor_(10.0f)
{
}

NotchFilter::NotchFilter(float freq, float sampleRate, float qFactor)
    : x1_(0.0f), x2_(0.0f), y1_(0.0f), y2_(0.0f)
    , sampleRate_(sampleRate), qFactor_(qFactor)
{
    updateCoefficients(freq, sampleRate, qFactor);
}

float NotchFilter::process(float input) {
    // Direct Form I: y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2]
    float output = b0_ * input + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_;
    x2_ = x1_;
    x1_ = input;
    y2_ = y1_;
    y1_ = output;
    return output;
}

void NotchFilter::process(float* data, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        data[i] = process(data[i]);
    }
}

std::vector<float> NotchFilter::process(const std::vector<float>& input) {
    std::vector<float> output(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        output[i] = process(input[i]);
    }
    return output;
}

void NotchFilter::reset() {
    x1_ = x2_ = y1_ = y2_ = 0.0f;
}

void NotchFilter::setFrequency(float freq) {
    updateCoefficients(freq, sampleRate_, qFactor_);
}

void NotchFilter::updateCoefficients(float freq, float sampleRate, float qFactor) {
    // Second-order IIR notch filter (Audio EQ cookbook formula)
    float w0 = 2.0f * static_cast<float>(M_PI) * freq / sampleRate;
    float cosw0 = std::cos(w0);
    float sinw0 = std::sin(w0);
    float alpha = sinw0 / (2.0f * qFactor);

    float a0_inv = 1.0f / (1.0f + alpha);

    b0_ =  1.0f * a0_inv;
    b1_ = -2.0f * cosw0 * a0_inv;
    b2_ =  1.0f * a0_inv;
    a1_ = -2.0f * cosw0 * a0_inv;  // note: same as b1_ for notch
    a2_ =  (1.0f - alpha) * a0_inv;
}

// ---- HarmonicNotchFilter ----

HarmonicNotchFilter::HarmonicNotchFilter(float fundamentalFreq, float sampleRate,
                                         int numHarmonics, float qFactor)
    : sampleRate_(sampleRate), qFactor_(qFactor), numHarmonics_(numHarmonics)
{
    notches_.reserve(static_cast<size_t>(numHarmonics));
    for (int i = 0; i < numHarmonics; ++i) {
        float harmonicFreq = fundamentalFreq * static_cast<float>(i + 1);
        // Only add harmonics below Nyquist
        if (harmonicFreq < sampleRate * 0.5f) {
            notches_.push_back(NotchFilter(harmonicFreq, sampleRate, qFactor));
        }
    }
}

float HarmonicNotchFilter::process(float input) {
    float result = input;
    for (size_t i = 0; i < notches_.size(); ++i) {
        result = notches_[i].process(result);
    }
    return result;
}

std::vector<float> HarmonicNotchFilter::process(const std::vector<float>& input) {
    std::vector<float> output = input;
    for (size_t i = 0; i < notches_.size(); ++i) {
        notches_[i].process(output.data(), output.size());
    }
    return output;
}

void HarmonicNotchFilter::reset() {
    for (size_t i = 0; i < notches_.size(); ++i) {
        notches_[i].reset();
    }
}

void HarmonicNotchFilter::setFundamentalFrequency(float freq) {
    for (size_t i = 0; i < notches_.size(); ++i) {
        float harmonicFreq = freq * static_cast<float>(i + 1);
        if (harmonicFreq < sampleRate_ * 0.5f) {
            notches_[i].setFrequency(harmonicFreq);
        }
    }
}

} // namespace audiot
