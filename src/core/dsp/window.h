#ifndef AUDIOT_WINDOW_H
#define AUDIOT_WINDOW_H

#include <vector>
#include <cmath>
#include <cstddef>

namespace audiot {

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// Rectangular window (no windowing, passthrough)
inline void apply_rectangular(std::vector<float>& data) {
    // Rectangular window: all ones, no modification needed
    (void)data;
}

// Hamming window: w(n) = 0.54 - 0.46 * cos(2*pi*n / (N-1))
inline void apply_hamming(std::vector<float>& data) {
    const size_t N = data.size();
    if (N <= 1) return;
    for (size_t n = 0; n < N; ++n) {
        float w = 0.54f - 0.46f * std::cos(2.0f * static_cast<float>(M_PI) * static_cast<float>(n) / static_cast<float>(N - 1));
        data[n] *= w;
    }
}

// Hann window: w(n) = 0.5 * (1 - cos(2*pi*n / (N-1)))
inline void apply_hann(std::vector<float>& data) {
    const size_t N = data.size();
    if (N <= 1) return;
    for (size_t n = 0; n < N; ++n) {
        float w = 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(M_PI) * static_cast<float>(n) / static_cast<float>(N - 1)));
        data[n] *= w;
    }
}

// Blackman window: w(n) = 0.42 - 0.5*cos(2*pi*n/(N-1)) + 0.08*cos(4*pi*n/(N-1))
inline void apply_blackman(std::vector<float>& data) {
    const size_t N = data.size();
    if (N <= 1) return;
    const float pi2 = 2.0f * static_cast<float>(M_PI);
    const float pi4 = 4.0f * static_cast<float>(M_PI);
    const float Nm1 = static_cast<float>(N - 1);
    for (size_t n = 0; n < N; ++n) {
        float nf = static_cast<float>(n);
        float w = 0.42f - 0.5f * std::cos(pi2 * nf / Nm1) + 0.08f * std::cos(pi4 * nf / Nm1);
        data[n] *= w;
    }
}

// Generate a Hamming window of given size
inline std::vector<float> generate_hamming(size_t size) {
    std::vector<float> w(size, 1.0f);
    apply_hamming(w);
    return w;
}

// Generate a Hann window of given size
inline std::vector<float> generate_hann(size_t size) {
    std::vector<float> w(size, 1.0f);
    apply_hann(w);
    return w;
}

// Generate a Blackman window of given size
inline std::vector<float> generate_blackman(size_t size) {
    std::vector<float> w(size, 1.0f);
    apply_blackman(w);
    return w;
}

} // namespace audiot
#endif
