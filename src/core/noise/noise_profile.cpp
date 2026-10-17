// noise_profile.cpp - Noise profile management implementation
#include "noise_profile.h"
#include "../dsp/fft_wrapper.h"
#include "../dsp/window.h"
#include "../util/file_utils.h"

#include <cmath>
#include <cstring>
#include <cstdint>
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

    FILE* out = openFile(filePath, "wb");
    if (!out) return false;

    fwrite(kProfileMagic, 1, 4, out);
    const uint32_t version = kProfileVersion;
    const int32_t rate = sampleRate_;
    const uint32_t count = static_cast<uint32_t>(samples_.size());
    fwrite(&version, sizeof(version), 1, out);
    fwrite(&rate, sizeof(rate), 1, out);
    fwrite(&count, sizeof(count), 1, out);
    const size_t written = fwrite(samples_.data(), sizeof(float), samples_.size(), out);
    const bool ok = (written == samples_.size());
    fclose(out);
    return ok;
}

bool NoiseProfile::loadFromFile(const std::string& filePath) {
    FILE* in = openFile(filePath, "rb");
    if (!in) return false;

    char magic[4] = { 0, 0, 0, 0 };
    if (fread(magic, 1, 4, in) != 4 || std::memcmp(magic, kProfileMagic, 4) != 0) {
        fclose(in);
        return false;
    }

    uint32_t version = 0;
    int32_t rate = 0;
    uint32_t count = 0;
    if (fread(&version, sizeof(version), 1, in) != 1 ||
        fread(&rate, sizeof(rate), 1, in) != 1 ||
        fread(&count, sizeof(count), 1, in) != 1 ||
        version != kProfileVersion || rate <= 0 ||
        count == 0 || count > (1u << 28)) {
        fclose(in);
        return false;
    }

    std::vector<float> data(count);
    const size_t got = fread(data.data(), sizeof(float), count, in);
    fclose(in);
    if (got != count) return false;

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
