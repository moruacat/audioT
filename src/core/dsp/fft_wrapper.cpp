#include "dsp/fft_wrapper.h"

extern "C" {
#include "kiss_fft.h"
}

namespace audiot {

FFTWrapper::FFTWrapper(size_t fft_size)
    : fft_size_(fft_size)
    , cfg_forward_(nullptr)
    , cfg_inverse_(nullptr)
{
    cfg_forward_ = kiss_fft_alloc(static_cast<int>(fft_size_), 0, nullptr, nullptr);
    cfg_inverse_ = kiss_fft_alloc(static_cast<int>(fft_size_), 1, nullptr, nullptr);
}

FFTWrapper::~FFTWrapper() {
    if (cfg_forward_) {
        kiss_fft_free(cfg_forward_);
    }
    if (cfg_inverse_) {
        kiss_fft_free(cfg_inverse_);
    }
}

void FFTWrapper::forward(const std::vector<float>& time_domain,
                         std::vector<std::complex<float>>& freq_domain)
{
    // Prepare complex input (imaginary parts = 0)
    std::vector<kiss_fft_cpx> fin(fft_size_);
    std::vector<kiss_fft_cpx> fout(fft_size_);

    size_t count = (time_domain.size() < fft_size_) ? time_domain.size() : fft_size_;
    for (size_t i = 0; i < count; ++i) {
        fin[i].r = time_domain[i];
        fin[i].i = 0.0f;
    }
    // Zero-pad if input is shorter than FFT size
    for (size_t i = count; i < fft_size_; ++i) {
        fin[i].r = 0.0f;
        fin[i].i = 0.0f;
    }

    kiss_fft(cfg_forward_, fin.data(), fout.data());

    // Convert to std::complex<float>
    freq_domain.resize(fft_size_);
    for (size_t i = 0; i < fft_size_; ++i) {
        freq_domain[i] = std::complex<float>(fout[i].r, fout[i].i);
    }
}

void FFTWrapper::inverse(const std::vector<std::complex<float>>& freq_domain,
                         std::vector<float>& time_domain)
{
    std::vector<kiss_fft_cpx> fin(fft_size_);
    std::vector<kiss_fft_cpx> fout(fft_size_);

    size_t count = (freq_domain.size() < fft_size_) ? freq_domain.size() : fft_size_;
    for (size_t i = 0; i < count; ++i) {
        fin[i].r = freq_domain[i].real();
        fin[i].i = freq_domain[i].imag();
    }
    for (size_t i = count; i < fft_size_; ++i) {
        fin[i].r = 0.0f;
        fin[i].i = 0.0f;
    }

    kiss_fft(cfg_inverse_, fin.data(), fout.data());

    // KissFFT inverse output needs scaling by 1/N
    const float scale = 1.0f / static_cast<float>(fft_size_);
    time_domain.resize(fft_size_);
    for (size_t i = 0; i < fft_size_; ++i) {
        time_domain[i] = fout[i].r * scale;
    }
}

size_t FFTWrapper::fftSize() const {
    return fft_size_;
}

} // namespace audiot
