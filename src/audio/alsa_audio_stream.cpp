// alsa_audio_stream.cpp - Linux ALSA audio backend implementation
//
// 编译策略：
//   - __linux__  : 完整 ALSA 实现（snd_pcm），需链接 asound
//   - 其他平台   : 空实现（构造后 isAvailable() == false），保证跨平台可编译
#include "alsa_audio_stream.h"

#include <cstring>
#include <algorithm>

#ifdef __linux__
#include <alsa/asoundlib.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <memory>

namespace audiot {

bool AlsaAudioBackend::isAvailable() {
    return true;
}

AlsaAudioBackend::AlsaAudioBackend()
    : pcm_(nullptr)
    , isInput_(false)
    , deviceName_("default")
    , sampleRate_(44100)
    , channels_(1)
    , running_(false)
{
}

AlsaAudioBackend::~AlsaAudioBackend() {
    stop();
}

void AlsaAudioBackend::setDeviceName(const std::string& name) {
    deviceName_ = name;
}

std::vector<IAudioBackend::DeviceInfo> AlsaAudioBackend::enumerateDevices() {
    std::vector<DeviceInfo> devices;

    void** hints = nullptr;
    if (snd_device_name_hint(-1, "pcm", &hints) == 0) {
        for (void** h = hints; *h != nullptr; ++h) {
            char* name = snd_device_name_get_hint(*h, "NAME");
            char* desc = snd_device_name_get_hint(*h, "DESC");
            if (name != nullptr) {
                DeviceInfo info;
                info.id = name;
                info.name = desc != nullptr ? desc : name;
                info.isInput = true;  // ALSA 设备通常双向，具体由打开方式决定
                info.defaultSampleRate = 44100;
                devices.push_back(info);
            }
            free(name);
            free(desc);
        }
        snd_device_name_free_hint(hints);
    }
    return devices;
}

bool AlsaAudioBackend::openPcm(int sampleRate, int channels, int bufferSize, bool input) {
    closePcm();

    snd_pcm_stream_t stream = input ? SND_PCM_STREAM_CAPTURE : SND_PCM_STREAM_PLAYBACK;
    int err = snd_pcm_open(&pcm_, deviceName_.c_str(), stream, 0);
    if (err < 0) {
        pcm_ = nullptr;
        return false;
    }

    snd_pcm_hw_params_t* hw = nullptr;
    snd_pcm_hw_params_alloca(&hw);
    snd_pcm_hw_params_any(pcm_, hw);

    unsigned int rate = static_cast<unsigned int>(sampleRate);
    snd_pcm_uframes_t period = static_cast<snd_pcm_uframes_t>(bufferSize);

    bool ok =
        snd_pcm_hw_params_set_access(pcm_, hw, SND_PCM_ACCESS_RW_INTERLEAVED) >= 0 &&
        snd_pcm_hw_params_set_format(pcm_, hw, SND_PCM_FORMAT_S16_LE) >= 0 &&
        snd_pcm_hw_params_set_channels(pcm_, hw, static_cast<unsigned int>(channels)) >= 0 &&
        snd_pcm_hw_params_set_rate_near(pcm_, hw, &rate, nullptr) >= 0 &&
        snd_pcm_hw_params_set_period_size_near(pcm_, hw, &period, nullptr) >= 0 &&
        snd_pcm_hw_params(pcm_, hw) >= 0;

    if (!ok) {
        closePcm();
        return false;
    }

    sampleRate_ = sampleRate;
    channels_ = channels;
    return true;
}

void AlsaAudioBackend::closePcm() {
    if (pcm_ != nullptr) {
        snd_pcm_close(pcm_);
        pcm_ = nullptr;
    }
}

bool AlsaAudioBackend::startInput(int sampleRate, int channels, int bufferSize) {
    if (running_) {
        return false;
    }
    if (!openPcm(sampleRate, channels, bufferSize, true)) {
        return false;
    }
    isInput_ = true;
    running_ = true;
    if (snd_pcm_start(pcm_) < 0) {
        stop();
        return false;
    }
    std::thread(&AlsaAudioBackend::inputLoop, this).detach();
    return true;
}

bool AlsaAudioBackend::startOutput(int sampleRate, int channels, int bufferSize) {
    if (running_) {
        return false;
    }
    if (!openPcm(sampleRate, channels, bufferSize, false)) {
        return false;
    }
    isInput_ = false;
    running_ = true;
    std::thread(&AlsaAudioBackend::outputLoop, this).detach();
    return true;
}

void AlsaAudioBackend::stop() {
    running_ = false;
    // 给工作线程一点时间退出后再关闭句柄（原型级处理）
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    closePcm();
}

bool AlsaAudioBackend::isRunning() const {
    return running_;
}

void AlsaAudioBackend::setInputCallback(InputCallback cb) {
    inputCb_ = std::move(cb);
}

void AlsaAudioBackend::setOutputCallback(OutputCallback cb) {
    outputCb_ = std::move(cb);
}

void AlsaAudioBackend::write(const float* samples, int count) {
    if (pcm_ == nullptr || isInput_ || samples == nullptr || count <= 0) {
        return;
    }
    // float(-1..1) -> S16 直接写阻塞式播放
    std::vector<int16_t> pcmBuf(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        float s = std::max(-1.0f, std::min(1.0f, samples[i]));
        pcmBuf[static_cast<size_t>(i)] = static_cast<int16_t>(s * 32767.0f);
    }
    snd_pcm_sframes_t frames =
        snd_pcm_writei(pcm_, pcmBuf.data(),
                       static_cast<snd_pcm_uframes_t>(count / channels_));
    if (frames < 0) {
        snd_pcm_recover(pcm_, static_cast<int>(frames), 1);
    }
}

void AlsaAudioBackend::inputLoop() {
    const int frames = 1024;
    std::vector<int16_t> raw(static_cast<size_t>(frames * channels_));
    std::vector<float> floats(static_cast<size_t>(frames * channels_));

    while (running_) {
        snd_pcm_sframes_t n = snd_pcm_readi(pcm_, raw.data(),
                                            static_cast<snd_pcm_uframes_t>(frames));
        if (n < 0) {
            n = snd_pcm_recover(pcm_, static_cast<int>(n), 1);
            if (n < 0) {
                break;
            }
            continue;
        }
        if (n == 0) {
            continue;
        }
        int sampleCount = static_cast<int>(n) * channels_;
        for (int i = 0; i < sampleCount; ++i) {
            floats[static_cast<size_t>(i)] = raw[static_cast<size_t>(i)] / 32768.0f;
        }
        if (inputCb_) {
            inputCb_(floats.data(), sampleCount);
        }
    }
}

void AlsaAudioBackend::outputLoop() {
    const int frames = 1024;
    std::vector<float> buf(static_cast<size_t>(frames * channels_));
    std::vector<int16_t> raw(static_cast<size_t>(frames * channels_));

    while (running_) {
        if (!outputCb_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        int need = frames * channels_;
        std::fill(buf.begin(), buf.end(), 0.0f);
        outputCb_(buf.data(), need);

        for (int i = 0; i < need; ++i) {
            float s = std::max(-1.0f, std::min(1.0f, buf[static_cast<size_t>(i)]));
            raw[static_cast<size_t>(i)] = static_cast<int16_t>(s * 32767.0f);
        }
        snd_pcm_sframes_t n = snd_pcm_writei(pcm_, raw.data(),
                                             static_cast<snd_pcm_uframes_t>(frames));
        if (n < 0) {
            snd_pcm_recover(pcm_, static_cast<int>(n), 1);
        }
    }
}

// Linux 平台工厂实现
std::unique_ptr<IAudioBackend> createAudioBackend() {
    return std::unique_ptr<IAudioBackend>(new AlsaAudioBackend());
}

} // namespace audiot

#else  // !__linux__ : 空实现（Windows/macOS 编译桩）

namespace audiot {

bool AlsaAudioBackend::isAvailable() {
    return false;
}

AlsaAudioBackend::AlsaAudioBackend()
    : deviceName_("default")
    , sampleRate_(44100)
    , channels_(1)
    , running_(false)
{
}

AlsaAudioBackend::~AlsaAudioBackend() = default;

void AlsaAudioBackend::setDeviceName(const std::string& name) {
    deviceName_ = name;
}

std::vector<IAudioBackend::DeviceInfo> AlsaAudioBackend::enumerateDevices() {
    return std::vector<DeviceInfo>();
}

bool AlsaAudioBackend::startInput(int, int, int) {
    return false;
}

bool AlsaAudioBackend::startOutput(int, int, int) {
    return false;
}

void AlsaAudioBackend::stop() {
    running_ = false;
}

bool AlsaAudioBackend::isRunning() const {
    return running_;
}

void AlsaAudioBackend::setInputCallback(InputCallback cb) {
    inputCb_ = std::move(cb);
}

void AlsaAudioBackend::setOutputCallback(OutputCallback cb) {
    outputCb_ = std::move(cb);
}

void AlsaAudioBackend::write(const float*, int) {
}

// Windows: 实时音频由 QtMultimedia (qt_audio_stream.h) 提供，
// 此处返回 nullptr 表示无原生非-Qt 后端。
// macOS: 未来由 CoreAudio 后端实现。
std::unique_ptr<IAudioBackend> createAudioBackend() {
    return nullptr;
}

} // namespace audiot

#endif // __linux__
