// test_sstv.cpp - SSTV 编解码测试
//
// 分层设计（避免"编解码自洽所以永远通过"的陷阱，这是历史上 SSTV 反复失败的根因）：
//   A 层：规范表结构自检——读表、不产波形。
//   B 层：波形核对——生产编码器出波，与测试内**独立**按标准字面常量生成的
//         参考波逐样本比对，并用"负对照"（故意用旧实现的错误模型生成参考波）
//         证明该测试确有分辨力。
//   C 层：独立生成器 -> 生产解码器（阶段 3 加入）。
// 最终确证仍需 D 层外部软件互操作（人工验收）。
#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>

#include <sstv/sstv_protocol.h>
#include <sstv/sstv_encoder.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using namespace audiot;
using namespace audiot::sstv;

// ================================================================
// A 层：规范表结构自检
// ================================================================

TEST_CASE("SSTV: all built-in mode specs pass structural validation", "[sstv][protocol]") {
    std::string err;
    const bool ok = validateAllModes(&err);
    INFO("validateAllModes 失败原因: " << err);
    REQUIRE(ok);
}

TEST_CASE("SSTV: header duration is 880 ms", "[sstv][protocol]") {
    // 300 leader + 10 break + 300 VIS start + 8x30 数据/校验 + 30 stop
    CHECK(headerDurationMs() == Approx(880.0f).margin(0.001f));
}

TEST_CASE("SSTV: Martin M1 spec matches published timing", "[sstv][protocol]") {
    const ModeSpec* m1 = modeSpecFromVisCode(44);
    REQUIRE(m1 != nullptr);
    CHECK(std::string(m1->name) == "Martin M1");
    CHECK(m1->width == 320);
    CHECK(m1->height == 256);
    CHECK(m1->ycbcr == false);
    CHECK(m1->lineTimeMs == Approx(446.446f).margin(0.001f));

    // 图像部分 114.29 s（256 行 x 446.446 ms）
    CHECK(m1->expectedImageSec == Approx(114.290f).margin(0.01f));
    // 整帧 = 头部 + 图像
    CHECK(frameDurationMs(*m1) / 1000.0f == Approx(115.17f).margin(0.01f));
}

TEST_CASE("SSTV: Robot 36 spec matches published timing", "[sstv][protocol]") {
    const ModeSpec* r36 = modeSpecFromVisCode(8);
    REQUIRE(r36 != nullptr);
    CHECK(std::string(r36->name) == "Robot 36");
    CHECK(r36->width == 320);
    CHECK(r36->height == 240);
    CHECK(r36->ycbcr == true);
    CHECK(r36->chromaWidth == 160);
    CHECK(r36->lineTimeMs == Approx(150.0f).margin(0.001f));

    // 锚点：模式名 36 就是帧时长——240 行 x 150 ms = 36.00 s。
    // 旧实现把行时长填成 88 ms，得到 21.1 s，与模式名直接矛盾。
    CHECK(r36->expectedImageSec == Approx(36.0f).margin(0.01f));
    CHECK(frameDurationMs(*r36) / 1000.0f == Approx(36.88f).margin(0.01f));
}

TEST_CASE("SSTV: Martin M1 line contains the porch segment after sync", "[sstv][protocol]") {
    // 专杀旧 bug②：旧实现"同步 + 三等分扫描"漏掉了紧跟同步的那段 1500 Hz
    // porch（0.572 ms）。这里直接断言段序列里存在它。
    const ModeSpec* m1 = modeSpecFromVisCode(44);
    REQUIRE(m1 != nullptr);
    const LinePattern& pat = linePatternFor(*m1, 0);
    REQUIRE(pat.count >= 2);

    CHECK(pat.segs[0].type == SegType::Sync);
    CHECK(pat.segs[0].freqHz == Approx(1200.0f).margin(0.5f));
    CHECK(pat.segs[1].type == SegType::Porch);
    CHECK(pat.segs[1].freqHz == Approx(1500.0f).margin(0.5f));
    CHECK(pat.segs[1].durMs == Approx(0.572f).margin(0.001f));

    // 整行 8 段：sync + porch + (扫描 + porch) x3
    CHECK(pat.count == 8);
}

