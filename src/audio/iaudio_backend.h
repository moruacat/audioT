// iaudio_backend.h - Platform audio backend abstraction layer (Phase 4)
#ifndef AUDIOT_IAUDIO_BACKEND_H
#define AUDIOT_IAUDIO_BACKEND_H

#include <string>
#include <vector>
#include <functional>
#include <memory>

namespace audiot {

// 音频后端抽象，为跨平台移植提供统一接口
class IAudioBackend {
public:
    virtual ~IAudioBackend() = default;

    // 设备枚举
    struct DeviceInfo {
        std::string id;
        std::string name;
        bool isInput;
        int defaultSampleRate;
    };
    virtual std::vector<DeviceInfo> enumerateDevices() = 0;

    // 音频流控制
    virtual bool startInput(int sampleRate, int channels, int bufferSize) = 0;
    virtual bool startOutput(int sampleRate, int channels, int bufferSize) = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;

    // 回调设置
    using InputCallback = std::function<void(const float* samples, int count)>;
    using OutputCallback = std::function<void(float* buffer, int count)>;
    virtual void setInputCallback(InputCallback cb) = 0;
    virtual void setOutputCallback(OutputCallback cb) = 0;

    // 数据写入（输出）
    virtual void write(const float* samples, int count) = 0;
};

// 工厂方法：根据平台创建合适的后端
// Windows: QtMultimedia 后端（参见 qt_audio_stream.h，UI 直接使用 Qt 流）
// Linux:   ALSA/PulseAudio 后端（参见 alsa_audio_stream.h）
// macOS:   CoreAudio 后端（规划中）
// 返回值：平台无可用原生后端时返回 nullptr（调用方可回退到 Qt 或 WAV 文件）
std::unique_ptr<IAudioBackend> createAudioBackend();

} // namespace audiot

#endif // AUDIOT_IAUDIO_BACKEND_H
