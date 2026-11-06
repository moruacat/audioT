// sstv_encoder.cpp - SSTV 编码器实现
//
// 关键实现约束（都是历史上出过错的地方）：
//   1) 段边界一律用**绝对样本位置**计算（llround(累计毫秒 * 样本/毫秒)），
//      绝不逐段累加取整——否则每段都带一个舍入误差，跑满 256 行后累积漂移
//      可达上千个样本，整帧长度错、接收端逐行对不齐。
//   2) 全帧相位连续：段与段之间不重置相位。每一帧信号的相位从同一个累加器
//      出来，避免段边界出现相位跳变被解码器误判成"另一个音"。
//   3) 容量一次性 reserve：旧实现"每段 reserve(size+n)"会导致 vector 反复
//      重新分配并整体拷贝（O(n²)），与本项目 AFSK 调制器踩过的坑同源。
//   4) 扫描段的频率映射统一为 f = 1500 + 800 * v/255；色差用标准 BT.601
//      系数（Cr=(R-Y)*0.713+128、Cb=(B-Y)*0.564+128），而不是旧实现的
//      (R-Y)*0.5+128 —— 后者不是标准系数，色彩必然偏。
#include "sstv_encoder.h"

#include <cmath>

namespace audiot {
namespace sstv {

namespace {

constexpr double kTwoPi = 6.283185307179586476925286766559;

// 追加一段固定频率的音（相位沿用调用方的累加器，保证全帧连续）
void emitTone(std::vector<float>& out, double& phase, double freqHz,
              long long count, int sampleRate, float amplitude)
{
    if (count <= 0) {
        return;
    }
    const double step = kTwoPi * freqHz / static_cast<double>(sampleRate);
    for (long long i = 0; i < count; ++i) {
        out.push_back(static_cast<float>(std::sin(phase)) * amplitude);
        phase += step;
        if (phase >= kTwoPi) {
            phase -= kTwoPi;
        } else if (phase < 0.0) {
            phase += kTwoPi;
        }
    }
}

// 分量值(0..255) -> 瞬时频率：0 = 黑 1500 Hz，255 = 白 2300 Hz
inline double valueToFreq(double v)
{
    if (v < 0.0) v = 0.0;
    if (v > 255.0) v = 255.0;
    return kBlackFreqHz + kScanSpanHz * (v / 255.0);
}

inline double clamp255(double v)
{
    if (v < 0.0) return 0.0;
    if (v > 255.0) return 255.0;
    return v;
}

// 取第 x 个像素在分量 c 上的取值（0..255）
inline double componentAt(const uint8_t* px, Comp c)
{
    const double R = px[0];
    const double G = px[1];
    const double B = px[2];
    switch (c) {
        case Comp::R: return R;
        case Comp::G: return G;
        case Comp::B: return B;
        case Comp::Y: return clamp255(0.299 * R + 0.587 * G + 0.114 * B);
        case Comp::Cr: {
            // BT.601：Cr = (R - Y) * 0.713 + 128
            const double Y = 0.299 * R + 0.587 * G + 0.114 * B;
            return clamp255((R - Y) * 0.713 + 128.0);
        }
        case Comp::Cb: {
            // BT.601：Cb = (B - Y) * 0.564 + 128
            const double Y = 0.299 * R + 0.587 * G + 0.114 * B;
            return clamp255((B - Y) * 0.564 + 128.0);
        }
        default: return 128.0;
    }
}

// 头部：leader / break / VIS 起始位 / 7 数据位(LSB first) / 偶校验 / 停止位
// 用绝对毫秒边界，保证头部样本数与 headerDurationMs() 完全一致。
void emitHeader(std::vector<float>& out, double& phase, uint8_t visCode,
                int sampleRate, float amplitude)
{
    const double spm = sampleRate / 1000.0;

    struct Part { double freq; double ms; };
    Part parts[3] = {
        { kLeaderFreqHz,   kLeaderMs },
        { kBreakFreqHz,    kBreakMs },
        { kVisStartFreqHz, kVisStartMs },
    };
    double cumMs = 0.0;
    for (const Part& p : parts) {
        const long long start = std::llround(cumMs * spm);
        cumMs += p.ms;
        const long long end = std::llround(cumMs * spm);
        emitTone(out, phase, p.freq, end - start, sampleRate, amplitude);
    }

    // VIS 数据位：LSB first，bit=1 -> 1100 Hz，bit=0 -> 1300 Hz
    int ones = 0;
    for (int i = 0; i < kVisDataBits; ++i) {
        const bool bit = ((visCode >> i) & 0x01) != 0;
        if (bit) ++ones;
        const double f = bit ? kVisBitOneHz : kVisBitZeroHz;
        const long long start = std::llround(cumMs * spm);
        cumMs += kVisBitMs;
        const long long end = std::llround(cumMs * spm);
        emitTone(out, phase, f, end - start, sampleRate, amplitude);
    }

    // 偶校验位：使 1 的总个数为偶数
    {
        const bool parityOne = (ones % 2) != 0;
        const double f = parityOne ? kVisBitOneHz : kVisBitZeroHz;
        const long long start = std::llround(cumMs * spm);
        cumMs += kVisBitMs;
        const long long end = std::llround(cumMs * spm);
        emitTone(out, phase, f, end - start, sampleRate, amplitude);
    }

    // 停止位 1200 Hz
    {
        const long long start = std::llround(cumMs * spm);
        cumMs += kVisStopMs;
        const long long end = std::llround(cumMs * spm);
        emitTone(out, phase, kVisStopFreqHz, end - start, sampleRate, amplitude);
    }
}

} // namespace

long long encodedSampleCount(const ModeSpec& spec, int sampleRate)
{
    if (sampleRate <= 0) {
        return 0;
    }
    const double spm = sampleRate / 1000.0;
    return std::llround(frameDurationMs(spec) * spm);
}

std::vector<float> encodeImage(const uint8_t* rgb, const ModeSpec& spec,
                               int sampleRate, float amplitude)
{
    std::vector<float> out;
    if (rgb == nullptr || sampleRate <= 0) {
        return out;
    }
    if (spec.width <= 0 || spec.height <= 0 || spec.patternCount <= 0) {
        return out;
    }

    const double spm = sampleRate / 1000.0;
    const double headerMs = headerDurationMs();

    // 容量一次性预留（见文件头注释 3）
    const long long total = encodedSampleCount(spec, sampleRate);
    if (total <= 0) {
        return out;
    }
    out.reserve(static_cast<size_t>(total));

    double phase = 0.0;
    emitHeader(out, phase, spec.visCode, sampleRate, amplitude);

    const size_t rowStride = static_cast<size_t>(spec.width) * 3;

    for (int line = 0; line < spec.height; ++line) {
        // 段边界一律用**从帧首起的绝对毫秒**直接换算成样本位置：
        //   sample(ms) = llround(ms * sampleRate / 1000)
        // 段首毫秒 = headerMs + line*lineTimeMs + 段前累计毫秒。
        //
        // 不要改成"按行内比例缩放以强制行长为整数"：Martin M1 的行长是
        // 446.446 ms x 44.1 = 19688.27 个样本（非整数），强行缩放会把行内
        // 每个段/像素的边界都挪动最多 0.27 个样本，边界处差 1 个样本就会
        // 造成此后相位整体跳变——实测波形与标准参考的失配率高达 81%。
        // 用绝对毫秒换算时，各段边界自动首尾相接，整帧长度也自然等于
        // llround(帧总毫秒 x 样本/毫秒)。
        const double lineBaseMs = headerMs + spec.lineTimeMs * line;

        const LinePattern& pat = linePatternFor(spec, line);
        const uint8_t* row = rgb + static_cast<size_t>(line) * rowStride;

        double cumMs = 0.0;
        for (int i = 0; i < pat.count; ++i) {
            const Segment& seg = pat.segs[i];
            const double beforeMs = cumMs;
            cumMs += seg.durMs;
            const long long segStart = std::llround((lineBaseMs + beforeMs) * spm);
            const long long segEnd = std::llround((lineBaseMs + cumMs) * spm);
            const long long segLen = segEnd - segStart;
            if (segLen <= 0) {
                continue;
            }

            if (seg.type != SegType::Scan) {
                emitTone(out, phase, seg.freqHz, segLen, sampleRate, amplitude);
                continue;
            }

            // 扫描段：按像素零阶保持，像素边界同样用绝对毫秒换算
            // （像素 p 覆盖 [p, p+1) 个像素时隙）。这样各像素时隙与整帧的
            // 绝对栅格严格对齐，不会出现累计漂移。
            const int pixels = seg.pixels;
            if (pixels <= 0) {
                continue;
            }
            const double segBaseMs = lineBaseMs + beforeMs;
            const double pixelMs = static_cast<double>(seg.durMs) / pixels;

            // 一个扫描采样点对应的源像素数（色度降采样时 > 1）
            const int srcPerSample = (spec.width % pixels == 0)
                                         ? (spec.width / pixels)
                                         : 1;
            for (int k = 0; k < pixels; ++k) {
                const long long pixStart =
                    std::llround((segBaseMs + pixelMs * k) * spm);
                const long long pixEnd =
                    std::llround((segBaseMs + pixelMs * (k + 1)) * spm);
                const long long n = pixEnd - pixStart;
                if (n <= 0) {
                    continue;
                }

                // 该采样点的分量值：色度段做水平盒滤波降采样（Cr/Cb 先逐像素
                // 求出再按对求平均，与主流实现一致）
                double v = 0.0;
                if (srcPerSample == 1) {
                    v = componentAt(row + static_cast<size_t>(k) * 3, seg.comp);
                } else {
                    for (int s = 0; s < srcPerSample; ++s) {
                        const int x = k * srcPerSample + s;
                        if (x >= spec.width) {
                            break;
                        }
                        v += componentAt(row + static_cast<size_t>(x) * 3, seg.comp);
                    }
                    v /= static_cast<double>(srcPerSample);
                }

                emitTone(out, phase, valueToFreq(v), n, sampleRate, amplitude);
            }
        }
    }

    return out;
}

} // namespace sstv
} // namespace audiot