TEST_CASE("SSTV: Martin M1 scan order is G-B-R", "[sstv][protocol]") {
    // 专杀旧 bug②：旧实现把所有模式都当成 R-Y/B-Y/Y。Martin M1 是 RGB 直发，
    // 顺序为绿、蓝、红。
    const ModeSpec* m1 = modeSpecFromVisCode(44);
    REQUIRE(m1 != nullptr);
    const LinePattern& pat = linePatternFor(*m1, 0);

    std::vector<Comp> order;
    for (int i = 0; i < pat.count; ++i) {
        if (pat.segs[i].type == SegType::Scan) {
            order.push_back(pat.segs[i].comp);
        }
    }
    REQUIRE(order.size() == 3);
    CHECK(order[0] == Comp::G);
    CHECK(order[1] == Comp::B);
    CHECK(order[2] == Comp::R);

    for (int i = 0; i < pat.count; ++i) {
        if (pat.segs[i].type == SegType::Scan) {
            CHECK(pat.segs[i].pixels == 320);
            CHECK(pat.segs[i].durMs == Approx(146.432f).margin(0.001f));
        }
    }
}

TEST_CASE("SSTV: Robot 36 has interleaved Cr/Cb line patterns", "[sstv][protocol]") {
    // 专杀旧 bug③：旧实现把 Robot 36 当成"同步 + 三个等长扫描段"，而它实际是
    // 奇偶行交织：每行都是 Y(88ms/320px) + 一个色差(44ms/160);偶行 Cr、奇行 Cb。
    const ModeSpec* r36 = modeSpecFromVisCode(8);
    REQUIRE(r36 != nullptr);
    CHECK(r36->patternCount == 2);

    const LinePattern& even = linePatternFor(*r36, 0);
    const LinePattern& odd = linePatternFor(*r36, 1);

    CHECK(&linePatternFor(*r36, 0) == &even);
    CHECK(&linePatternFor(*r36, 2) == &even);
    CHECK(&linePatternFor(*r36, 1) == &odd);
    CHECK(&linePatternFor(*r36, 239) == &odd);

    REQUIRE(even.count == 6);
    REQUIRE(odd.count == 6);

    CHECK(even.segs[2].type == SegType::Scan);
    CHECK(even.segs[2].comp == Comp::Y);
    CHECK(even.segs[2].durMs == Approx(88.0f).margin(0.001f));
    CHECK(even.segs[2].pixels == 320);

    CHECK(even.segs[5].type == SegType::Scan);
    CHECK(even.segs[5].comp == Comp::Cr);
    CHECK(even.segs[5].durMs == Approx(44.0f).margin(0.001f));
    CHECK(even.segs[5].pixels == 160);

    CHECK(odd.segs[5].type == SegType::Scan);
    CHECK(odd.segs[5].comp == Comp::Cb);

    CHECK(even.segs[4].type == SegType::Porch);
    CHECK(even.segs[4].freqHz == Approx(1900.0f).margin(0.5f));
    CHECK(even.segs[4].durMs == Approx(1.5f).margin(0.001f));

    // 自洽：Y 每像素 0.275 ms，色度每采样也是 0.275 ms（同一像素时钟）
    CHECK(even.segs[2].durMs / 320.0f == Approx(even.segs[5].durMs / 160.0f).margin(0.0005f));
}

TEST_CASE("SSTV: unknown VIS code is reported, not silently mapped", "[sstv][protocol]") {
    // 旧实现把未知 VIS 静默回退成 Martin M1，会把"解错模式"变成静默错误。
    REQUIRE(modeSpecFromVisCode(44) != nullptr);
    REQUIRE(modeSpecFromVisCode(8) != nullptr);
    CHECK(modeSpecFromVisCode(0) == nullptr);
    CHECK(modeSpecFromVisCode(255) == nullptr);
    CHECK(modeSpecFromVisCode(60) == nullptr);   // Scottie S1 未在本次范围内
}

