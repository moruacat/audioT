// sstv_decoder.cpp - SSTV 解码器实现
//
// 算法要点（每一条都对应一个具体的失败模式）：
//
// 1) 两级频率估计，因为像素窗口太短，单一方法无法兼顾：
//    - **长音（leader / VIS 位 / sync）用 Goertzel**：窗口 3~30 ms，弱机友好。
//    - **像素用相位差分鉴频**：M1 每像素只有 0.4576 ms（约 20 样本 @44.1k），
//      Goertzel 的频率分辨力是 fs/N ≈ 2.2 kHz，远大于 800 Hz 的信号带宽，
//      完全不可用。相位差分只需 1.5~3 个周期即可给出频率，
//      理论误差约 17 Hz（≈6 个灰阶），2 像素平滑后可到 5 灰阶。
//
// 2) 正交混频后必须低通：实信号混频会在 -2*f0 处留下镜像（这里约 3800 Hz），
//    不滤掉会让相位差估计抖动。用两级一阶 IIR（截止 600 Hz）：
//    在 3800 Hz 处约 -33 dB，而基带 ±400 Hz 内群延迟变化约 4 个样本，
//    对 12~20 样本的像素窗口只造成轻微邻像素混合——代价远小于不过滤。
//
// 3) 混频相位只影响绝对相位、不影响**相邻样本的相位差**，所以每次
//    process() 都可以用精确 sin/cos 重新初始化混频相位，不必跨调用累加，
//    既消除了数值漂移，又避免了长序列的精度损失。
//
// 4) 每个像素不求 atan2，而是把窗口内所有 z[n]*conj(z[n-1]) **复数累加**，
//    最后只做一次 atan2：这在数学上就是窗口内的相位增量平均（即平均频率），
//    噪声被平均掉，而 atan2 次数从"每样本一次"降到"每像素一次"（约 8 万次）。
//
// 5) 行同步必须 track + 拟合，不能直接按名义行距硬推：
//    M1 一行 446.446 ms，录音设备若有 0.1% 时钟偏差，239 行后就偏 35 ms
//    （远超一行内 porch 的宽度）。做法是先用 Goertzel 做一遍很轻的
//    sync 预扫描（每行只在名义位置 ±3 ms 内找 1200 Hz 能量峰，
//    并按实测行距做 EMA 跟踪），再对全部检测点做最小二乘拟合 n = a + b*i，
//    正式解码一律用拟合出的 a、b。
//
// 6) 第 0 行不做 sync 检测：它前面是 VIS 停止位（同样是 1200 Hz，30 ms），
//    与 sync 连成一片，测不出边界。第 0 行起点由拟合外推得到。
//
// 7) Robot 36 的色度是 4:2:0 交织（每行发全分辨率 Y + 一个色差分量，
//    偶行 Cr、奇行 Cb，水平 160 点）。还原时：水平 160->320 线性插值，
//    垂直方向对缺失分量用上下最近的同类行做线性插值，最后按 BT.601 反变换。
#include "sstv_decoder.h"

#include <algorithm>
#include <cmath>

