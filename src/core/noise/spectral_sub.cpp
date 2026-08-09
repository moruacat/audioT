// spectral_sub.cpp - Spectral subtraction noise reduction implementation
#include "noise/spectral_sub.h"
#include "dsp/fft_wrapper.h"
#include "dsp/window.h"
#include <cmath>
#include <algorithm>
#include <complex>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

namespace audiot {

struct SpectralSubtractor::Impl {
    FFTWrapper* fft;
    std::vector<float> window;          // Analysis/synthesis window
    std::vector<float> overlapBuffer;   // Overlap-add accumulation buffer
    int fftSize;
    int hopSize;
    bool overlapReady;                  // Whether overlapBuffer has valid data

    Impl(int size)
        : fft(nullptr), fftSize(size), hopSize(size / 2), overlapReady(false)
    {
        fft = new FFTWrapper(static_cast<size_t>(size));
        window.resize(static_cast<size_t>(size));
        overlapBuffer.resize(static_cast<size_t>(size), 0.0f);

        // Generate Hamming window
        for (int i = 0; i < size; ++i) {
            window[static_cast<size_t>(i)] = 0.54f - 0.46f *
                std::cos(2.0f * static_cast<float>(M_PI) *
                         static_cast<float>(i) / static_cast<float>(size - 1));
        }
    }

