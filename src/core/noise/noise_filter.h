// noise_filter.h - Adaptive noise filter using LMS/NLMS algorithm
#ifndef AUDIOT_NOISE_FILTER_H
#define AUDIOT_NOISE_FILTER_H

#include <vector>
#include <cstddef>

namespace audiot {

// Adaptive noise filter using LMS (Least Mean Squares) or NLMS (Normalized LMS)
// Adaptively cancels periodic noise by estimating and subtracting it
class AdaptiveNoiseFilter {
public:
    // Create adaptive filter
    // filterLength: filter order (number of taps)
    // mu: step size factor (controls convergence speed and stability)
    explicit AdaptiveNoiseFilter(int filterLength = 64, float mu = 0.01f);

    // Process a single sample
    float process(float input);

    // Process a batch of samples
    std::vector<float> process(const std::vector<float>& input);

    // Reset filter state and weights
    void reset();

    // Set step size
    void setStepSize(float mu);

    // Enable/disable NLMS mode (Normalized LMS, more stable)
    void setNLMS(bool enable);

private:
    std::vector<float> weights_;  // Filter weights
    std::vector<float> buffer_;   // Input buffer (delay line)
    int filterLength_;
    float mu_;
    bool nlms_;
    int bufferPos_;
};

} // namespace audiot
#endif // AUDIOT_NOISE_FILTER_H