namespace audiot {
namespace sstv {

namespace {

constexpr double kPi    = 3.14159265358979323846;
constexpr double kTwoPi = 6.28318530717958647692;

inline long long msToSamples(double ms, int sr)
{
    return std::llround(ms * static_cast<double>(sr) / 1000.0);
}

inline double clamp01(double v)
{
    return (v < 0.0) ? 0.0 : ((v > 1.0) ? 1.0 : v);
}

// ---------------------------------------------------------------
// Goertzel：归一化功率（约为该频率分量的幅度平方）
// ----------------------------------------------------------
double goertzelPower(const float* x, long long n, double freq, int sr)
{
    if (n <= 4) {
        return 0.0;
    }
    const double w = kTwoPi * freq / static_cast<double>(sr);
    const double c = 2.0 * std::cos(w);
    double s1 = 0.0;
    double s2 = 0.0;
    for (long long i = 0; i < n; ++i) {
        const double s0 = static_cast<double>(x[i]) + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    double p = s1 * s1 + s2 * s2 - c * s1 * s2;
    if (p < 0.0) {
        p = 0.0;
    }
    return p / (static_cast<double>(n) * static_cast<double>(n));
}

// 带偏移与越界裁剪的版本
double gPow(const std::vector<float>& s, long long a, long long n,
            double freq, int sr)
{
    const long long size = static_cast<long long>(s.size());
    if (a < 0) {
        n += a;
        a = 0;
    }
    if (a + n > size) {
        n = size - a;
    }
    if (n <= 4) {
        return 0.0;
    }
    return goertzelPower(s.data() + a, n, freq, sr);
}

// ---------------------------------------------------------------
// 头部检测：leader 导频（1900 Hz 300 ms）
// ---------------------------------------------------------------
struct LeaderInfo {
    bool found = false;
    long long start = 0;   // leader 起点
    long long end = 0;     // leader 结束 = break（1200 Hz）开始
};

LeaderInfo findLeader(const std::vector<float>& s, int sr)
{
    LeaderInfo info;
    const long long size = static_cast<long long>(s.size());
    const long long win = msToSamples(10, sr);
    const long long hop = msToSamples(5, sr);
    if (size < msToSamples(400, sr) || win <= 0 || hop <= 0) {
        return info;
    }

    // 参考功率：用全局均方作为"有声"的参照，避免固定阈值在不同音量下失效
    double meanSq = 0.0;
    long long cnt = 0;
    const long long step = std::max<long long>(1, size / 100000);
    for (long long i = 0; i < size; i += step) {
        meanSq += static_cast<double>(s[i]) * static_cast<double>(s[i]);
        ++cnt;
    }
    meanSq /= static_cast<double>(std::max<long long>(1, cnt));
    if (meanSq < 1e-7) {
        return info;   // 静音
    }
    const double thr = 0.02 * 2.0 * meanSq;

    long long leaderAt = -1;
    for (long long a = 0; a + win <= size; a += hop) {
        const double p1900 = goertzelPower(s.data() + a, win, kLeaderFreqHz, sr);
        const double p1200 = goertzelPower(s.data() + a, win, 1200.0, sr);
        if (p1900 > thr && p1900 > 4.0 * p1200) {
            leaderAt = a;
            break;
        }
    }
    if (leaderAt < 0) {
        return info;
    }
    info.start = leaderAt;

    // 找 1900 -> 1200 的过渡
    long long transAt = -1;
    for (long long a = leaderAt + hop; a + win <= size; a += hop) {
        const double p1900 = goertzelPower(s.data() + a, win, kLeaderFreqHz, sr);
        const double p1200 = goertzelPower(s.data() + a, win, 1200.0, sr);
        if (p1200 > thr && p1200 > 2.0 * p1900) {
            transAt = a;
            break;
        }
    }
    if (transAt < 0) {
        return info;
    }

    // 精化到 0.25 ms：在过渡点附近用 2 ms 窗找 1900 Hz 能量半高位置
    const long long fineWin = msToSamples(2, sr);
    const long long fineHop = std::max<long long>(1, msToSamples(0.25, sr));
    long long end = transAt + hop / 2;
    for (long long a = std::max<long long>(leaderAt, transAt - hop);
         a <= transAt + hop; a += fineHop) {
        const double p1900 = goertzelPower(s.data() + a, fineWin, kLeaderFreqHz, sr);
        if (p1900 <= thr) {
            end = a + fineWin / 2;
            break;
        }
    }
    info.end = end;

    // leader 至少应有 120 ms（允许录音把开头截掉一部分）
    if (info.end - info.start < msToSamples(120, sr)) {
        return info;
    }
    info.found = true;
    return info;
}

// ---------------------------------------------------------------
// VIS：break(10ms,1200) + start(300ms,1200) + 7 数据位 + 偶校验 + stop(30ms,1200)
// ---------------------------------------------------------------
struct VisInfo {
    bool found = false;
    bool parityOk = false;
    uint8_t code = 0;
    double quality = 0.0;   // 最差那一位的"信噪比"（p1 与 p0 的比值），用于判可信度
};

VisInfo readVis(const std::vector<float>& s, int sr, const LeaderInfo& leader)
{
    VisInfo v;
    if (!leader.found) {
        return v;
    }
    // break + VIS 起始位之后的第一位数据
    const long long bitStart =
        leader.end + msToSamples(kBreakMs + kVisStartMs, sr);
    const long long bitLen = msToSamples(kVisBitMs, sr);
    const long long win = msToSamples(10, sr);
    if (bitLen <= 0 || win <= 0) {
        return v;
    }

    const auto readBit = [&](int index, bool* ok) {
        const long long c = bitStart + bitLen * index + bitLen / 2;
        const double p1 = gPow(s, c - win / 2, win, kVisBitOneHz, sr);
        const double p0 = gPow(s, c - win / 2, win, kVisBitZeroHz, sr);
        const double hi = std::max(p1, p0);
        const double lo = std::min(p1, p0);
        if (ok) {
            *ok = (hi > 0.0 && hi > 2.5 * lo);
        }
        return p1 > p0;
    };

    uint8_t code = 0;
    int ones = 0;
    double worst = 1e9;
    bool allOk = true;
    for (int i = 0; i < kVisDataBits; ++i) {
        bool ok = false;
        const bool bit = readBit(i, &ok);
        if (bit) {
            code |= static_cast<uint8_t>(1u << i);
            ++ones;
        }
        if (!ok) {
            allOk = false;
        }
        const long long c = bitStart + bitLen * i + bitLen / 2;
        const double p1 = gPow(s, c - win / 2, win, kVisBitOneHz, sr);
        const double p0 = gPow(s, c - win / 2, win, kVisBitZeroHz, sr);
        const double hi = std::max(p1, p0);
        const double lo = std::min(p1, p0);
        const double ratio = (lo > 1e-12) ? (hi / lo) : 1000.0;
        worst = std::min(worst, ratio);
    }

    bool parOk = false;
    const bool parityBit = readBit(kVisDataBits, &parOk);
    const bool parityGood = ((ones + (parityBit ? 1 : 0)) % 2) == 0;

    v.code = code;
    v.parityOk = parityGood && parOk;
    v.quality = (worst > 1e9) ? 1.0 : clamp01((worst - 1.0) / 4.0);
    v.found = allOk && parOk;
    return v;
}

// ---------------------------------------------------------------
// 相位差分鉴频器
// ---------------------------------------------------------------
// 低通级数与截止频率的选择（这里的一个具体数值直接决定图像噪声）：
// 混频后除了基带（±400 Hz）还留着 -2*f0 的镜像（约 3800 Hz）。镜像与基带
// 叠加会让**相位**轻微摆动，而相位差分是对相位做**逐样本求差**，摆动会被
// 放大成很大的频率误差：
//   相位摆幅 δφ、镜像频率 f_img -> 单样本相位差抖动 ≈ δφ·2π·f_img/fs
//   再乘以 fs/(2π) 得频率误差 ≈ δφ·f_img
// 两级一阶低通（截止 600 Hz）在 3800 Hz 只有约 -32 dB（δφ≈0.025 rad），
// 对应约 95 Hz 的频率抖动，实测灰图会出现 ±19 个灰阶的周期性条纹。
// 改为**四级一阶、截止 700 Hz**：3800 Hz 处约 -60 dB，抖动降到 3 Hz 量级。
// 代价是群延迟约 42 个样本（R36 每像素 12 个），对块内恒定区域无影响，
// 只在彩色边缘有约 1~2 像素的轻微渗色——远小于条纹噪声的可见度。
class FmDemod {
public:
    FmDemod(int sr, double centerHz)
        : sr_(sr)
        , center_(centerHz)
        , lpA_(1.0 - std::exp(-kTwoPi * 700.0 / static_cast<double>(sr)))
        , prevI_(0.0), prevQ_(0.0), hasPrev_(false)
    {
        for (int i = 0; i < kStages; ++i) {
            lpI_[i] = 0.0;
            lpQ_[i] = 0.0;
        }
    }

    // 丢弃相邻样本关系（跨行跳样本时必须调用）
    void resetContinuity() { hasPrev_ = false; }

    // 处理样本 [a, b)：始终推进 IIR 状态；accumulate 为真时把窗口内的
    // z[n]*conj(z[n-1]) 累加进 re/im（跳过窗口首 skipHead、末 skipTail 个）。
    void process(const std::vector<float>& s, long long a, long long b,
                 bool accumulate, int skipHead, int skipTail,
                 double& re, double& im)
    {
        const long long size = static_cast<long long>(s.size());
        if (a < 0) {
            a = 0;
        }
        if (b > size) {
            b = size;
        }
        if (a >= b) {
            return;
        }

        const double w = kTwoPi * center_ / static_cast<double>(sr_);
        const double cs = std::cos(w);
        const double ss = std::sin(w);
        double c = std::cos(w * static_cast<double>(a));
        double sn = std::sin(w * static_cast<double>(a));

        const long long n = b - a;
        const long long head = std::min<long long>(skipHead, n / 4);
        const long long tail = std::min<long long>(skipTail, n / 8);

        for (long long i = a; i < b; ++i) {
            const double v = static_cast<double>(s[static_cast<size_t>(i)]);

            // 正交混频到 0 Hz
            const double I = v * c;
            const double Q = -v * sn;

            const double nc = c * cs - sn * ss;
            sn = sn * cs + c * ss;
            c = nc;

            // 四级一阶低通（抑制 -2f0 镜像，见类注释）
            double v1 = I;
            double vq = Q;
            for (int s = 0; s < kStages; ++s) {
                lpI_[s] += lpA_ * (v1 - lpI_[s]);
                lpQ_[s] += lpA_ * (vq - lpQ_[s]);
                v1 = lpI_[s];
                vq = lpQ_[s];
            }
            const double bI = v1;
            const double bQ = vq;

            if (hasPrev_ && accumulate) {
                const long long rel = i - a;
                if (rel >= head && rel < n - tail) {
                    re += bI * prevI_ + bQ * prevQ_;
                    im += bQ * prevI_ - bI * prevQ_;
                }
            }
            prevI_ = bI;
            prevQ_ = bQ;
            hasPrev_ = true;
        }
    }

    double freqFrom(double re, double im) const
    {
        if (re == 0.0 && im == 0.0) {
            return center_;
        }
        return center_ + std::atan2(im, re) * static_cast<double>(sr_) / kTwoPi;
    }

    double magnitude(double re, double im) const
    {
        return std::sqrt(re * re + im * im);
    }

private:
    static constexpr int kStages = 4;

    int sr_;
    double center_;
    double lpA_;
    double lpI_[kStages];
    double lpQ_[kStages];
    double prevI_, prevQ_;
    bool hasPrev_;
};

// 频率 -> 灰阶值（0..255）
inline double freqToValue(double f)
{
    const double v = (f - kBlackFreqHz) / kScanSpanHz * 255.0;
    if (v < 0.0) {
        return 0.0;
    }
    if (v > 255.0) {
        return 255.0;
    }
    return v;
}

// 1210 Hz 能量峰搜索结果。
// 必须带判据，不能"取搜索窗内最大功率所在位置"就算找到：任何音频在
// 任意位置都有某个 1200 Hz 响应，若不加阈值，一段噪声或错误的时序模型
// 都会被判成"每行都锁到同步"，置信度假性为 100%。
struct PeakResult {
    long long pos = -1;
    double power = 0.0;
    double contrast = 0.0;   // 1200 Hz 功率 / 1500 Hz 功率
};

// 在 [lo, hi] 内找"最像同步脉冲"的位置（返回窗口起点，窗口长 win）。
//
// 判据不能用"峰值 / 窗内平均功率"：sync 脉冲本身就占了搜索窗的很大一部分
// （M1 的 sync 4.862 ms，搜索窗 ±3 ms 共约 11 ms），峰均比只有 1.2 左右，
// 会把正常波形也判成"没找到"。真正的特征是：同步段是 1200 Hz 而
// 紧邻的 porch / 扫描段是 1500~2300 Hz，所以用 **p1200 / p1500** 作为判据。
PeakResult findSyncPeak(const std::vector<float>& s, int sr,
                        long long lo, long long hi, long long win, long long hop)
{
    const long long size = static_cast<long long>(s.size());
    PeakResult r;

    // 第一遍：找最强的 1200/1500 比值，并记录所有位置
    struct Sample { long long pos; double ratio; double p1200; };
    std::vector<Sample> hits;
    hits.reserve(64);
    for (long long a = lo; a <= hi; a += hop) {
        if (a < 0 || a + win > size) {
            continue;
        }
        const double p1200 = goertzelPower(s.data() + a, win, 1200.0, sr);
        const double p1500 = goertzelPower(s.data() + a, win, kBlackFreqHz, sr);
        const double ratio = p1200 / (p1500 + 1e-9);
        hits.push_back(Sample{ a, ratio, p1200 });
        if (ratio > r.contrast) {
            r.contrast = ratio;
            r.power = p1200;
            r.pos = a;
        }
    }
    if (r.pos < 0 || hits.empty()) {
        return r;
    }

    // 第二遍：取"高比值区间"的**质心**，而不是最左位置。
    // 同步段比窗口长（M1 sync 4.862 ms vs 窗 3 ms，R36 9 ms vs 3 ms），
    // 窗口能在同步段内滑动一段距离而比值都同样高；若取最左位置，
    // 估出的 sync 起点会固定偏早约半个窗口，而且这个偏差会被
    // "expected = 上一行起点 + 行距"的跟踪沿用下去，最终累积到
    // 搜索窗之外（R36 就是这样只锁到 1 行）。
    // 取高比值区间的质心后，对两种模式都能还原到真实的 sync 起点附近。
    double sumPos = 0.0;
    int n = 0;
    const double floorRatio = 0.75 * r.contrast;
    for (const Sample& h : hits) {
        if (h.ratio >= floorRatio && h.p1200 >= 2.5e-3) {
            sumPos += static_cast<double>(h.pos);
            ++n;
        }
    }
    if (n > 0) {
        r.pos = static_cast<long long>(sumPos / static_cast<double>(n));
    }

    // 绝对下限（幅度 0.8 时 sync 功率约 0.16，这里留约 60 倍余量）；
    // 相对判据要求 1200 Hz 至少是 1500 Hz 的两倍。
    const bool strong = r.power >= 2.5e-3 && r.contrast >= 2.0;
    if (!strong) {
        r.pos = -1;
    }
    return r;
}

// ---------------------------------------------------------------
// 行同步预扫描 + 最小二乘拟合
// ---------------------------------------------------------------
struct SyncModel {
    double a = 0.0;      // 第 0 行 sync 起点（样本）
    double b = 0.0;      // 行距（样本）
    int detected = 0;    // 检测到的行数
    bool modelled = false;
    bool fitRejected = false;   // 拟合行距与名义值相差过大（波形不是这个模式）
};

SyncModel modelSync(const std::vector<float>& s, int sr, const ModeSpec& spec,
                    long long headerEndSample)
{
    SyncModel m;
    const long long size = static_cast<long long>(s.size());
    const long long syncSamples =
        msToSamples(spec.patterns[0].segs[0].durMs, sr);
    if (syncSamples <= 0) {
        return m;
    }
    const long long win = std::min<long long>(msToSamples(3.0, sr), syncSamples);
    const long long hop = std::max<long long>(1, msToSamples(0.25, sr));
    // 搜索半宽必须能**完整覆盖一个 sync 段**，否则"高比值区间"会被搜索边界
    // 截断：质心随之左移，估出的起点偏早，而这个偏差会沿
    // expected = 上一行 + 行距 逐行累积（Robot 36 的 sync 有 9 ms，
    // 用固定的 ±3 ms 时每行只锁到约 7 行就漂出窗）。
    // 取：3 ms 基础容差 + 半个 sync 长度 + 1 ms 余量。
    const long long half =
        msToSamples(3.0, sr) + syncSamples / 2 + msToSamples(1.0, sr);

    const double nominalLine = spec.lineTimeMs * static_cast<double>(sr) / 1000.0;

    std::vector<double> starts(static_cast<size_t>(spec.height), -1.0);
    double interval = nominalLine;
    // 第 0 行不参与检测（它前面紧邻 VIS 停止位，同样是 1200 Hz，两者连成
    // 一片，测不出边界）。所以第一轮循环处理的是第 1 行，搜索中心也必须
    // 从"第 1 行的名义位置"开始——**不能**用 headerEnd（那是第 0 行的位置）：
    // 否则第 1 行会锁到第 0 行的同步，估出的起点系统性偏早半个 sync，
    // 这个偏差又会沿 expected = 上一行 + 行距 一路传下去，
    // 最后漂出 ±3 ms 的搜索窗（Robot 36 曾因此只锁到 10 行）。
    double expected = static_cast<double>(headerEndSample) + interval;

    for (int line = 1; line < spec.height; ++line) {
        const long long lo = static_cast<long long>(expected) - half;
        const long long hi = static_cast<long long>(expected) + half;
        const PeakResult pr = findSyncPeak(s, sr, lo, hi, win, hop);
        if (pr.pos > 0) {
            // 能量峰在窗口中心 -> sync 中心 -> sync 起点
            const double centre = static_cast<double>(pr.pos) +
                                  static_cast<double>(win) / 2.0;
            const double start = centre - static_cast<double>(syncSamples) / 2.0;
            starts[static_cast<size_t>(line)] = start;
            if (line > 1 && starts[static_cast<size_t>(line - 1)] >= 0.0) {
                const double measured = start - starts[static_cast<size_t>(line - 1)];
                // EMA 跟踪实际行距，抗采样时钟偏差
                interval = 0.85 * interval + 0.15 * measured;
            }
            expected = start + interval;
        } else {
            expected += interval;
        }
    }

    // 最小二乘：n = a + b*line
    double sumI = 0.0, sumN = 0.0, sumII = 0.0, sumIN = 0.0;
    int cnt = 0;
    for (int line = 1; line < spec.height; ++line) {
        const double n = starts[static_cast<size_t>(line)];
        if (n < 0.0) {
            continue;
        }
        const double i = static_cast<double>(line);
        sumI += i;
        sumN += n;
        sumII += i * i;
        sumIN += i * n;
        ++cnt;
    }
    m.detected = cnt;
    if (cnt < 4) {
        // 连 4 行都锁不到，无法做有意义的拟合：按名义行距硬解，
        // 但明确标记"行速不可信"，让调用方压低置信度。
        m.a = static_cast<double>(headerEndSample);
        m.b = nominalLine;
        m.modelled = cnt > 0;
        m.fitRejected = true;
        return m;
    }
    const double den = static_cast<double>(cnt) * sumII - sumI * sumI;
    if (den <= 1e-9) {
        m.a = static_cast<double>(headerEndSample);
        m.b = nominalLine;
        m.modelled = true;
        return m;
    }
    const double b = (static_cast<double>(cnt) * sumIN - sumI * sumN) / den;
    const double a = (sumN - b * sumI) / static_cast<double>(cnt);

    // 合理性：行距偏离名义值超过 5% 视为锁错（例如波形其实是另一种模式、
    // 或旧式"三等分"实现的错误时序）。此时仍按名义行距尝试解码，
    // 但把 fitRejected 置位，让调用方知道结果不可信。
    if (std::fabs(b - nominalLine) > 0.05 * nominalLine) {
        m.a = static_cast<double>(headerEndSample);
        m.b = nominalLine;
        m.modelled = true;
        m.fitRejected = true;
        return m;
    }
    m.a = a;
    m.b = b;
    m.modelled = true;
    return m;
}

// ---------------------------------------------------------------
// Robot 36 色度还原 + BT.601 反变换
// ---------------------------------------------------------------
// 色度行的 3 点水平平滑。
// 依据：4:2:0 色度的水平带宽只有亮度的一半以上，本来就被降到 160 点，
// 解调噪声（相位差分的理论误差约 9 个灰阶）会被 BT.601 的反变换
// 放大 1.4 倍（R = Y + 1.402·ΔCr），不做平滑时红/蓝通道噪声明显。
// 这一平滑只作用在已经降采样的色度域上，不会损失亮度细节。
void smoothChromaRows(std::vector<double>& rows, int count, int cw)
{
    if (count <= 0 || cw <= 0) {
        return;
    }
    std::vector<double> src(rows.begin(),
                            rows.begin() + static_cast<size_t>(count) * cw);
    for (int r = 0; r < count; ++r) {
        const size_t base = static_cast<size_t>(r) * cw;
        for (int x = 0; x < cw; ++x) {
            const int xm = (x > 0) ? (x - 1) : 0;
            const int xp = (x + 1 < cw) ? (x + 1) : (cw - 1);
            const double a = src[base + xm];
            const double b = src[base + x];
            const double c = src[base + xp];
            rows[base + x] = 0.25 * a + 0.5 * b + 0.25 * c;
        }
    }
}

void assembleRgbYcc(const ModeSpec& spec,
                    const std::vector<double>& yPlaneIn,
                    std::vector<double> crRows,
                    std::vector<double> cbRows,
                    int crCount, int cbCount,
                    std::vector<uint8_t>& rgb)
{
    const int w = spec.width;
    const int h = spec.height;
    const int cw = spec.chromaWidth;
    rgb.assign(static_cast<size_t>(w) * static_cast<size_t>(h) * 3, 0);

    smoothChromaRows(crRows, crCount, cw);
    smoothChromaRows(cbRows, cbCount, cw);

    // 亮度不做额外平滑：镜像抑制加强后（四级低通），实测灰图的
    // 单像素误差已在 ±1 个灰阶内，再加平滑只会让图像变糊。
    const std::vector<double>& yPlane = yPlaneIn;

    const auto sampleChromaRow = [&](const std::vector<double>& rows, int rowIndex,
                                     double frac) {
        // frac 为 0..1 的行内水平位置，返回 160 -> 320 的线性插值
        const double pos = frac * static_cast<double>(cw - 1);
        const int i0 = static_cast<int>(std::floor(pos));
        const int i1 = std::min(cw - 1, i0 + 1);
        const double t = pos - static_cast<double>(i0);
        const double v0 = rows[static_cast<size_t>(rowIndex) * cw + i0];
        const double v1 = rows[static_cast<size_t>(rowIndex) * cw + i1];
        return v0 + (v1 - v0) * t;
    };

    for (int y = 0; y < h; ++y) {
        // 垂直方向：偶行有 Cr、奇行有 Cb，缺失分量用上下最近的同类行插值
        const double crPos = static_cast<double>(y) / 2.0;
        const int cr0 = std::min(crCount - 1, static_cast<int>(std::floor(crPos)));
        const int cr1 = std::min(crCount - 1, cr0 + 1);
        const double crT = crPos - static_cast<double>(cr0);

        const double cbPos = static_cast<double>(y - 1) / 2.0;
        int cb0 = 0;
        int cb1 = 0;
        double cbT = 0.0;
        if (y == 0) {
            // 第 0 行没有上方奇行，直接用最近的奇行
            cb0 = cb1 = 0;
        } else {
            const double pos = (cbPos < 0.0) ? 0.0 : cbPos;
            cb0 = std::min(cbCount - 1, static_cast<int>(std::floor(pos)));
            cb1 = std::min(cbCount - 1, cb0 + 1);
            cbT = pos - static_cast<double>(cb0);
        }

        for (int x = 0; x < w; ++x) {
            const double frac = (w > 1) ? static_cast<double>(x) / static_cast<double>(w - 1)
                                        : 0.0;
            const double yv = yPlane[static_cast<size_t>(y) * w + x];

            const double crA = sampleChromaRow(crRows, cr0, frac);
            const double crB = sampleChromaRow(crRows, cr1, frac);
            const double cr = crA + (crB - crA) * crT;

            const double cbA = sampleChromaRow(cbRows, cb0, frac);
            const double cbB = sampleChromaRow(cbRows, cb1, frac);
            const double cb = cbA + (cbB - cbA) * cbT;

            // BT.601 反变换（与编码侧的 0.713 / 0.564 系数互为倒数）
            const double rv = yv + 1.402 * (cr - 128.0);
            const double bv = yv + 1.773 * (cb - 128.0);
            const double gv = (yv - 0.299 * rv - 0.114 * bv) / 0.587;

            const size_t idx = (static_cast<size_t>(y) * w + x) * 3;
            rgb[idx + 0] = static_cast<uint8_t>(std::lround(std::max(0.0, std::min(255.0, rv))));
            rgb[idx + 1] = static_cast<uint8_t>(std::lround(std::max(0.0, std::min(255.0, gv))));
            rgb[idx + 2] = static_cast<uint8_t>(std::lround(std::max(0.0, std::min(255.0, bv))));
        }
    }
}

} // namespace

// ---------------------------------------------------------------
// 对外接口
// ---------------------------------------------------------------

bool detectVis(const std::vector<float>& samples, int sampleRate, uint8_t* visCode)
{
    if (sampleRate < kMinDecodeSampleRate) {
        return false;
    }
    const LeaderInfo leader = findLeader(samples, sampleRate);
    if (!leader.found) {
        return false;
    }
    const VisInfo vis = readVis(samples, sampleRate, leader);
    if (!vis.found || !vis.parityOk) {
        return false;
    }
    if (visCode) {
        *visCode = vis.code;
    }
    return true;
}

DecodeResult decode(const std::vector<float>& samples, int sampleRate,
                    const ModeSpec* forceMode,
                    const std::function<void(int)>& progress)
{
    DecodeResult out;

    if (sampleRate < kMinDecodeSampleRate) {
        out.error = "采样率过低（当前 " + std::to_string(sampleRate) +
                    " Hz）：SSTV 每个像素只有几毫秒，采样率低于 " +
                    std::to_string(kMinDecodeSampleRate) +
                    " Hz 时无法可靠解调，请在设置中改为 44100 Hz。";
        return out;
    }
    if (samples.empty()) {
        out.error = "音频样本为空";
        return out;
    }
    if (static_cast<long long>(samples.size()) < msToSamples(500, sampleRate)) {
        out.error = "音频太短：SSTV 一个完整帧至少需要几十秒，这里不足以包含头部。";
        return out;
    }

    // ---- 1. 找 leader 与 VIS ----
    const LeaderInfo leader = findLeader(samples, sampleRate);
    if (!leader.found) {
        out.error = "未检测到 SSTV 信号（没有找到 1900 Hz 的 leader 导频音）。"
                    "请确认音频里有 SSTV 录音，且音量足够。";
        return out;
    }

    const VisInfo vis = readVis(samples, sampleRate, leader);
    out.visCode = vis.code;

    // ---- 2. 决定模式 ----
    const ModeSpec* spec = nullptr;
    if (forceMode) {
        spec = forceMode;
        if (!vis.found || !vis.parityOk) {
            out.warning = "未能确认 VIS 头部（信号可能噪声较大），已按你指定的模式解码。";
        } else if (vis.code != forceMode->visCode) {
            out.warning = "录音里的 VIS 与指定模式不一致（录音为 VIS=" +
                          std::to_string(static_cast<int>(vis.code)) +
                          "），已按你指定的模式解码，结果可能不正确。";
        }
    } else {
        if (!vis.found || !vis.parityOk) {
            out.error = "SSTV 头部（VIS）校验失败：信号可能太弱或格式不受支持。"
                        "可以在界面上手动指定模式后再试。";
            return out;
        }
        spec = modeSpecFromVisCode(vis.code);
        if (!spec) {
            out.error = "暂不支持这种 SSTV 模式（VIS=" +
                        std::to_string(static_cast<int>(vis.code)) +
                        "）。当前支持 Martin M1（44）与 Robot 36（8）。";
            return out;
        }
    }

    out.mode = spec;
    out.width = spec->width;
    out.height = spec->height;

    // ---- 3. 行同步预扫描 + 拟合 ----
    // 头部结束位置：leader 结束（break 开始）之后还有 break + VIS 起始 +
    // 7 数据位 + 偶校验 + 停止位，共 580 ms
    const long long headerEnd =
        leader.end + msToSamples(kBreakMs + kVisStartMs +
                                 static_cast<double>(kVisDataBits + 1) * kVisBitMs +
                                 kVisStopMs, sampleRate);

    const SyncModel sync = modelSync(samples, sampleRate, *spec, headerEnd);
    out.linesSynced = sync.detected;
    out.linesTotal = spec->height - 1;

    if (sync.detected == 0) {
        out.confidence = 0.0f;
    } else {
        out.confidence = clamp01(static_cast<double>(sync.detected) /
                                 std::max(1.0, static_cast<double>(spec->height - 1)));
    }
    // VIS 质量也计入置信度（最多扣 20%）
    out.confidence = static_cast<float>(clamp01(
        static_cast<double>(out.confidence) * (0.8 + 0.2 * vis.quality)));

    if (sync.detected < 2) {
        out.error = "没有锁定到图像行同步：信号可能是别种 SSTV 模式、"
                    "或被强噪声破坏（可尝试手动指定模式）。";
        return out;
    }
    if (sync.fitRejected) {
        // 波形里的行距与模式规范不符（例如录音用的是别的模式，
        // 或者发送端用的是把"同步 + 剩余时间三等分"的错误时序）。
        // 仍尝试出图，但明确把置信度压到很低，避免用户误以为解码正确。
        out.confidence = std::min(out.confidence, 0.15f);
        if (!out.warning.empty()) {
            out.warning += " ";
        }
        out.warning += "检测到的行速与 " + std::string(spec->name) +
                       " 的规范不符，图像可能完全错位。";
    }

    // ---- 4. 逐行解码 ----
    const int w = spec->width;
    const int h = spec->height;
    const double lineMs = spec->lineTimeMs;

    const size_t planeSize = static_cast<size_t>(w) * static_cast<size_t>(h);
    const size_t chromaStride = static_cast<size_t>(std::max(1, spec->chromaWidth));

    std::vector<double> yPlane(planeSize, 128.0);       // 色差模式的亮度
    std::vector<double> rPlane(planeSize, 0.0);         // RGB 直发模式的三平面
    std::vector<double> gPlane(planeSize, 0.0);
    std::vector<double> bPlane(planeSize, 0.0);
    std::vector<double> crRows((static_cast<size_t>(h / 2) + 2) * chromaStride, 128.0);
    std::vector<double> cbRows((static_cast<size_t>(h / 2) + 2) * chromaStride, 128.0);
    std::vector<uint8_t> rgb(planeSize * 3, 0);

    FmDemod fm(sampleRate, kChromaCenterHz);

    int crCount = 0;
    int cbCount = 0;
    int lastProgress = -1;

    for (int line = 0; line < h; ++line) {
        if (line > 0) {
            fm.resetContinuity();   // 行间可能跳过若干样本
        }

        const double lineStart = sync.a + sync.b * static_cast<double>(line);
        const double lineStep = sync.b;

        const LinePattern& pat = linePatternFor(*spec, line);
        double cumMs = 0.0;

        for (int i = 0; i < pat.count; ++i) {
            const Segment& seg = pat.segs[i];
            const double beforeMs = cumMs;
            cumMs += seg.durMs;

            const long long segStart = std::llround(
                lineStart + beforeMs / lineMs * lineStep);
            const long long segEnd = std::llround(
                lineStart + cumMs / lineMs * lineStep);
            if (segEnd <= segStart) {
                continue;
            }

            if (seg.type != SegType::Scan) {
                // 非扫描段也要推进滤波器状态，避免下一段起始出现暂态
                double dummyRe = 0.0;
                double dummyIm = 0.0;
                fm.process(samples, segStart, segEnd, false, 0, 0,
                           dummyRe, dummyIm);
                continue;
            }

            const int pixels = seg.pixels;
            const long long segLen = segEnd - segStart;
            if (pixels <= 0 || segLen <= 0) {
                continue;
            }

            // 目标平面
            std::vector<double>* dst = nullptr;
            size_t dstOffset = 0;
            size_t dstStride = 1;
            switch (seg.comp) {
                case Comp::Cr:
                    dst = &crRows;
                    dstOffset = static_cast<size_t>(crCount) * chromaStride;
                    break;
                case Comp::Cb:
                    dst = &cbRows;
                    dstOffset = static_cast<size_t>(cbCount) * chromaStride;
                    break;
                case Comp::Y:
                    dst = &yPlane;
                    dstOffset = static_cast<size_t>(line) * w;
                    break;
                case Comp::R:
                    dst = &rPlane;
                    dstOffset = static_cast<size_t>(line) * w;
                    break;
                case Comp::G:
                    dst = &gPlane;
                    dstOffset = static_cast<size_t>(line) * w;
                    break;
                case Comp::B:
                    dst = &bPlane;
                    dstOffset = static_cast<size_t>(line) * w;
                    break;
                default:
                    break;
            }
            if (!dst) {
                continue;
            }

            for (int k = 0; k < pixels; ++k) {
                const long long pa = segStart +
                    (segLen * static_cast<long long>(k)) / pixels;
                const long long pb = segStart +
                    (segLen * static_cast<long long>(k + 1)) / pixels;
                if (pb <= pa) {
                    continue;
                }

                double re = 0.0;
                double im = 0.0;
                fm.process(samples, pa, pb, true, 2, 1, re, im);

                double value = 128.0;
                if (fm.magnitude(re, im) > 1e-9) {
                    value = freqToValue(fm.freqFrom(re, im));
                }

                const size_t off = dstOffset + static_cast<size_t>(k) * dstStride;
                if (off < dst->size()) {
                    (*dst)[off] = value;
                }
            }

            if (seg.comp == Comp::Cr) {
                ++crCount;
            } else if (seg.comp == Comp::Cb) {
                ++cbCount;
            }
        }

        if (progress) {
            const int pct = static_cast<int>(
                (static_cast<double>(line + 1) / static_cast<double>(h)) * 100.0);
            if (pct != lastProgress && pct % 5 == 0) {
                lastProgress = pct;
                progress(pct);
            }
        }
    }
    if (progress) {
        progress(100);
    }

    // ---- 5. 组装 RGB ----
    if (spec->ycbcr) {
        if (crCount < 1 || cbCount < 1) {
            out.error = "色度通道缺失：Robot 36 的奇偶行结构不完整，"
                        "信号可能被截断或受噪声破坏。";
            return out;
        }
        assembleRgbYcc(*spec, yPlane, crRows, cbRows, crCount, cbCount, rgb);
    } else {
        // RGB 直发：三个平面各取一个通道
        for (size_t p = 0; p < planeSize; ++p) {
            rgb[p * 3 + 0] = static_cast<uint8_t>(
                std::lround(std::max(0.0, std::min(255.0, rPlane[p]))));
            rgb[p * 3 + 1] = static_cast<uint8_t>(
                std::lround(std::max(0.0, std::min(255.0, gPlane[p]))));
            rgb[p * 3 + 2] = static_cast<uint8_t>(
                std::lround(std::max(0.0, std::min(255.0, bPlane[p]))));
        }
    }

    out.rgb = std::move(rgb);
    out.ok = true;

    // 低置信度也要明说，不能只靠一个数字让用户自己猜
    if (out.confidence < 0.5f) {
        if (!out.warning.empty()) {
            out.warning += " ";
        }
        out.warning += "只有 " +
                       std::to_string(static_cast<int>(out.confidence * 100.0f)) +
                       "% 的图像行稳定锁定到同步，画面可能残缺或错位。";
    }
    return out;
}

} // namespace sstv
} // namespace audiot
