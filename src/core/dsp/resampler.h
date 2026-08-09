// resampler.h - Linear-interpolation audio resampler
#ifndef AUDIOT_RESAMPLER_H
#define AUDIOT_RESAMPLER_H

#include <vector>

namespace audiot {

// 线性插值重采样器（简单高效，适合音频通信）
class Resampler {
public:
    // inputRate: 输入采样率, outputRate: 输出采样率
    Resampler(int inputRate, int outputRate);

    // 重采样整段音频
    std::vector<float> resample(const std::vector<float>& input);

    // 增量重采样（用于实时流）
    // 输入新样本块，输出重采样后的样本
    std::vector<float> process(const float* input, int count);

    // 重置内部状态
    void reset();

    int inputRate() const;
    int outputRate() const;
    double ratio() const;

private:
    int inputRate_;
    int outputRate_;
    double ratio_;

    // 增量处理状态
    double position_;      // 当前插值位置
    float lastSample_;     // 上一个样本（用于跨块插值）
    bool hasLastSample_;
};

} // namespace audiot
#endif // AUDIOT_RESAMPLER_H
