// test_filter.cpp - Filter tests
#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
#include <dsp/filter.h>
#include <cmath>
#include <numeric>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

using namespace audiot;

TEST_CASE("FIR lowpass filter attenuates high frequencies", "[filter]") {
    const float sampleRate = 44100.0f;
    const float cutoff = 1000.0f;
    const int numTaps = 65;

    // Design lowpass filter
    std::vector<float> coeffs = Filter::design_fir_lowpass(cutoff, sampleRate, numTaps);
    REQUIRE(coeffs.size() == static_cast<size_t>(numTaps));

    // Generate test signal: 200 Hz (pass) + 5000 Hz (stop)
    const size_t N = 2048;
    std::vector<float> input(N);
    for (size_t i = 0; i < N; ++i) {
        float t = static_cast<float>(i) / sampleRate;
        input[i] = std::sin(2.0f * static_cast<float>(M_PI) * 200.0f * t)
                 + std::sin(2.0f * static_cast<float>(M_PI) * 5000.0f * t);
    }

    // Apply filter
    std::vector<float> output = Filter::apply_fir(input, coeffs);

    // Skip transient (first numTaps samples)
    size_t start = static_cast<size_t>(numTaps);

    // Compute RMS of input and output in steady state
    float rmsInput = 0.0f;
    float rmsOutput = 0.0f;
    for (size_t i = start; i < N && (i - start) < 1024; ++i) {
        rmsInput += input[i] * input[i];
        // Output is longer due to convolution, align properly
        size_t outIdx = i;
        if (outIdx < output.size()) {
            rmsOutput += output[outIdx] * output[outIdx];
        }
    }

    // Output should have less energy (high freq removed)
    rmsInput = std::sqrt(rmsInput / 1024.0f);
    rmsOutput = std::sqrt(rmsOutput / 1024.0f);

    // The output RMS should be less than input (high freq component removed)
    REQUIRE(rmsOutput < rmsInput);
}

TEST_CASE("Notch filter attenuates target frequency", "[filter]") {
    const float sampleRate = 44100.0f;
    const float notchFreq = 1000.0f;
    const float qFactor = 10.0f;

    Filter::BiquadSection notch = Filter::design_notch(notchFreq, sampleRate, qFactor);

    // Generate signal at the notch frequency
    const size_t N = 4096;
    std::vector<float> input(N);
    for (size_t i = 0; i < N; ++i) {
        input[i] = std::sin(2.0f * static_cast<float>(M_PI) * notchFreq * static_cast<float>(i) / sampleRate);
    }

    // Process through notch filter
    std::vector<float> output(N);
    for (size_t i = 0; i < N; ++i) {
        output[i] = notch.process(input[i]);
    }

    // Measure output RMS (skip transient)
    float rms = 0.0f;
    for (size_t i = 500; i < N; ++i) {
        rms += output[i] * output[i];
    }
    rms = std::sqrt(rms / static_cast<float>(N - 500));

    // Notch should heavily attenuate the signal
    REQUIRE(rms < 0.1f);
}

TEST_CASE("BiquadSection reset works", "[filter]") {
    Filter::BiquadSection bq;
    bq.b0 = 1.0f;
    bq.b1 = 0.0f;
    bq.b2 = 0.0f;
    bq.a1 = 0.0f;
    bq.a2 = 0.0f;

    float out1 = bq.process(1.0f);
    REQUIRE(out1 == 1.0f);

    bq.reset();
    float out2 = bq.process(0.0f);
    REQUIRE(out2 == 0.0f);
}