TEST_CASE("SSTV: mode table rejects an intentionally broken spec", "[sstv][protocol]") {
    std::string err;

    // 行时长与段时长之和不符
    {
        static const Segment bad[] = {
            { SegType::Sync, Comp::None, 4.862f,  1200.0f,   0 },
            { SegType::Scan, Comp::G,  146.432f, 1500.0f, 320 },
        };
        static const LinePattern pats[] = { { bad, 2 } };
        ModeSpec spec = *modeSpecFromVisCode(44);
        spec.lineTimeMs = 500.0f;
        spec.patterns = pats;
        spec.patternCount = 1;
        CHECK_FALSE(validateModeSpec(spec, &err));
        CHECK_FALSE(err.empty());
    }

    // 扫描段像素数与宽度不符
    {
        static const Segment bad[] = {
            { SegType::Sync, Comp::None, 4.862f,  1200.0f,   0 },
            { SegType::Scan, Comp::G,  141.584f, 1500.0f, 256 },
        };
        static const LinePattern pats[] = { { bad, 2 } };
        ModeSpec spec = *modeSpecFromVisCode(44);
        spec.lineTimeMs = 146.446f;
        spec.expectedImageSec = 146.446f * 256.0f / 1000.0f;
        spec.patterns = pats;
        spec.patternCount = 1;
        CHECK_FALSE(validateModeSpec(spec, &err));
    }
}

// ================================================================
// B 层：波形核对（生产编码器 -> 测试内独立按标准字面常量生成的参考波）
//
// 为什么不用"逐段测频率"：Martin M1 的 porch 只有 0.572 ms（25 个样本），
// 而它与相邻段相差 300~800 Hz；25 个样本的频率分辨极限约 fs/N = 1.7 kHz，
// **任何**方法都无法在该窗口内分辨。所以改为逐样本比对参考波，并配"负对照"
// 证明本测试确有分辨力——负对照必须大面积失配。
// ================================================================

