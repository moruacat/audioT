// spectral_sub.h - Spectral subtraction noise reduction
#ifndef AUDIOT_SPECTRAL_SUB_H
#define AUDIOT_SPECTRAL_SUB_H

#include <vector>
#include <cstddef>

namespace audiot {

// Spectral subtraction noise reducer
// Subtracts estimated noise spectrum from the signal spectrum
// Uses overlap-add method for artifact-free reconstruction
class SpectralSubtractor {
public:
    // fftSize: FFT size (recommend 512 or 1024)
    // sampleRate: sampling rate
    SpectralSubtractor(int fftSize = 1024, int sampleRate = 44100);
    ~SpectralSubtractor();

    // Learn noise profile from noise-only audio samples
    void learnNoiseProfile(const std::vector<float>& noiseSamples);

    // Process a single sample (uses overlap-add internally)
    float process(float input);

    // Process a batch of samples
    std::vector<float> process(const std::vector<float>& input);

    // Set over-subtraction factor (prevents musical noise)
    void setOverSubtraction(float alpha);  // 1.0~4.0, default 2.0

    // Set spectral floor (prevents over-suppression)
    void setSpectralFloor(float beta);  // 0.0~0.1, default 0.02

    void reset();

private:
    int fftSize_;
    int sampleRate_;
    int hopSize_;  // Frame shift

    // Pimpl for FFT and internal buffers
    struct Impl;
    Impl* impl_;

    // Noise profile (average magnitude spectrum)
    std::vector<float> noiseProfile_;
    bool hasProfile_;

    // Overlap-add buffers
    std::vector<float> inputBuffer_;
    std::vector<float> outputBuffer_;
    int inputPos_;

    // Parameters
    float alpha_;  // Over-subtraction factor
    float beta_;   // Spectral floor

    void processFrame(float* frame);
};

} // namespace audiot
#endif // AUDIOT_SPECTRAL_SUB_H
