// qam16_modem.cpp - 16-QAM modem implementation
#include "qam16_modem.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace audiot {

namespace {

// 每轴 4 个电平，按标签 v（2 bit 值）索引。电平顺序 {-3,-1,+3,+1}
// 保证：**电平相邻（最易混淆）的两个星座点，其标签只差 1 bit**
// （Gray 映射），单点误判只影响 1 bit。
const float kLevels[4] = { -3.0f, -1.0f, 3.0f, 1.0f };
// sqrt(10)：16-QAM 平均功率 (E[I^2]+E[Q^2]=10) 归一化到 1
const float kNorm = 3.16227766f;

inline float levelValue(int v) {
    return kLevels[v & 3] / kNorm;
}

// 星座点：idx 高 2 位给 I，(低 2 位给 Q)
inline void constellation(int idx, float& I, float& Q) {
    I = levelValue((idx >> 2) & 3);
    Q = levelValue(idx & 3);
}

// 最近电平反查（枚举 4 个电平，数组非单调）
inline int nearestLevel(float x) {
    int best = 0;
    float bestD = std::numeric_limits<float>::max();
    for (int v = 0; v < 4; ++v) {
        const float d = x - levelValue(v);
        if (d * d < bestD) {
            bestD = d * d;
            best = v;
        }
    }
    return best;
}

} // namespace

QAM16Modem::QAM16Modem(const Config& config)
    : config_(config)
{
}

QAM16Modem::~QAM16Modem() {}

int QAM16Modem::sampleRate() const {
    return config_.sampleRate;
}

ModemType QAM16Modem::type() const {
    return ModemType::QAM;
}

int QAM16Modem::bitsPerSymbol() const {
    return 4;
}

int QAM16Modem::samplesPerSymbol() const {
    if (config_.symbolRate <= 0) return 0;
    return config_.sampleRate / config_.symbolRate;
}

int QAM16Modem::bitsPerSecond() const {
    return config_.symbolRate * bitsPerSymbol();
}

std::vector<uint8_t> QAM16Modem::generatePreamble() const {
    // 轮转遍历全部 16 个星座点：既便于同步匹配，也让信道增益估计覆盖
    // 各幅度/相位，估计更稳。
    std::vector<uint8_t> p(static_cast<size_t>(config_.preambleSymbols));
    for (int i = 0; i < config_.preambleSymbols; ++i) {
        p[static_cast<size_t>(i)] = static_cast<uint8_t>(i % 16);
    }
    return p;
}

std::vector<float> QAM16Modem::modulate(const std::vector<uint8_t>& bits) {
    const int sps = samplesPerSymbol();
    const int bps = bitsPerSymbol();
    if (sps <= 0 || bits.empty()) {
        return std::vector<float>();
    }

    // 1) 组装符号序列：前导 + 数据（每 4 bit 凑一个符号，MSB first）。
    //    先把比特流补齐到 4 位的整数倍（末尾补 0），否则最后不足一个符号
    //    的比特会被丢弃，接收端拿到截断流（帧尾 CRC 不全而解码失败）。
    std::vector<uint8_t> payload = bits;
    while (payload.size() % static_cast<size_t>(bps) != 0) {
        payload.push_back(0);
    }

    std::vector<uint8_t> symbols;
    const std::vector<uint8_t> pre = generatePreamble();
    symbols.reserve(pre.size() + payload.size() / static_cast<size_t>(bps) + 1);
    symbols.insert(symbols.end(), pre.begin(), pre.end());
    for (size_t i = 0; i + static_cast<size_t>(bps) <= payload.size();
         i += static_cast<size_t>(bps)) {
        int idx = 0;
        for (int b = 0; b < bps; ++b) {
            idx = (idx << 1) | (payload[i + static_cast<size_t>(b)] & 1);
        }
        symbols.push_back(static_cast<uint8_t>(idx & 0xF));
    }

    const double twoPi = 2.0 * static_cast<double>(M_PI);
    const double phaseInc = twoPi * static_cast<double>(config_.carrierFreq) /
                            static_cast<double>(config_.sampleRate);

    // 2) 矩形脉冲 + 相位连续载波合成。载波取符号率的整数倍，
    //    每符号内相位推进整数个周期，符号边界自然衔接。
    std::vector<float> out;
    out.reserve(symbols.size() * static_cast<size_t>(sps));
    double phase = 0.0;
    for (size_t k = 0; k < symbols.size(); ++k) {
        float I = 0.0f, Q = 0.0f;
        constellation(symbols[k], I, Q);
        for (int i = 0; i < sps; ++i) {
            const float sample = I * static_cast<float>(std::cos(phase))
                               - Q * static_cast<float>(std::sin(phase));
            out.push_back(sample * config_.amplitude);
            phase += phaseInc;
            if (phase >= twoPi) phase -= twoPi;
        }
    }
    return out;
}

