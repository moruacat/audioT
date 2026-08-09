// noise_profile.h - Noise profile management
#ifndef AUDIOT_NOISE_PROFILE_H
#define AUDIOT_NOISE_PROFILE_H

#include <vector>
#include <string>
#include <cstddef>

namespace audiot {

// Manages a noise profile (background noise characterization)
// Created from a recording of noise-only audio
class NoiseProfile {
public:
    NoiseProfile();

    // Create noise profile from audio samples
    bool createFromAudio(const std::vector<float>& noiseSamples, int sampleRate);

    // Save/load noise profile to/from file
    bool saveToFile(const std::string& filePath) const;
    bool loadFromFile(const std::string& filePath);

    // Get profile info
    int sampleRate() const;
    size_t sampleCount() const;
    float duration() const;  // Seconds

    // Get raw noise samples
    const std::vector<float>& noiseSamples() const;

    // Get noise statistics
    float rmsLevel() const;         // RMS level
    float peakLevel() const;        // Peak level
    float spectralCentroid() const; // Spectral centroid (Hz)

    bool isValid() const;
    void clear();

private:
    std::vector<float> samples_;
    int sampleRate_;
    float rms_;
    float peak_;
};

} // namespace audiot
#endif // AUDIOT_NOISE_PROFILE_H
