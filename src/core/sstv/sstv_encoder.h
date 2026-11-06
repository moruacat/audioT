// sstv_encoder.h - SSTV 编码器：图片 -> 音频样本
#ifndef AUDIOT_SSTV_ENCODER_H
#define AUDIOT_SSTV_ENCODER_H

#include <cstdint>
#include <vector>

#include "sstv_protocol.h"

namespace audiot {
namespace sstv {

// 把一张 RGB888 图片按模式规范调制为整帧 SSTV 音频样本。
//
// 输入约定：
//   - rgb 必须恰好是 spec.width x spec.height 的 8 位 RGB 三通道、行连续
//     （每行 3*width 字节，不带 Qt 那种 4 字节对齐填充）；缩放由调用方完成；
//   - sampleRate 为输出采样率（标准时序以毫秒定义，任意采样率都成立，
//     工程默认 44100）。
//
// 输出：覆盖整帧（含 880 ms 头部）的音频样本，范围 [-amplitude, amplitude]。
// 失败（参数非法）返回空 vector。
std::vector<float> encodeImage(const uint8_t* rgb,
                               const ModeSpec& spec,
                               int sampleRate,
                               float amplitude = 0.8f);

// 帧的总样本数（与 encodeImage 的输出长度一致），便于调用方预分配/估算时长。
long long encodedSampleCount(const ModeSpec& spec, int sampleRate);

} // namespace sstv
} // namespace audiot

#endif // AUDIOT_SSTV_ENCODER_H
