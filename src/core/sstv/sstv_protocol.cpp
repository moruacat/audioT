// sstv_protocol.cpp - SSTV 模式规范表
#include "sstv_protocol.h"

#include <cmath>

namespace audiot {
namespace sstv {

namespace {

// ---------------------------------------------------------------
// Martin M1（VIS 44，320x256）
// 行结构：Sync 4.862 -> Porch 0.572 -> G 146.432 -> Porch 0.572
//         -> B 146.432 -> Porch 0.572 -> R 146.432 -> Porch 0.572
// 合计 446.446 ms；扫描顺序 G-B-R（HRGB 与 GBR 的说法见协议文档注释）。
// 每像素 = 146.432 / 320 = 0.4576 ms。
// ---------------------------------------------------------------
const Segment kMartinM1Line[] = {
    { SegType::Sync,  Comp::None, 4.862,   1200.0,   0 },
    { SegType::Porch, Comp::None, 0.572,   1500.0,   0 },
    { SegType::Scan,  Comp::G,    146.432, 1500.0, 320 },
    { SegType::Porch, Comp::None, 0.572,   1500.0,   0 },
    { SegType::Scan,  Comp::B,    146.432, 1500.0, 320 },
    { SegType::Porch, Comp::None, 0.572,   1500.0,   0 },
    { SegType::Scan,  Comp::R,    146.432, 1500.0, 320 },
    { SegType::Porch, Comp::None, 0.572,   1500.0,   0 },
};
const LinePattern kMartinM1Patterns[] = {
    { kMartinM1Line, static_cast<int>(sizeof(kMartinM1Line) / sizeof(kMartinM1Line[0])) },
};

// ---------------------------------------------------------------
// Robot 36（VIS 8，320x240，4:2:0 交织）
// 每行都发全分辨率 Y（88.0 ms / 320 px = 0.275 ms/px），再发一个色差分量
// （44.0 ms / 160 = 0.275 ms/采样，与 Y 同步）。偶行 Cr、奇行 Cb。
// 段结构：Sync 9.0 -> Porch 3.0 -> Y 88.0 -> Sep 4.5 -> Porch 1.5(1900Hz)
//         -> Cr/Cb 44.0        合计 150.0 ms；240 行 = 36.00 s（模式名即帧长）。
// Sep 频率：偶行 1500 Hz、奇行 2300 Hz（来源对奇行有冲突，见解码器的处理策略：
// 奇偶行由同步锁定后的行号决定，Sep 频率仅作校验不作判定依据）。
// ---------------------------------------------------------------
const Segment kRobot36EvenLine[] = {
    { SegType::Sync,  Comp::None, 9.0,  1200.0,   0 },
    { SegType::Porch, Comp::None, 3.0,  1500.0,   0 },
    { SegType::Scan,  Comp::Y,    88.0, 1500.0, 320 },
    { SegType::Porch, Comp::None, 4.5,  1500.0,   0 },
    { SegType::Porch, Comp::None, 1.5,  1900.0,   0 },
    { SegType::Scan,  Comp::Cr,   44.0, 1500.0, 160 },
};
const Segment kRobot36OddLine[] = {
    { SegType::Sync,  Comp::None, 9.0,  1200.0,   0 },
    { SegType::Porch, Comp::None, 3.0,  1500.0,   0 },
    { SegType::Scan,  Comp::Y,    88.0, 1500.0, 320 },
    { SegType::Porch, Comp::None, 4.5,  2300.0,   0 },
    { SegType::Porch, Comp::None, 1.5,  1900.0,   0 },
    { SegType::Scan,  Comp::Cb,   44.0, 1500.0, 160 },
};
const LinePattern kRobot36Patterns[] = {
    { kRobot36EvenLine, static_cast<int>(sizeof(kRobot36EvenLine) / sizeof(kRobot36EvenLine[0])) },
    { kRobot36OddLine,  static_cast<int>(sizeof(kRobot36OddLine) / sizeof(kRobot36OddLine[0])) },
};

const ModeSpec kModes[] = {
    // mode,             vis,  name,          w,   h,   chroma, ycbcr, lineMs,  patterns,              count, expectedImageSec
    { SstvMode::MartinM1, 44, "Martin M1", 320, 256,     0, false, 446.446, kMartinM1Patterns, 1, 114.290 },
    { SstvMode::Robot36,   8, "Robot 36",  320, 240,   160, true,  150.000, kRobot36Patterns,  2,  36.000 },
};

constexpr int kModeCount = static_cast<int>(sizeof(kModes) / sizeof(kModes[0]));

// 时长比较容差（毫秒）：标准值多为 3 位小数，留 0.05 ms 余量
constexpr double kDurEpsMs = 0.05;

} // namespace

int modeCount()
{
    return kModeCount;
}

const ModeSpec& modeSpecByIndex(int index)
{
    if (index < 0 || index >= kModeCount) {
        return kModes[0];
    }
    return kModes[index];
}

const ModeSpec* modeSpecFromVisCode(uint8_t visCode)
{
    for (int i = 0; i < kModeCount; ++i) {
        if (kModes[i].visCode == visCode) {
            return &kModes[i];
        }
    }
    return nullptr;   // 未知 VIS：由调用方明确报错，不再静默回退
}

double headerDurationMs()
{
    return kLeaderMs + kBreakMs + kVisStartMs +
           static_cast<double>(kVisDataBits + 1) * kVisBitMs +   // 7 数据位 + 1 偶校验
           kVisStopMs;
}

double frameDurationMs(const ModeSpec& spec)
{
    return headerDurationMs() + spec.lineTimeMs * static_cast<double>(spec.height);
}

const LinePattern& linePatternFor(const ModeSpec& spec, int lineIndex)
{
    int idx = 0;
    if (spec.patternCount > 0) {
        idx = lineIndex % spec.patternCount;
        if (idx < 0) {
            idx += spec.patternCount;
        }
    }
    return spec.patterns[idx];
}

bool validateModeSpec(const ModeSpec& spec, std::string* err)
{
    const auto fail = [err](const std::string& msg) {
        if (err) {
            *err = msg;
        }
        return false;
    };

    if (spec.width <= 0 || spec.height <= 0) {
        return fail("宽高必须为正");
    }
    if (spec.patternCount <= 0 || spec.patterns == nullptr) {
        return fail("缺少行型表");
    }

    double frameMs = headerDurationMs();
    bool modeSawCr = false;
    bool modeSawCb = false;
    for (int p = 0; p < spec.patternCount; ++p) {
        const LinePattern& pat = spec.patterns[p];
        if (pat.count <= 0 || pat.segs == nullptr) {
            return fail("行型段序列为空");
        }

        double sumMs = 0.0;
        bool sawR = false, sawG = false, sawB = false;
        bool sawY = false, sawCr = false, sawCb = false;
        for (int i = 0; i < pat.count; ++i) {
            const Segment& s = pat.segs[i];
            if (s.durMs <= 0.0) {
                return fail("段时长必须为正");
            }
            sumMs += s.durMs;
            if (s.type != SegType::Scan) {
                continue;
            }
            if (s.pixels <= 0) {
                return fail("扫描段像素数必须为正");
            }
            // 扫描段起始频率必须是黑电平（黑->白线性调频）
            if (std::fabs(s.freqHz - kBlackFreqHz) > 0.5f) {
                return fail("扫描段起始频率应为 1500 Hz");
            }
            // 每个亮度/彩色分量各自都是一整行，像素数必须等于宽度
            switch (s.comp) {
                case Comp::R:
                    if (s.pixels != spec.width) return fail("R 段像素数 != 宽度");
                    sawR = true;
                    break;
                case Comp::G:
                    if (s.pixels != spec.width) return fail("G 段像素数 != 宽度");
                    sawG = true;
                    break;
                case Comp::B:
                    if (s.pixels != spec.width) return fail("B 段像素数 != 宽度");
                    sawB = true;
                    break;
                case Comp::Y:
                    if (s.pixels != spec.width) return fail("Y 段像素数 != 宽度");
                    sawY = true;
                    break;
                case Comp::Cr:
                    if (spec.chromaWidth <= 0 || s.pixels != spec.chromaWidth) {
                        return fail("Cr 段像素数 != chromaWidth");
                    }
                    sawCr = true;
                    modeSawCr = true;
                    break;
                case Comp::Cb:
                    if (spec.chromaWidth <= 0 || s.pixels != spec.chromaWidth) {
                        return fail("Cb 段像素数 != chromaWidth");
                    }
                    sawCb = true;
                    modeSawCb = true;
                    break;
                default:
                    return fail("扫描段缺少分量类型");
            }
        }

        if (std::fabs(sumMs - spec.lineTimeMs) > kDurEpsMs) {
            return fail("行内段时长之和与 lineTimeMs 不一致");
        }

        if (!spec.ycbcr) {
            // RGB 直发模式：R/G/B 各一段，不得出现色差或亮度复合段
            if (!(sawR && sawG && sawB)) {
                return fail("RGB 模式必须包含 R/G/B 各一段");
            }
            if (sawY || sawCr || sawCb) {
                return fail("RGB 模式不应出现 Y/Cr/Cb 段");
            }
        } else {
            // 色差模式：每行一段 Y 加且仅加一个色差分量
            if (!sawY) {
                return fail("色差模式每行必须包含 Y 段");
            }
            if (sawCr == sawCb) {
                return fail("色差模式每行必须且只能包含 Cr/Cb 之一");
            }
            if (sawR || sawG || sawB) {
                return fail("色差模式不应出现 R/G/B 段");
            }
            if (spec.chromaWidth <= 0) {
                return fail("色差模式必须给出 chromaWidth");
            }
        }
        frameMs += sumMs * static_cast<double>(spec.height);
    }

    // 色差模式：整帧必须同时覆盖 Cr 与 Cb（否则会缺一个色度通道）
    if (spec.ycbcr && !(modeSawCr && modeSawCb)) {
        return fail("色差模式的各行型必须覆盖 Cr 与 Cb");
    }

    // 帧总时长与公开值对照（图像部分，不含头部）
    const double imageSec = spec.lineTimeMs * static_cast<double>(spec.height) / 1000.0;
    if (std::fabs(imageSec - spec.expectedImageSec) > 0.01) {
        return fail("图像时长与公开值不一致");
    }
    (void)frameMs;
    return true;
}

bool validateAllModes(std::string* err)
{
    for (int i = 0; i < kModeCount; ++i) {
        std::string localErr;
        if (!validateModeSpec(kModes[i], &localErr)) {
            if (err) {
                *err = std::string(kModes[i].name) + ": " + localErr;
            }
            return false;
        }
    }
    return true;
}

} // namespace sstv
} // namespace audiot
