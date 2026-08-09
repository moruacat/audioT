#ifndef AUDIOT_FILTER_H
#define AUDIOT_FILTER_H

#include <vector>
#include <cstddef>

namespace audiot {

// Digital filter design and application utilities
// Supports FIR filter design (window method) and IIR biquad sections
class Filter {
public:
    // ---- FIR filter design (window method, Hamming window) ----

    // Design FIR lowpass filter coefficients
    static std::vector<float> design_fir_lowpass(float cutoff_freq, float sample_rate, int num_taps);

    // Design FIR highpass filter coefficients
    static std::vector<float> design_fir_highpass(float cutoff_freq, float sample_rate, int num_taps);

    // Design FIR bandpass filter coefficients
    static std::vector<float> design_fir_bandpass(float low_freq, float high_freq, float sample_rate, int num_taps);

    // Apply FIR filter to input signal (direct form, convolution)
    static std::vector<float> apply_fir(const std::vector<float>& input, const std::vector<float>& coefficients);

    // ---- IIR biquad section (second-order section) ----

    // Biquad section: y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2]
    struct BiquadSection {
        float b0, b1, b2;
        float a1, a2;
        float x1, x2, y1, y2; // State variables (delay elements)

        BiquadSection();

        // Reset all state variables to zero
        void reset();

        // Process a single sample through the biquad
        float process(float input);
    };

    // Design a notch (band-reject) biquad filter to remove a specific frequency
    static BiquadSection design_notch(float freq, float sample_rate, float q_factor);

    // Design a bandpass biquad filter centered at a specific frequency
    static BiquadSection design_bandpass_biquad(float center_freq, float sample_rate, float q_factor);
};

} // namespace audiot
#endif
