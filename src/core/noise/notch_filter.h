// notch_filter.h - Notch filter for removing specific frequency interference
#ifndef AUDIOT_NOTCH_FILTER_H
#define AUDIOT_NOTCH_FILTER_H

#include <vector>
#include <cstddef>

namespace audiot {

// Second-order IIR notch filter to eliminate a specific frequency
// (e.g., 50Hz/60Hz AC mains hum and its harmonics)
class NotchFilter {
public:
    // Default constructor (passthrough, call updateCoefficients before use)
    NotchFilter();

    // Create a notch filter
    // freq: frequency to remove (Hz)
    // sampleRate: sampling rate
    // qFactor: quality factor (higher = narrower notch)
    NotchFilter(float freq, float sampleRate, float qFactor = 10.0f);

    // Process a single sample
    float process(float input);

    // Process a batch of samples (in-place)
    void process(float* data, size_t count);

    // Process a batch of samples (return new vector)
    std::vector<float> process(const std::vector<float>& input);

    // Reset filter state
    void reset();

    // Update target frequency (for real-time tracking)
    void setFrequency(float freq);

private:
    // Second-order IIR notch filter coefficients
    float b0_, b1_, b2_, a1_, a2_;
    float x1_, x2_, y1_, y2_;  // State variables

    float sampleRate_;
    float qFactor_;

    void updateCoefficients(float freq, float sampleRate, float qFactor);
};

// Multi-stage notch filter (removes fundamental frequency and its harmonics)
class HarmonicNotchFilter {
public:
    HarmonicNotchFilter(float fundamentalFreq, float sampleRate,
                        int numHarmonics = 8, float qFactor = 10.0f);

    float process(float input);
    std::vector<float> process(const std::vector<float>& input);
    void reset();

    // Update fundamental frequency (all harmonics update automatically)
    void setFundamentalFrequency(float freq);

private:
    std::vector<NotchFilter> notches_;
    float sampleRate_;
    float qFactor_;
    int numHarmonics_;
};

} // namespace audiot
#endif // AUDIOT_NOTCH_FILTER_H