namespace {

constexpr double kTestTwoPi = 6.283185307179586476925286766559;

// ---- 标准字面常量（独立于生产代码，作为评分标准） ----
constexpr double kRefLeaderHz   = 1900.0;
constexpr double kRefBreakHz    = 1200.0;
constexpr double kRefVisOneHz   = 1100.0;
constexpr double kRefVisZeroHz  = 1300.0;
constexpr double kRefBlackHz    = 1500.0;
constexpr double kRefWhiteHz    = 2300.0;
constexpr double kRefM1LineMs   = 446.446;
constexpr double kRefM1FrameMs  = 115170.176;
constexpr double kRefR36LineMs  = 150.0;
constexpr double kRefR36FrameMs = 36880.0;

// 参考段：kind 0=sync 1=porch 2=scan；comp 1=Y 2=Cr 3=Cb 4=R 5=G 6=B 0=none
struct RefSeg {
    int kind;
    double ms;
    float freq;
    int comp;
    int pixels;
};

// Martin M1 标准行：sync 4.862 / porch 0.572 / G 146.432 / porch / B / porch / R / porch
const RefSeg kRefM1Std[] = {
    { 0,   4.862, 1200.0f, 0,   0 },
    { 1,   0.572, 1500.0f, 0,   0 },
    { 2, 146.432,    0.0f, 5, 320 },
    { 1,   0.572, 1500.0f, 0,   0 },
    { 2, 146.432,    0.0f, 6, 320 },
    { 1,   0.572, 1500.0f, 0,   0 },
    { 2, 146.432,    0.0f, 4, 320 },
    { 1,   0.572, 1500.0f, 0,   0 },
};
// 负对照①：旧实现的同步加三等分扫描，漏掉全部 porch
const RefSeg kRefM1NoPorch[] = {
    { 0,   4.862, 1200.0f, 0,   0 },
    { 2, 147.1947,   0.0f, 5, 320 },
    { 2, 147.1947,   0.0f, 6, 320 },
    { 2, 147.1947,   0.0f, 4, 320 },
};
// 负对照②：色彩顺序改成 R-B-G
const RefSeg kRefM1WrongOrder[] = {
    { 0,   4.862, 1200.0f, 0,   0 },
    { 1,   0.572, 1500.0f, 0,   0 },
    { 2, 146.432,    0.0f, 4, 320 },
    { 1,   0.572, 1500.0f, 0,   0 },
    { 2, 146.432,    0.0f, 6, 320 },
    { 1,   0.572, 1500.0f, 0,   0 },
    { 2, 146.432,    0.0f, 5, 320 },
    { 1,   0.572, 1500.0f, 0,   0 },
};

// Robot 36 标准行型：偶行 Cr、奇行 Cb
const RefSeg kRefR36Even[] = {
    { 0,  9.0, 1200.0f, 0,   0 },
    { 1,  3.0, 1500.0f, 0,   0 },
    { 2, 88.0,    0.0f, 1, 320 },
    { 1,  4.5, 1500.0f, 0,   0 },
    { 1,  1.5, 1900.0f, 0,   0 },
    { 2, 44.0,    0.0f, 2, 160 },
};
const RefSeg kRefR36Odd[] = {
    { 0,  9.0, 1200.0f, 0,   0 },
    { 1,  3.0, 1500.0f, 0,   0 },
    { 2, 88.0,    0.0f, 1, 320 },
    { 1,  4.5, 2300.0f, 0,   0 },
    { 1,  1.5, 1900.0f, 0,   0 },
    { 2, 44.0,    0.0f, 3, 160 },
};
// 负对照③：旧实现把 Robot 36 也当成同步加三等分（且用 R/G/B 色空间）
const RefSeg kRefR36EqualThirds[] = {
    { 0,   9.0, 1200.0f, 0,   0 },
    { 2, 141.0,    0.0f, 4, 320 },
    { 2, 141.0,    0.0f, 6, 320 },
    { 2, 141.0,    0.0f, 5, 320 },
};

// ---- 标准公式（测试侧独立复算） ----
inline double lumaOf(double r, double g, double b)
{
    return 0.299 * r + 0.587 * g + 0.114 * b;
}
inline double crOf(double r, double g, double b)
{
    return (r - lumaOf(r, g, b)) * 0.713 + 128.0;
}
inline double cbOf(double r, double g, double b)
{
    return (b - lumaOf(r, g, b)) * 0.564 + 128.0;
}
inline double freqOf(double v)
{
    const double c = std::max(0.0, std::min(255.0, v));
    return kRefBlackHz + (kRefWhiteHz - kRefBlackHz) * (c / 255.0);
}

// 分量取值：comp 1=Y 2=Cr 3=Cb 4=R 5=G 6=B
double compValue(const uint8_t* px, int comp)
{
    const double R = px[0], G = px[1], B = px[2];
    switch (comp) {
        case 1: return lumaOf(R, G, B);
        case 2: return crOf(R, G, B);
        case 3: return cbOf(R, G, B);
        case 4: return R;
        case 5: return G;
        case 6: return B;
        default: return 128.0;
    }
}

std::vector<uint8_t> makeSolidImage(int w, int h, uint8_t r, uint8_t g, uint8_t b)
{
    std::vector<uint8_t> img(static_cast<size_t>(w) * static_cast<size_t>(h) * 3);
    for (size_t i = 0; i < img.size(); i += 3) {
        img[i + 0] = r;
        img[i + 1] = g;
        img[i + 2] = b;
    }
    return img;
}

// 红色按每 n 像素切换一次；用于验证色度水平盒滤波
std::vector<uint8_t> makeRedBandedImage(int w, int h, int n)
{
    std::vector<uint8_t> img(static_cast<size_t>(w) * static_cast<size_t>(h) * 3);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t i = (static_cast<size_t>(y) * w + x) * 3;
            const bool hi = ((x / n) % 2) != 0;
            img[i + 0] = hi ? 200 : 100;
            img[i + 1] = 128;
            img[i + 2] = 128;
        }
    }
    return img;
}