    ~Impl() {
        delete fft;
    }
};

SpectralSubtractor::SpectralSubtractor(int fftSize, int sampleRate)
    : fftSize_(fftSize)
    , sampleRate_(sampleRate)
    , hopSize_(fftSize / 2)
    , impl_(nullptr)
    , hasProfile_(false)
    , inputPos_(0)
    , alpha_(2.0f)
    , beta_(0.02f)
{
    impl_ = new Impl(fftSize);
    inputBuffer_.resize(static_cast<size_t>(fftSize), 0.0f);
    outputBuffer_.resize(static_cast<size_t>(fftSize), 0.0f);
    noiseProfile_.resize(static_cast<size_t>(fftSize / 2 + 1), 0.0f);
}

SpectralSubtractor::~SpectralSubtractor() {
    delete impl_;
}

void SpectralSubtractor::learnNoiseProfile(const std::vector<float>& noiseSamples) {
    if (noiseSamples.empty()) return;

    size_t specSize = static_cast<size_t>(fftSize_ / 2 + 1);
    std::fill(noiseProfile_.begin(), noiseProfile_.end(), 0.0f);

    std::vector<float> frame(static_cast<size_t>(fftSize_), 0.0f);
    std::vector<std::complex<float> > spectrum(static_cast<size_t>(fftSize_));

    int numFrames = 0;
    size_t pos = 0;

    // Average magnitude spectrum over multiple noise frames
    while (pos + static_cast<size_t>(fftSize_) <= noiseSamples.size()) {
        // Copy frame and apply window
        for (int i = 0; i < fftSize_; ++i) {
            frame[static_cast<size_t>(i)] = noiseSamples[pos + static_cast<size_t>(i)] *
                                             impl_->window[static_cast<size_t>(i)];
        }

        // Forward FFT
        impl_->fft->forward(frame, spectrum);

        // Accumulate magnitude spectrum
        for (size_t k = 0; k < specSize; ++k) {
            noiseProfile_[k] += std::abs(spectrum[k]);
        }

        ++numFrames;
        pos += static_cast<size_t>(hopSize_);
    }

    // Average
    if (numFrames > 0) {
        float inv = 1.0f / static_cast<float>(numFrames);
        for (size_t k = 0; k < specSize; ++k) {
            noiseProfile_[k] *= inv;
        }
        hasProfile_ = true;
    }
}

void SpectralSubtractor::processFrame(float* frame) {
    if (!hasProfile_) return;

    size_t specSize = static_cast<size_t>(fftSize_ / 2 + 1);

    // Apply analysis window
    std::vector<float> windowed(static_cast<size_t>(fftSize_));
    for (int i = 0; i < fftSize_; ++i) {
        windowed[static_cast<size_t>(i)] = frame[i] * impl_->window[static_cast<size_t>(i)];
    }

    // Forward FFT
    std::vector<std::complex<float> > spectrum(static_cast<size_t>(fftSize_));
    impl_->fft->forward(windowed, spectrum);

    // Spectral subtraction on magnitude
    for (size_t k = 0; k < specSize; ++k) {
        float mag = std::abs(spectrum[k]);
        float noiseMag = noiseProfile_[k];

        // Subtract noise spectrum with over-subtraction factor
        float cleanMag = mag - alpha_ * noiseMag;

        // Apply spectral floor
        float floor = beta_ * noiseMag;
        if (cleanMag < floor) {
            cleanMag = floor;
        }

        // Reconstruct complex spectrum with original phase
        if (mag > 1e-10f) {
            float scale = cleanMag / mag;
            spectrum[k] *= scale;
        } else {
            spectrum[k] = std::complex<float>(0.0f, 0.0f);
        }
    }

    // Mirror spectrum for negative frequencies (real signal)
    for (size_t k = specSize; k < static_cast<size_t>(fftSize_); ++k) {
        spectrum[k] = std::conj(spectrum[static_cast<size_t>(fftSize_) - k]);
    }

    // Inverse FFT
    std::vector<float> output(static_cast<size_t>(fftSize_));
    impl_->fft->inverse(spectrum, output);

    // Apply synthesis window and overlap-add
    for (int i = 0; i < fftSize_; ++i) {
        float val = output[static_cast<size_t>(i)] * impl_->window[static_cast<size_t>(i)];
        impl_->overlapBuffer[static_cast<size_t>(i)] += val;
    }
}

float SpectralSubtractor::process(float input) {
    // Add sample to input buffer
    inputBuffer_[static_cast<size_t>(inputPos_)] = input;
    inputPos_++;

    // When we have a full frame, process it
    if (inputPos_ >= hopSize_) {
        // Shift input buffer: move last fftSize samples to beginning
        // (overlap from previous frame)
        processFrame(inputBuffer_.data());

        // Shift buffer left by hopSize
        int remaining = fftSize_ - hopSize_;
        if (remaining > 0) {
            for (int i = 0; i < remaining; ++i) {
                inputBuffer_[static_cast<size_t>(i)] =
                    inputBuffer_[static_cast<size_t>(i + hopSize_)];
            }
        }
        inputPos_ = remaining;

        // Fill remaining with zeros
        for (int i = inputPos_; i < fftSize_; ++i) {
            inputBuffer_[static_cast<size_t>(i)] = 0.0f;
        }
    }

    // Read output from overlap buffer (first sample)
    float output = impl_->overlapBuffer[0];

    // Shift overlap buffer left by hopSize
    int bufSize = fftSize_;
    for (int i = 0; i < bufSize - hopSize_; ++i) {
        impl_->overlapBuffer[static_cast<size_t>(i)] =
            impl_->overlapBuffer[static_cast<size_t>(i + hopSize_)];
    }
    for (int i = bufSize - hopSize_; i < bufSize; ++i) {
        impl_->overlapBuffer[static_cast<size_t>(i)] = 0.0f;
    }

    return output;
}

std::vector<float> SpectralSubtractor::process(const std::vector<float>& input) {
    std::vector<float> output(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        output[i] = process(input[i]);
    }
    return output;
}

void SpectralSubtractor::setOverSubtraction(float alpha) {
    alpha_ = std::max(1.0f, std::min(4.0f, alpha));
}

void SpectralSubtractor::setSpectralFloor(float beta) {
    beta_ = std::max(0.0f, std::min(0.1f, beta));
}

void SpectralSubtractor::reset() {
    std::fill(inputBuffer_.begin(), inputBuffer_.end(), 0.0f);
    std::fill(outputBuffer_.begin(), outputBuffer_.end(), 0.0f);
    std::fill(impl_->overlapBuffer.begin(), impl_->overlapBuffer.end(), 0.0f);
    inputPos_ = 0;
    hasProfile_ = false;
    std::fill(noiseProfile_.begin(), noiseProfile_.end(), 0.0f);
}

} // namespace audiot
