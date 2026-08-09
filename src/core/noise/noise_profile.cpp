// noise_profile.cpp - Noise profile management implementation
#include "noise_profile.h"
#include "../dsp/fft_wrapper.h"
#include "../dsp/window.h"

#include <fstream>
#include <cmath>
#include <cstring>
#include <complex>

namespace audiot {

// Binary profile file layout:
//   [magic "ANPF"(4)] [version u32] [sampleRate i32] [count u32] [float32 x count]
static const char kProfileMagic[4] = { 'A', 'N', 'P', 'F' };
static const uint32_t kProfileVersion = 1;

NoiseProfile::NoiseProfile()
    : sampleRate_(0)
    , rms_(0.0f)
    , peak_(0.0f)
{
}

bool NoiseProfile::createFromAudio(const std::vector<float>& noiseSamples,
                                   int sampleRate) {
    if (noiseSamples.empty() || sampleRate <= 0) {
        return false;
    }

    samples_ = noiseSamples;
    sampleRate_ = sampleRate;

    // RMS and peak levels
    double sumSq = 0.0;
    float peak = 0.0f;
    for (size_t i = 0; i < samples_.size(); ++i) {
        const float s = samples_[i];
        sumSq += static_cast<double>(s) * s;
        const float a = s < 0.0f ? -s : s;
        if (a > peak) peak = a;
    }
    rms_ = static_cast<float>(std::sqrt(sumSq / samples_.size()));
    peak_ = peak;
    return true;
}

bool NoiseProfile::saveToFile(const std::string& filePath) const {
    if (!isValid()) return false;

    std::ofstream out(filePath, std::ios::binary);
    if (!out.is_open()) return false;

    out.write(kProfileMagic, 4);
    const uint32_t version = kProfileVersion;
    const int32_t rate = sampleRate_;
    const uint32_t count = static_cast<uint32_t>(samples_.size());
    out.write(reinterpret_cast<const char*>(&version), sizeof(version));
    out.write(reinterpret_cast<const char*>(&rate), sizeof(rate));
    out.write(reinterpret_cast<const char*>(&count), sizeof(count));
    out.write(reinterpret_cast<const char*>(samples_.data()),
              static_cast<std::streamsize>(samples_.size() * sizeof(float)));
    return out.good();
}

bool NoiseProfile::loadFromFile(const std::string& filePath) {
    std::ifstream in(filePath, std::ios::binary);
    if (!in.is_open()) return false;

    char magic[4] = { 0, 0, 0, 0 };
    in.read(magic, 4);
    if (!in.good() || std::memcmp(magic, kProfileMagic, 4) != 0) {
        return false;
    }

    uint32_t version = 0;
    int32_t rate = 0;
    uint32_t count = 0;
    in.read(reinterpret_cast<char*>(&version), sizeof(version));
    in.read(reinterpret_cast<char*>(&rate), sizeof(rate));
    in.read(reinterpret_cast<char*>(&count), sizeof(count));
    if (!in.good() || version != kProfileVersion || rate <= 0 ||
        count == 0 || count > (1u << 28)) {
        return false;
    }

    std::vector<float> data(count);
    in.read(reinterpret_cast<char*>(data.data()),
            static_cast<std::streamsize>(count * sizeof(float)));
    if (!in.good()) return false;

    return createFromAudio(data, rate);
}

int NoiseProfile::sampleRate() const { return sampleRate_; }
size_t NoiseProfile::sampleCount() const { return samples_.size(); }

float NoiseProfile::duration() const {
    if (sampleRate_ <= 0) return 0.0f;
    return static_cast<float>(samples_.size()) / sampleRate_;
}

const std::vector<float>& NoiseProfile::noiseSamples() const {
    return samples_;
}

float NoiseProfile::rmsLevel() const { return rms_; }
float NoiseProfile::peakLevel() const { return peak_; }

float NoiseProfile::spectralCentroid() const {
    if (!isValid() || samples_.size() < 512) return 0.0f;

    // Average magnitude spectrum over 2048-sample Hann-windowed frames,
    // then compute the spectral centroid: sum(f_k * |X_k|) / sum(|X_k|)
    const size_t fftSize = 2048;
    const size_t hop = fftSize / 2;
    FFTWrapper fft(fftSize);
    std::vector<float> window = generate_hann(fftSize);

    std::vector<double> magnitude(fftSize / 2 + 1, 0.0);
    size_t frames = 0;

    std::vector<float> frame(fftSize);
    std::vector<std::complex<float>> spectrum;
    for (size_t start = 0; start + fftSize <= samples_.size(); start += hop) {
        for (size_t i = 0; i < fftSize; ++i) {
            frame[i] = samples_[start + i] * window[i];
        }
        fft.forward(frame, spectrum);
        for (size_t b = 0; b <= fftSize / 2; ++b) {
            magnitude[b] += std::abs(spectrum[b]);
        }
        ++frames;
        if (frames >= 32) break; // cap the work for long recordings
    }
    if (frames == 0) return 0.0f;

    const double binHz = static_cast<double>(sampleRate_) / fftSize;
    double weighted = 0.0;
    double total = 0.0;
    for (size_t b = 0; b <= fftSize / 2; ++b) {
        weighted += magnitude[b] * (b * binHz);
        total += magnitude[b];
    }
    if (total <= 0.0) return 0.0f;
    return static_cast<float>(weighted / total);
}

bool NoiseProfile::isValid() const {
    return !samples_.empty() && sampleRate_ > 0;
}

void NoiseProfile::clear() {
    samples_.clear();
    sampleRate_ = 0;
    rms_ = 0.0f;
    peak_ = 0.0f;
}

} // namespace audiot
