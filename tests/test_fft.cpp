// test_fft.cpp - FFT wrapper tests
#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
#include <dsp/fft_wrapper.h>
#include <cmath>
#include <complex>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

using namespace audiot;

TEST_CASE("FFT forward and inverse roundtrip", "[fft]") {
    const size_t N = 256;
    FFTWrapper fft(N);

    // Create a simple time-domain signal
    std::vector<float> input(N);
    for (size_t i = 0; i < N; ++i) {
        input[i] = std::sin(2.0f * static_cast<float>(M_PI) * 4.0f * static_cast<float>(i) / static_cast<float>(N));
    }

    // Forward transform
    std::vector<std::complex<float>> freq;
    fft.forward(input, freq);

    REQUIRE(freq.size() == N);

    // Inverse transform
    std::vector<float> output;
    fft.inverse(freq, output);

    REQUIRE(output.size() == N);

    // Verify roundtrip
    for (size_t i = 0; i < N; ++i) {
        REQUIRE(output[i] == Approx(input[i]).margin(1e-4f));
    }
}

TEST_CASE("FFT of known frequency sine wave", "[fft]") {
    const size_t N = 1024;
    FFTWrapper fft(N);

    // Generate 100 Hz sine wave at 44100 Hz sample rate
    const float sampleRate = 44100.0f;
    const float freq = 100.0f;
    std::vector<float> signal(N);
    for (size_t i = 0; i < N; ++i) {
        signal[i] = std::sin(2.0f * static_cast<float>(M_PI) * freq * static_cast<float>(i) / sampleRate);
    }

    std::vector<std::complex<float>> spectrum;
    fft.forward(signal, spectrum);

    // Find peak frequency bin
    float maxMag = 0.0f;
    size_t peakBin = 0;
    for (size_t i = 0; i < N / 2; ++i) {
        float mag = std::abs(spectrum[i]);
        if (mag > maxMag) {
            maxMag = mag;
            peakBin = i;
        }
    }

    // Expected bin: freq * N / sampleRate
    float expectedBin = freq * static_cast<float>(N) / sampleRate;
    REQUIRE(static_cast<int>(peakBin) == Approx(static_cast<int>(expectedBin)).margin(2));
}

TEST_CASE("FFT size is correct", "[fft]") {
    FFTWrapper fft(512);
    REQUIRE(fft.fftSize() == 512);
}
