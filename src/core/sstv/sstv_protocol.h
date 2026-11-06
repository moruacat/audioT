// sstv_protocol.h - SSTV 模式规范表（逐段时序 + 行型序列）
//
// 为什么不用"同步 + 把剩余时间三等分"的简化模型：
// 历史上 AudioT 曾用该模型实现过 9 种 SSTV 模式，结果被整体删除
// （commit a93fb53，理由"彩色与栅格化缺陷反复"）。考古出的根因是：
//   1) 模式表把"单个色彩段的扫描时长"当成了"整行时长"（Martin M1 真行
//      446.446 ms 却填了 146.432 ms，Robot 36 真行 150 ms 却填了 88 ms），
//      于是行速整体快约 3 倍；
//   2) 三等分模型漏掉了 Martin M1 的 porch/分隔段（1200 Hz 同步之后那段
//      1500 Hz），并把色彩顺序一刀切成 R-Y/B-Y/Y（Martin M1 实际是 G-B-R）；
//   3) Robot 36 根本不是三等分结构——它是 4:2:0 交织：每行都发全分辨率
//      Y，再发一个色差分量（偶行 Cr、奇行 Cb），色度水平还降到 160 点；
//   4) 致命的是编码器与解码器共用同一张错表，往返自测永远通过，缺陷从不暴露。
//
// 因此这里改为**显式逐段时序表 + 行型序列**：一行由若干段按序组成，段类型
// 只有 Sync（同步）/ Porch（固定频率的隔离开关）/ Scan（像素调频扫描）三种；
// 不同模式只是表不同，代码完全共用。新增模式 = 加一段表，不改算法。
#ifndef AUDIOT_SSTV_PROTOCOL_H
#define AUDIOT_SSTV_PROTOCOL_H

#include <cstdint>
#include <cstddef>
#include <string>

namespace audiot {
namespace sstv {

// ---------------------------------------------------------------
// 频率与时序约定（两种模式通用）
//
// 这些常量与结构体里的时长一律用 **double**：标准时序是十进制毫秒（如
// 146.432 ms），用 float 存会损失精度（146.432f 实际是 146.4320068359375），
// 乘以采样率后取整会让个别段边界差 1 个样本；边界差一个样本会让其后整段
// 相位跳变，实测可造成 40% 以上的波形失配。
// ---------------------------------------------------------------

// 同步脉冲
constexpr double kSyncFreqHz      = 1200.0;
// 像素扫描的频率范围：0 -> 黑 1500 Hz，255 -> 白 2300 Hz
constexpr double kBlackFreqHz     = 1500.0;
constexpr double kWhiteFreqHz     = 2300.0;
constexpr double kScanSpanHz      = kWhiteFreqHz - kBlackFreqHz;   // 800 Hz
// 色差中性电平（Robot 36 色度段的参考频率：中性 1900 Hz）
constexpr double kChromaCenterHz  = 1900.0;

// 头部（两种模式相同，合计 880 ms）
constexpr double kLeaderFreqHz      = 1900.0;   // 校准导频
constexpr double kLeaderMs          = 300.0;
constexpr double kBreakFreqHz       = 1200.0;   // leader 与 VIS 之间的分隔音
constexpr double kBreakMs           = 10.0;
constexpr double kVisStartFreqHz    = 1200.0;   // VIS 起始位
constexpr double kVisStartMs        = 300.0;
constexpr double kVisBitMs          = 30.0;     // 每个 VIS 数据位
constexpr double kVisBitOneHz       = 1100.0;   // 1
constexpr double kVisBitZeroHz      = 1300.0;   // 0
constexpr double kVisStopFreqHz     = 1200.0;   // 停止位
constexpr double kVisStopMs         = 30.0;     // VIS 停止位时长
constexpr int    kVisDataBits       = 7;        // 7 位模式码（LSB first）+ 1 位偶校验

// ---------------------------------------------------------------
// 模式
// ---------------------------------------------------------------
enum class SstvMode {
    MartinM1 = 0,   // VIS 44，320x256，RGB 顺序 G-B-R
    Robot36  = 1    // VIS 8， 320x240，YCrCb 4:2:0，奇偶行交织
};

enum class SegType {
    Sync,   // 固定频率的同步脉冲
    Porch,  // 固定频率的隔离/参考段
    Scan    // 像素调频扫描段
};

// 扫描段承载的分量
enum class Comp {
    None,   // 非扫描段
    Y,
    Cr,
    Cb,
    R,
    G,
    B
};

struct Segment {
    SegType type;
    Comp comp;      // 仅 Scan 段有效
    double durMs;   // 段时长（毫秒）—— 必须 double，见文件头精度说明
    double freqHz;  // Sync/Porch：该段的固定频率；Scan：扫描起始频率（= kBlackFreqHz）
    int pixels;     // Scan 段承载的像素/采样点个数；非扫描段为 0
};

// 一种"行型"：一行内的段序列
struct LinePattern {
    const Segment* segs;
    int count;
};

struct ModeSpec {
    SstvMode mode;
    uint8_t visCode;
    const char* name;
    int width;          // 扫描行像素数（Y / R / G / B 段）
    int height;         // 行数
    int chromaWidth;    // 色度段采样数（RGB 模式为 0）
    bool ycbcr;         // true = 色差模式（Robot 36）
    double lineTimeMs;  // 整行时长
    const LinePattern* patterns;   // 行型表；第 line 行用 patterns[line % patternCount]
    int patternCount;
    double expectedImageSec;       // 图像部分公开时长（仅行扫描，不含头部），供校验
};

// ---------------------------------------------------------------
// 查询
// ---------------------------------------------------------------
int modeCount();
const ModeSpec& modeSpecByIndex(int index);
// 按 VIS 码查表；未命中返回 nullptr（**不静默回退到某个模式**）
const ModeSpec* modeSpecFromVisCode(uint8_t visCode);

// 头部总时长（毫秒）：leader + break + VIS
double headerDurationMs();
// 整帧时长（毫秒）：头部 + 行时长 x 行数
double frameDurationMs(const ModeSpec& spec);
// 行索引 -> 行型
const LinePattern& linePatternFor(const ModeSpec& spec, int lineIndex);

// 结构自检：校验每行型的 Σ段时长 == lineTimeMs、Σ扫描像素数 == width、
// 帧时长与公开值一致、VIS 码合法等。失败返回 false 并写入 err。
bool validateModeSpec(const ModeSpec& spec, std::string* err = nullptr);
// 校验全部内置模式
bool validateAllModes(std::string* err = nullptr);

} // namespace sstv
} // namespace audiot

#endif // AUDIOT_SSTV_PROTOCOL_H