// 独立参考生成器：段边界一律用从帧首起的绝对毫秒直接换算，
// 不引用生产代码里的任何表或函数。
std::vector<float> refGenerate(const std::vector<uint8_t>& img, int w, int h,
                               const RefSeg* evenSegs, int nEven,
                               const RefSeg* oddSegs, int nOdd,
                               uint8_t visCode, double lineMs, double frameMs,
                               int sr, float amp)
{
    const double spm = sr / 1000.0;
    std::vector<float> out;
    out.reserve(static_cast<size_t>(std::llround(frameMs * spm)));
    double phase = 0.0;

    // 发射 [msStart, msEnd) 区间内的一段固定频率（按绝对样本位置取整）
    const auto tone = [&](double f, double msStart, double msEnd) {
        const long long a = std::llround(msStart * spm);
        const long long b = std::llround(msEnd * spm);
        const double step = kTestTwoPi * f / sr;
        for (long long i = a; i < b; ++i) {
            out.push_back(amp * static_cast<float>(std::sin(phase)));
            phase += step;
            if (phase >= kTestTwoPi) phase -= kTestTwoPi;
        }
    };

    // ---- 头部（880 ms） ----
    tone(kRefLeaderHz, 0.0, 300.0);
    tone(kRefBreakHz, 300.0, 310.0);
    tone(kRefBreakHz, 310.0, 610.0);            // VIS 起始位 300 ms @1200
    double ms = 610.0;
    int ones = 0;
    for (int i = 0; i < 7; ++i) {
        const bool bit = ((visCode >> i) & 0x01) != 0;
        if (bit) ++ones;
        tone(bit ? kRefVisOneHz : kRefVisZeroHz, ms, ms + 30.0);
        ms += 30.0;
    }
    tone(((ones % 2) != 0) ? kRefVisOneHz : kRefVisZeroHz, ms, ms + 30.0);
    ms += 30.0;
    tone(kRefBreakHz, ms, ms + 30.0);           // 停止位
    ms += 30.0;

    // ---- 行扫描 ----
    for (int line = 0; line < h; ++line) {
        const RefSeg* segs = evenSegs;
        int n = nEven;
        if (nOdd > 0) {
            const bool odd = (line % 2) != 0;
            segs = odd ? oddSegs : evenSegs;
            n = odd ? nOdd : nEven;
        }
        const double lineBase = ms;
        const uint8_t* row =
            img.data() + static_cast<size_t>(line) * static_cast<size_t>(w) * 3;

        double cum = 0.0;
        for (int i = 0; i < n; ++i) {
            const RefSeg& s = segs[i];
            const double segStartMs = lineBase + cum;
            const double segEndMs = segStartMs + s.ms;
            if (s.kind != 2) {
                tone(s.freq, segStartMs, segEndMs);
            } else {
                const long long a = std::llround(segStartMs * spm);
                const long long b = std::llround(segEndMs * spm);
                const long long segLen = b - a;
                const int srcPerSample = (w % s.pixels == 0) ? (w / s.pixels) : 1;
                for (int k = 0; k < s.pixels; ++k) {
                    const long long pa = a + std::llround(
                        static_cast<double>(k) * segLen / s.pixels);
                    const long long pb = a + std::llround(
                        static_cast<double>(k + 1) * segLen / s.pixels);
                    double v = 0.0;
                    for (int t = 0; t < srcPerSample; ++t) {
                        const int x = k * srcPerSample + t;
                        if (x >= w) break;
                        v += compValue(row + static_cast<size_t>(x) * 3, s.comp);
                    }
                    v /= static_cast<double>(srcPerSample);
                    const double f = freqOf(v);
                    const double step = kTestTwoPi * f / sr;
                    for (long long q = pa; q < pb; ++q) {
                        out.push_back(amp * static_cast<float>(std::sin(phase)));
                        phase += step;
                        if (phase >= kTestTwoPi) phase -= kTestTwoPi;
                    }
                }
            }
            cum += s.ms;
        }
        ms = lineBase + lineMs;
    }
    return out;
}

// 失配比例：逐样本比较（取较短长度），返回 |diff| > tol 的样本占比。
// 结构正确时只有段边界处个别样本因取整差 1 个样本而不同（<0.5%）；
// 一旦结构错了（漏段/顺序错/时序错），绝大部分样本都会失配。
double mismatchRatio(const std::vector<float>& a, const std::vector<float>& b,
                     double tol)
{
    const size_t n = std::min(a.size(), b.size());
    if (n == 0) {
        return 1.0;
    }
    size_t bad = 0;
    for (size_t i = 0; i < n; ++i) {
        if (std::fabs(static_cast<double>(a[i]) -
                      static_cast<double>(b[i])) > tol) {
            ++bad;
        }
    }
    return static_cast<double>(bad) / static_cast<double>(n);
}