std::vector<uint8_t> QAM16Modem::demodulate(const std::vector<float>& samples) {
    const int sps = samplesPerSymbol();
    const int bps = bitsPerSymbol();
    const int P = config_.preambleSymbols;
    if (sps <= 0 || samples.empty() || P < 2) {
        return std::vector<uint8_t>();
    }

    const int totalSymbols = static_cast<int>(samples.size()) / sps;
    if (totalSymbols < P + 1) {
        return std::vector<uint8_t>();
    }

    const double twoPi = 2.0 * static_cast<double>(M_PI);
    const double phaseInc = twoPi * static_cast<double>(config_.carrierFreq) /
                            static_cast<double>(config_.sampleRate);
    const std::vector<uint8_t> pre = generatePreamble();

    // 符号内本地振荡器查表
    std::vector<float> loCos(static_cast<size_t>(sps));
    std::vector<float> loSin(static_cast<size_t>(sps));
    for (int i = 0; i < sps; ++i) {
        loCos[static_cast<size_t>(i)] = static_cast<float>(std::cos(phaseInc * i));
        loSin[static_cast<size_t>(i)] = static_cast<float>(std::sin(phaseInc * i));
    }

    // 判决导向（DD）LMS 更新步长：数据段微调信道增益
    const float kMu = 0.02f;

    int bestScore = -1;
    float bestMetric = -1.0f;
    std::vector<uint8_t> bestBits;

    for (int ph = 0; ph < sps; ++ph) {
        // 逐符号正交积分：z_k = g · s_k（g 为含载波公共相位的复数信道增益）
        std::vector<float> zRe, zIm, energy;
        zRe.reserve(static_cast<size_t>(totalSymbols));
        zIm.reserve(static_cast<size_t>(totalSymbols));
        energy.reserve(static_cast<size_t>(totalSymbols));
        for (int k = 0; k < totalSymbols; ++k) {
            const int start = ph + k * sps;
            if (start + sps > static_cast<int>(samples.size())) break;
            float accRe = 0.0f;
            float accIm = 0.0f;
            for (int i = 0; i < sps; ++i) {
                const float x = samples[static_cast<size_t>(start + i)];
                accRe += x * loCos[static_cast<size_t>(i)];
                accIm -= x * loSin[static_cast<size_t>(i)];
            }
            zRe.push_back(accRe);
            zIm.push_back(accIm);
            energy.push_back(accRe * accRe + accIm * accIm);
        }
        const int n = static_cast<int>(zRe.size());
        if (n < P + 1) continue;

        // 能量门限定位信号起点：prep tone（1200 Hz 纯音）对 2100 Hz 载波
        // 正交积分能量极低，故第一个明显超门限的符号即前导起点。
        float maxE = 0.0f;
        for (int k = 0; k < n; ++k) maxE = std::max(maxE, energy[static_cast<size_t>(k)]);
        if (maxE <= 1e-9f) continue;
        const float gate = maxE * 0.2f;
        int k0 = -1;
        for (int k = 0; k < n; ++k) {
            if (energy[static_cast<size_t>(k)] > gate) { k0 = k; break; }
        }
        if (k0 < 0 || k0 + P + 1 > n) continue;

        // 前导兼作导频：最小二乘估计复数信道增益 g = Σ z·conj(s) / Σ|s|²
        std::complex<float> g(0.0f, 0.0f);
        float norm = 0.0f;
        for (int p = 0; p < P; ++p) {
            float I = 0.0f, Q = 0.0f;
            constellation(pre[static_cast<size_t>(p)], I, Q);
            const std::complex<float> s(I, Q);
            const std::complex<float> z(zRe[static_cast<size_t>(k0 + p)],
                                        zIm[static_cast<size_t>(k0 + p)]);
            g += z * std::conj(s);
            norm += std::norm(s);
        }
        if (norm <= 1e-6f) continue;
        g /= norm;
        const float gPow = std::norm(g);
        if (gPow <= 1e-9f) continue;

        // 前导匹配分数：用估计出的 g 均衡前导，判决后与已知符号比较
        int score = 0;
        for (int p = 0; p < P; ++p) {
            const std::complex<float> z(zRe[static_cast<size_t>(k0 + p)],
                                        zIm[static_cast<size_t>(k0 + p)]);
            const std::complex<float> sHat = z * std::conj(g) / gPow;
            const int idx = (nearestLevel(sHat.real()) << 2) | nearestLevel(sHat.imag());
            if (idx == pre[static_cast<size_t>(p)]) ++score;
        }

        // 相位对齐判据：用前导相关的幅度 |g|，而不是硬判决匹配数。
        // 前导覆盖全部 16 个星座点，只要窗口大致落在信号上，用前导自身
        // 估计的 g 做"自拟合"就能得到高匹配数（实测偏差 11 个样本仍得
        // 满分），无法区分相位；而 |g| 在窗口精确落在符号边界时最大
        // （相邻符号的串扰会削弱前导相关幅度）。
        const float metric = std::sqrt(gPow);
        if (metric > bestMetric) {
            bestMetric = metric;
            bestScore = score;

            // 用当前 g 解码数据段，并用 DD 逐符号跟踪
            std::complex<float> gTrack = g;
            std::vector<uint8_t> bits;
            bits.reserve(static_cast<size_t>(n - k0 - P) * static_cast<size_t>(bps));
            for (int k = k0 + P; k < n; ++k) {
                const std::complex<float> z(zRe[static_cast<size_t>(k)],
                                            zIm[static_cast<size_t>(k)]);
                const float gp = std::norm(gTrack);
                if (gp <= 1e-9f) break;
                const std::complex<float> sHat = z * std::conj(gTrack) / gp;

                const int vi = nearestLevel(sHat.real());
                const int vq = nearestLevel(sHat.imag());
                const int idx = (vi << 2) | vq;
                for (int b = bps - 1; b >= 0; --b) {
                    bits.push_back(static_cast<uint8_t>((idx >> b) & 1));
                }

                // DD-LMS：仅当判决可信时微调 g（用重构星座点做参考）
                float rI = 0.0f, rQ = 0.0f;
                constellation(idx, rI, rQ);
                const std::complex<float> sRef(rI, rQ);
                const float sPow = std::norm(sRef);
                if (sPow > 1e-6f) {
                    gTrack += kMu * (z - gTrack * sRef) * std::conj(sRef) / sPow;
                }
            }
            bestBits.swap(bits);
        }
    }

    // 前导至少需 3/4 匹配，否则视为未找到信号
    if (bestScore < P * 3 / 4) {
        return std::vector<uint8_t>();
    }
    return bestBits;
}

} // namespace audiot
