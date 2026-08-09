#ifndef AUDIOT_FFT_WRAPPER_H
#define AUDIOT_FFT_WRAPPER_H

#include <vector>
#include <complex>
#include <cstddef>

// Forward declare KissFFT types to avoid including C header in public interface
struct kiss_fft_state;
typedef struct kiss_fft_state* kiss_fft_cfg;

namespace audiot {

// FFT wrapper around KissFFT (float precision)
// Provides forward (time->freq) and inverse (freq->time) transforms
class FFTWrapper {
public:
    // Construct with specified FFT size
    explicit FFTWrapper(size_t fft_size);

    // Destructor frees KissFFT configs
    ~FFTWrapper();

    // Non-copyable
    FFTWrapper(const FFTWrapper&) = delete;
    FFTWrapper& operator=(const FFTWrapper&) = delete;

    // Forward transform: time domain -> frequency domain
    void forward(const std::vector<float>& time_domain,
                 std::vector<std::complex<float>>& freq_domain);

    // Inverse transform: frequency domain -> time domain
    void inverse(const std::vector<std::complex<float>>& freq_domain,
                 std::vector<float>& time_domain);

    // Get the FFT size
    size_t fftSize() const;

private:
    size_t fft_size_;
    kiss_fft_cfg cfg_forward_;
    kiss_fft_cfg cfg_inverse_;
};

} // namespace audiot
#endif