// 在窗口内估计平均瞬时频率（正交解调 + 一阶低通 + lag-1 相位增量）。
// 只用于长窗口（>=150 样本）的定点核对；短窗口分辨力不足，不作为判据。
double estimateFreq(const std::vector<float>& s, long long start, long long len,
                    int sr, double f0)
{
    if (len < 150 || start < 0 || start + len > static_cast<long long>(s.size())) {
        return -1.0;
    }
    const double a = std::min(0.05, 6.0 / static_cast<double>(len));
    const long long skip = len / 3;
    const double w = kTestTwoPi * f0 / static_cast<double>(sr);
    const double cw = std::cos(w);
    const double sw = std::sin(w);
    double c = std::cos(w * static_cast<double>(start));
    double sn = std::sin(w * static_cast<double>(start));
    double loI = 0.0, loQ = 0.0, pI = 0.0, pQ = 0.0, accRe = 0.0, accIm = 0.0;
    bool have = false;
    for (long long i = 0; i < len; ++i) {
        const double x = s[static_cast<size_t>(start + i)];
        loI += a * (x * c - loI);
        loQ += a * (-x * sn - loQ);
        if (have && i >= skip) {
            accRe += pI * loI + pQ * loQ;
            accIm += pI * loQ - loI * pQ;
        }
        pI = loI;
        pQ = loQ;
        have = true;
        const double nc = c * cw - sn * sw;
        sn = sn * cw + c * sw;
        c = nc;
    }
    return f0 + std::atan2(accIm, accRe) * static_cast<double>(sr) / kTestTwoPi;
}

double windowRms(const std::vector<float>& s, long long start, long long len)
{
    if (len <= 0 || start < 0 || start + len > static_cast<long long>(s.size())) {
        return 0.0;
    }
    double sum = 0.0;
    for (long long i = 0; i < len; ++i) {
        const double v = s[static_cast<size_t>(start + i)];
        sum += v * v;
    }
    return std::sqrt(sum / static_cast<double>(len));
}

// 第 line 行第 segIndex 段的绝对样本位置（按标准毫秒时序算）
struct SegRef { long long start; long long len; };

SegRef segRef(const ModeSpec& spec, int line, int segIndex, int sr)
{
    const LinePattern& pat = linePatternFor(spec, line);
    const double spm = sr / 1000.0;
    double ms = headerDurationMs() + spec.lineTimeMs * static_cast<double>(line);
    for (int i = 0; i < segIndex; ++i) {
        ms += pat.segs[i].durMs;
    }
    const long long start = std::llround(ms * spm);
    const long long end = std::llround((ms + pat.segs[segIndex].durMs) * spm);
    return SegRef{ start, end - start };
}

} // namespace

TEST_CASE("SSTV: frequency estimator self-check (long windows)", "[sstv][measure]") {
    // 测量工具本身必须先被验证，否则基于它的定点核对都不可信。
    const int sr = 44100;
    const double freqs[] = { 1200.0, 1500.0, 1900.0, 2300.0 };
    for (double f : freqs) {
        std::vector<float> tone(6000);
        for (size_t i = 0; i < tone.size(); ++i) {
            tone[i] = 0.8f * static_cast<float>(
                std::sin(kTestTwoPi * f * static_cast<double>(i) / sr));
        }
        const double est = estimateFreq(tone, 200, 2000, sr, f - 40.0);
        INFO("f=" << f << " est=" << est);
        CHECK(std::fabs(est - f) < 4.0);
    }
}

