// alsa_audio_stream.h - Linux ALSA audio backend prototype (Phase 4)
//
// 仅在 Linux 平台提供完整实现（#ifdef __linux__）。
// 在其他平台上头文件仍可包含，但 AlsaAudioBackend 不可用；
// createAudioBackend() 会返回 nullptr 或平台对应的后端。
#ifndef AUDIOT_ALSA_AUDIO_STREAM_H
#define AUDIOT_ALSA_AUDIO_STREAM_H

#include "iaudio_backend.h"

#include <string>
#include <vector>

#ifdef __linux__
// ALSA 前置声明，避免在头文件中暴露 <alsa/asoundlib.h>
typedef struct _snd_pcm snd_pcm_t;
#endif

namespace audiot {

// ALSA 音频后端：基于 snd_pcm 的 PCM 输入/输出封装
// Linux: 完整实现（需要 -lasound 链接）
// 其他平台: 类定义仍存在但构造即失败（isAvailable() == false）
class AlsaAudioBackend : public IAudioBackend {
public:
    AlsaAudioBackend();
    ~AlsaAudioBackend() override;

    // 当前平台是否可用（非 Linux 恒为 false）
    static bool isAvailable();

    std::vector<DeviceInfo> enumerateDevices() override;

    bool startInput(int sampleRate, int channels, int bufferSize) override;
    bool startOutput(int sampleRate, int channels, int bufferSize) override;
    void stop() override;
    bool isRunning() const override;

    void setInputCallback(InputCallback cb) override;
    void setOutputCallback(OutputCallback cb) override;

    void write(const float* samples, int count) override;

    // 选择设备名（如 "default" / "hw:0,0"），须在 start 前调用
    void setDeviceName(const std::string& name);

private:
#ifdef __linux__
    // 打开 PCM 句柄；input=true 为捕获流，否则为播放流
    bool openPcm(int sampleRate, int channels, int bufferSize, bool input);
    void closePcm();

    // 采集线程主体：循环 snd_pcm_readi -> float 转换 -> 回调
    void inputLoop();
    // 播放线程主体：回调填充 / write() 缓冲 -> snd_pcm_writei
    void outputLoop();

    snd_pcm_t* pcm_;
    bool isInput_;
#endif

    std::string deviceName_;
    int sampleRate_;
    int channels_;
    bool running_;
    InputCallback inputCb_;
    OutputCallback outputCb_;
};

} // namespace audiot

#endif // AUDIOT_ALSA_AUDIO_STREAM_H
