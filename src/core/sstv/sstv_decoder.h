// sstv_decoder.h - SSTV 解码器：音频样本 -> 图片
#ifndef AUDIOT_SSTV_DECODER_H
#define AUDIOT_SSTV_DECODER_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "sstv_protocol.h"

namespace audiot {
namespace sstv {

// 采样率下限：低于此值时每个像素只有约 2~6 个样本，无法可靠解调。
constexpr int kMinDecodeSampleRate = 22050;

struct DecodeResult {
    bool ok = false;
    std::string error;          // 失败原因（中文，可直接显示给用户）
    std::string warning;        // 成功但有疑点时给出提示（如 VIS 与强制模式不一致）
    uint8_t visCode = 0;
    const ModeSpec* mode = nullptr;
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgb;   // RGB888，尺寸 width x height
    float confidence = 0.0f;    // 0..1，主要由"成功锁定同步的行数占比"决定
    int linesSynced = 0;
    int linesTotal = 0;
};

// 解码一段音频里的 SSTV 图像。
//
// forceMode：非空时强制按该模式解码（VIS 失败也能出图），VIS 仅作信息回报；
//            为空时按 VIS 自动识别，未识别则失败返回。
// progress：  0..100 进度回调，可为空。
//
// 采样率低于 kMinDecodeSampleRate 时直接失败——不做上行重采样，
// 因为插值会引入本可避免的相位误差，不如让用户在设置里改成 44100。
DecodeResult decode(const std::vector<float>& samples,
                    int sampleRate,
                    const ModeSpec* forceMode = nullptr,
                    const std::function<void(int)>& progress = nullptr);

// 只做头部检测（供 UI 在开始解码前给出"检测到 Martin M1 / 未识别"）。
// 返回 false 表示没找到 leader 与 VIS。
bool detectVis(const std::vector<float>& samples, int sampleRate,
               uint8_t* visCode);

} // namespace sstv
} // namespace audiot

#endif // AUDIOT_SSTV_DECODER_H