TEST_CASE("SSTV: Martin M1 waveform matches the standard reference; old model rejected",
          "[sstv][encoder]") {
    const int sr = 44100;
    const ModeSpec* m1 = modeSpecFromVisCode(44);
    REQUIRE(m1 != nullptr);

    // 用一张三通道互不相同的图，任何色彩顺序错误都会暴露
    const std::vector<uint8_t> img =
        makeSolidImage(m1->width, m1->height, 40, 200, 90);
    const std::vector<float> enc = encodeImage(img.data(), *m1, sr, 0.8f);
    REQUIRE(!enc.empty());

    // 帧长必须等于标准帧长：多出来的准备音/帧尾静音都会让它变长
    CHECK(static_cast<long long>(enc.size()) == encodedSampleCount(*m1, sr));
    CHECK(std::llround(frameDurationMs(*m1) * sr / 1000.0) ==
          std::llround(kRefM1FrameMs * sr / 1000.0));

    const std::vector<float> ref = refGenerate(
        img, m1->width, m1->height, kRefM1Std, 8, nullptr, 0, 44,
        kRefM1LineMs, kRefM1FrameMs, sr, 0.8f);

    CHECK(std::llabs(static_cast<long long>(enc.size()) -
                     static_cast<long long>(ref.size())) <= 2);

    const double match = mismatchRatio(enc, ref, 0.05);
    INFO("M1 与标准参考的失配比例=" << match);
    CHECK(match < 0.005);

    // 负对照①：漏掉 porch 的旧模型 —— 必须大面积失配，证明本测试有分辨力
    const std::vector<float> refNoPorch = refGenerate(
        img, m1->width, m1->height, kRefM1NoPorch, 4, nullptr, 0, 44,
        kRefM1LineMs, kRefM1FrameMs, sr, 0.8f);
    const double mismatchNoPorch = mismatchRatio(enc, refNoPorch, 0.05);
    INFO("与[漏 porch 旧模型]的失配比例=" << mismatchNoPorch);
    CHECK(mismatchNoPorch > 0.10);

    // 负对照②：色彩顺序错（R-B-G）—— 同样必须大面积失配
    const std::vector<float> refWrongOrder = refGenerate(
        img, m1->width, m1->height, kRefM1WrongOrder, 8, nullptr, 0, 44,
        kRefM1LineMs, kRefM1FrameMs, sr, 0.8f);
    const double mismatchOrder = mismatchRatio(enc, refWrongOrder, 0.05);
    INFO("与[色彩顺序错旧模型]的失配比例=" << mismatchOrder);
    CHECK(mismatchOrder > 0.10);

    // 定点核对（长窗口）：sync 必须落在 1200 Hz
    const SegRef sync = segRef(*m1, 0, 0, sr);
    const double syncHz = estimateFreq(enc, sync.start, sync.len - 4, sr, 1200.0);
    INFO("sync 实测=" << syncHz);
    CHECK(std::fabs(syncHz - 1200.0) < 15.0);

    // 首尾都必须有信号（若追加了静音，RMS 会塌掉）
    CHECK(windowRms(enc, 0, 441) > 0.2);
    CHECK(windowRms(enc, static_cast<long long>(enc.size()) - 441, 441) > 0.2);
}

TEST_CASE("SSTV: Robot 36 waveform matches the standard reference; equal-thirds model rejected",
          "[sstv][encoder]") {
    const int sr = 44100;
    const ModeSpec* r36 = modeSpecFromVisCode(8);
    REQUIRE(r36 != nullptr);

    const std::vector<uint8_t> img =
        makeSolidImage(r36->width, r36->height, 60, 140, 210);
    const std::vector<float> enc = encodeImage(img.data(), *r36, sr, 0.8f);
    REQUIRE(!enc.empty());

    CHECK(static_cast<long long>(enc.size()) == encodedSampleCount(*r36, sr));
    CHECK(std::llround(frameDurationMs(*r36) * sr / 1000.0) ==
          std::llround(kRefR36FrameMs * sr / 1000.0));

    const std::vector<float> ref = refGenerate(
        img, r36->width, r36->height, kRefR36Even, 6, kRefR36Odd, 6, 8,
        kRefR36LineMs, kRefR36FrameMs, sr, 0.8f);

    CHECK(std::llabs(static_cast<long long>(enc.size()) -
                     static_cast<long long>(ref.size())) <= 2);

    const double match = mismatchRatio(enc, ref, 0.05);
    INFO("R36 与标准参考的失配比例=" << match);
    CHECK(match < 0.005);

    // 负对照③：旧实现的同步加三等分模型 —— 必须大面积失配
    const std::vector<float> refThirds = refGenerate(
        img, r36->width, r36->height, kRefR36EqualThirds, 4, nullptr, 0, 8,
        kRefR36LineMs, kRefR36FrameMs, sr, 0.8f);
    const double mismatchThirds = mismatchRatio(enc, refThirds, 0.05);
    INFO("与[三等分旧模型]的失配比例=" << mismatchThirds);
    CHECK(mismatchThirds > 0.10);

    // 定点核对（长窗口）：偶行 sep 1500 Hz、奇行 sep 2300 Hz
    // （该段 4.5 ms ≈ 198 样本，估计器在此窗口长度上仍有约 20 Hz 系统偏差；
    //   两个频率相差 800 Hz，容差 40 Hz 足以判定）
    const SegRef sepEven = segRef(*r36, 0, 3, sr);
    const double evenHz = estimateFreq(enc, sepEven.start, sepEven.len - 4, sr, 1500.0);
    INFO("偶行 sep 实测=" << evenHz);
    CHECK(std::fabs(evenHz - 1500.0) < 40.0);

    const SegRef sepOdd = segRef(*r36, 1, 3, sr);
    const double oddHz = estimateFreq(enc, sepOdd.start, sepOdd.len - 4, sr, 2300.0);
    INFO("奇行 sep 实测=" << oddHz);
    CHECK(std::fabs(oddHz - 2300.0) < 40.0);

    CHECK(windowRms(enc, 0, 441) > 0.2);
    CHECK(windowRms(enc, static_cast<long long>(enc.size()) - 441, 441) > 0.2);
}

