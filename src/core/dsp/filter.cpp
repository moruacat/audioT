#include "dsp/filter.h"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

namespace audiot {

// ---- Sinc function helper ----
static float sinc(float x) {
    if (std::fabs(x) < 1e-6f) return 1.0f;
    return std::sin(x) / x;
}

// ---- Hamming window value at index n of N total taps ----
static float hamming_window(int n, int N) {
    return 0.54f - 0.46f * std::cos(2.0f * static_cast<float>(M_PI) * static_cast<float>(n) / static_cast<float>(N - 1));
}

// ---- FIR lowpass design using windowed sinc method ----
std::vector<float> Filter::design_fir_lowpass(float cutoff_freq, float sample_rate, int num_taps) {
    std::vector<float> coeffs(static_cast<size_t>(num_taps));
    float fc = cutoff_freq / sample_rate; // Normalized cutoff (0..0.5)
    int M = num_taps - 1;

    for (int n = 0; n < num_taps; ++n) {
        float x = static_cast<float>(n) - static_cast<float>(M) / 2.0f;
        // Ideal lowpass impulse response: 2*fc * sinc(2*pi*fc*x)
        coeffs[static_cast<size_t>(n)] = 2.0f * fc * sinc(2.0f * static_cast<float>(M_PI) * 2.0f * fc * x) * hamming_window(n, num_taps);
    }
    return coeffs;
}

// ---- FIR highpass design: spectral inversion of lowpass ----
std::vector<float> Filter::design_fir_highpass(float cutoff_freq, float sample_rate, int num_taps) {
    std::vector<float> coeffs = design_fir_lowpass(cutoff_freq, sample_rate, num_taps);
    int M = num_taps - 1;

    // Spectral inversion: negate all coefficients, then add 1 to center tap
    for (size_t i = 0; i < coeffs.size(); ++i) {
        coeffs[i] = -coeffs[i];
    }
    coeffs[static_cast<size_t>(M / 2)] += 1.0f;
    return coeffs;
}

// ---- FIR bandpass design ----
std::vector<float> Filter::design_fir_bandpass(float low_freq, float high_freq, float sample_rate, int num_taps) {
    // Bandpass = lowpass(high) - lowpass(low), or equivalently:
    // bandpass = lowpass(high) + highpass(low)
    std::vector<float> lp_high = design_fir_lowpass(high_freq, sample_rate, num_taps);
    std::vector<float> lp_low  = design_fir_lowpass(low_freq, sample_rate, num_taps);

    std::vector<float> coeffs(static_cast<size_t>(num_taps));
    for (int i = 0; i < num_taps; ++i) {
        coeffs[static_cast<size_t>(i)] = lp_high[static_cast<size_t>(i)] - lp_low[static_cast<size_t>(i)];
    }
    return coeffs;
}

// ---- Apply FIR filter via direct convolution ----
std::vector<float> Filter::apply_fir(const std::vector<float>& input, const std::vector<float>& coefficients) {
    if (coefficients.empty()) return input;

    size_t in_len = input.size();
    size_t coeff_len = coefficients.size();
    size_t out_len = in_len + coeff_len - 1;
    std::vector<float> output(out_len, 0.0f);

    for (size_t i = 0; i < in_len; ++i) {
        for (size_t j = 0; j < coeff_len; ++j) {
            output[i + j] += input[i] * coefficients[j];
        }
    }
    return output;
}

// ---- BiquadSection implementation ----

Filter::BiquadSection::BiquadSection()
    : b0(0), b1(0), b2(0)
    , a1(0), a2(0)
    , x1(0), x2(0), y1(0), y2(0)
{
}

void Filter::BiquadSection::reset() {
    x1 = x2 = y1 = y2 = 0.0f;
}

float Filter::BiquadSection::process(float input) {
    // Direct Form I: y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2]
    float output = b0 * input + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1;
    x1 = input;
    y2 = y1;
    y1 = output;
    return output;
}

// ---- Design notch biquad (A-weighting style) ----
// Removes a narrow band around the specified frequency
Filter::BiquadSection Filter::design_notch(float freq, float sample_rate, float q_factor) {
    BiquadSection bq;
    float w0 = 2.0f * static_cast<float>(M_PI) * freq / sample_rate;
    float cosw0 = std::cos(w0);
    float sinw0 = std::sin(w0);
    float alpha = sinw0 / (2.0f * q_factor);

    float a0_inv = 1.0f / (1.0f + alpha);

    bq.b0 =  1.0f * a0_inv;
    bq.b1 = -2.0f * cosw0 * a0_inv;
    bq.b2 =  1.0f * a0_inv;
    bq.a1 = -2.0f * cosw0 * a0_inv;
    bq.a2 =  (1.0f - alpha) * a0_inv;

    return bq;
}

// ---- Design bandpass biquad (constant skirt gain) ----
Filter::BiquadSection Filter::design_bandpass_biquad(float center_freq, float sample_rate, float q_factor) {
    BiquadSection bq;
    float w0 = 2.0f * static_cast<float>(M_PI) * center_freq / sample_rate;
    float cosw0 = std::cos(w0);
    float sinw0 = std::sin(w0);
    float alpha = sinw0 / (2.0f * q_factor);

    float a0_inv = 1.0f / (1.0f + alpha);

    bq.b0 =  alpha * a0_inv;
    bq.b1 =  0.0f;
    bq.b2 = -alpha * a0_inv;
    bq.a1 = -2.0f * cosw0 * a0_inv;
    bq.a2 =  (1.0f - alpha) * a0_inv;

    return bq;
}

} // namespace audiot