TEST_CASE("SSTV: Robot 36 chroma is horizontally averaged 2:1 and uses BT.601",
          "[sstv][encoder]") {
    const int sr = 44100;
    const ModeSpec* r36 = modeSpecFromVisCode(8);
    REQUIRE(r36 != nullptr);

    // 红色每像素交替 -> 色度按 2:1 盒滤波后应成为恒定值（两条像素的平均）。
    // 若编码器漏了降采样（逐像素发 320 点），该段会交替出现两个相差约 157 Hz
    // 的频率，长窗口测出的值会明显偏离平均值。
    {
        const std::vector<uint8_t> img = makeRedBandedImage(r36->width, r36->height, 1);
        const std::vector<float> enc = encodeImage(img.data(), *r36, sr, 0.8f);
        REQUIRE(!enc.empty());

        const double avgCr = (crOf(100, 128, 128) + crOf(200, 128, 128)) / 2.0;
        const double expected = freqOf(avgCr);   // 约 1936 Hz

        const SegRef seg = segRef(*r36, 0, 5, sr);
        const double est = estimateFreq(enc, seg.start + seg.len / 4,
                                        seg.len / 2, sr, expected);
        INFO("色度段(降采样后) 期望=" << expected << " 实测=" << est);
        CHECK(std::fabs(est - expected) < 20.0);
    }

    // 纯红 / 纯蓝：标准 BT.601 系数会把色差推到饱和（255 -> 2300 Hz）；
    // 旧实现的 (R-Y)*0.5 系数只能到约 217（-> 2181 Hz），两者相差 119 Hz。
    {
        const std::vector<uint8_t> img =
            makeSolidImage(r36->width, r36->height, 255, 0, 0);
        const std::vector<float> enc = encodeImage(img.data(), *r36, sr, 0.8f);
        REQUIRE(!enc.empty());
        const SegRef seg = segRef(*r36, 0, 5, sr);
        const double est = estimateFreq(enc, seg.start + seg.len / 4,
                                        seg.len / 2, sr, 2300.0);
        INFO("纯红色差 Cr 实测=" << est << " (标准应饱和到 2300)");
        CHECK(est > 2250.0);
    }
    {
        const std::vector<uint8_t> img =
            makeSolidImage(r36->width, r36->height, 0, 0, 255);
        const std::vector<float> enc = encodeImage(img.data(), *r36, sr, 0.8f);
        REQUIRE(!enc.empty());
        const SegRef seg = segRef(*r36, 1, 5, sr);   // 奇行是 Cb
        const double est = estimateFreq(enc, seg.start + seg.len / 4,
                                        seg.len / 2, sr, 2300.0);
        INFO("纯蓝色差 Cb 实测=" << est << " (标准应饱和到 2300)");
        CHECK(est > 2250.0);
    }
}
