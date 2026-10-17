// dpsk_modem.cpp - Differential PSK modem implementation
#include "dpsk_modem.h"

#include <algorithm>
#include <cmath>
#include <limits>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace audiot {

DPSKModem::DPSKModem(const Config& config)
    : config_(config)
{
}

DPSKModem::~DPSKModem() {}

int DPSKModem::sampleRate() const {
    return config_.sampleRate;
}

ModemType DPSKModem::type() const {
    return ModemType::PSK;
}

int DPSKModem::bitsPerSymbol() const {
    return config_.order;
}

int DPSKModem::samplesPerSymbol() const {
    if (config_.symbolRate <= 0) return 0;
    return config_.sampleRate / config_.symbolRate;
}

int DPSKModem::slotCount() const {
    return 1 << config_.order;
}

int DPSKModem::bitsPerSecond() const {
    return config_.symbolRate * bitsPerSymbol();
}

std::vector<uint8_t> DPSKModem::grayTable() const {
    // 二进制转 Gray：g = v ^ (v >> 1)。该变换自逆，编码与解码共用一张表。
    const int M = slotCount();
    std::vector<uint8_t> t(static_cast<size_t>(M));
    for (int v = 0; v < M; ++v) {
        t[static_cast<size_t>(v)] = static_cast<uint8_t>(v ^ (v >> 1));
    }
    return t;
}

std::vector<uint8_t> DPSKModem::generatePreamble() const {
    // 已知前导符号序列：轮转星座点。差分之后仍是一个可预测的轮转序列，
    // 接收端据此定位数据起点。
    const int M = slotCount();
    std::vector<uint8_t> p(static_cast<size_t>(config_.preambleSymbols));
    for (int i = 0; i < config_.preambleSymbols; ++i) {
        p[static_cast<size_t>(i)] = static_cast<uint8_t>(i % M);
    }
    return p;
}

std::vector<float> DPSKModem::modulate(const std::vector<uint8_t>& bits) {
    const int sps = samplesPerSymbol();
    const int M = slotCount();
    const int bps = bitsPerSymbol();
    if (sps <= 0 || M <= 1 || bps <= 0 || bits.empty()) {
        return std::vector<float>();
    }

    // 1) 组装符号序列：前导 + 数据（每 bps 个比特凑一个符号，MSB first）。
    //    先把比特流补齐到每符号 bps 位的整数倍（末尾补 0），否则最后不足
    //    一个符号的比特会被丢弃，接收端拿到截断流（帧尾 CRC 不全而解码失败）。
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
        symbols.push_back(static_cast<uint8_t>(idx % M));
    }

    const std::vector<uint8_t> gray = grayTable();
    const double twoPi = 2.0 * static_cast<double>(M_PI);
    const double phaseInc = twoPi * static_cast<double>(config_.carrierFreq) /
                            static_cast<double>(config_.sampleRate);

    // 2) 相位连续合成。每个符号起点先叠加差分相位增量 Δφ = 2π·gray[s]/M，
    //    符号内再以载波推进。发送端不做脉冲成型（矩形脉冲），因此没有
    //    升余弦滤波与接收端不匹配带来的符号间干扰；绝对相位无需对齐，
    //    接收端靠差分抵消公共相移。
    std::vector<float> out;
    out.reserve(symbols.size() * static_cast<size_t>(sps));
    double phase = 0.0;
    for (size_t k = 0; k < symbols.size(); ++k) {
        const int g = gray[symbols[k]];
        phase += twoPi * static_cast<double>(g) / static_cast<double>(M);
        if (phase >= twoPi) phase -= twoPi;
        for (int i = 0; i < sps; ++i) {
            out.push_back(static_cast<float>(std::sin(phase)) * config_.amplitude);
            phase += phaseInc;
            if (phase >= twoPi) phase -= twoPi;
        }
    }
    return out;
}

std::vector<uint8_t> DPSKModem::demodulate(const std::vector<float>& samples) {
    const int sps = samplesPerSymbol();
    const int M = slotCount();
    const int bps = bitsPerSymbol();
    if (sps <= 0 || M <= 1 || bps <= 0 || samples.empty()) {
        return std::vector<uint8_t>();
    }

    const int totalSymbols = static_cast<int>(samples.size()) / sps;
    if (totalSymbols < 2) {
        return std::vector<uint8_t>();
    }

    const double twoPi = 2.0 * static_cast<double>(M_PI);
    const double phaseInc = twoPi * static_cast<double>(config_.carrierFreq) /
                            static_cast<double>(config_.sampleRate);
    const std::vector<uint8_t> gray = grayTable();
    // 逆表：相位增量索引 -> 符号索引。注意 v^(v>>1) 只在 M<=4 时自逆，
    // M=8 必须显式求逆，否则 D8PSK 判决出的符号索引错误。
    std::vector<uint8_t> grayInv(gray.size());
    for (size_t s = 0; s < gray.size(); ++s) {
        grayInv[static_cast<size_t>(gray[s])] = static_cast<uint8_t>(s);
    }
    const std::vector<uint8_t> pre = generatePreamble();

    // 匹配长度：第一个发送符号的差分没有前一个符号可参照，因此接收端
    // 只能观测到 symbols[1..]，前导中可用的匹配符号数为 P-1。
    const int matchLen = config_.preambleSymbols - 1;
    if (matchLen < 2) {
        return std::vector<uint8_t>();
    }

    // 符号内本地振荡器查表（i 从 0 起；每符号的绝对相位差在差分中抵消，
    // 所以无需与发送端载波相位对齐）
    std::vector<float> loCos(static_cast<size_t>(sps));
    std::vector<float> loSin(static_cast<size_t>(sps));
    for (int i = 0; i < sps; ++i) {
        loCos[static_cast<size_t>(i)] = static_cast<float>(std::cos(phaseInc * i));
        loSin[static_cast<size_t>(i)] = static_cast<float>(std::sin(phaseInc * i));
    }

    int bestScore = -1;
    std::vector<uint8_t> bestBits;

    // 符号级整数相位扫描：发送/接收之间没有样本级对齐，逐个候选相位解一遍，
    // 取前导匹配最好的那个（与 MFSK 的解调思路一致）。
    for (int ph = 0; ph < sps; ++ph) {
        std::vector<float> zI, zQ;
        zI.reserve(static_cast<size_t>(totalSymbols));
        zQ.reserve(static_cast<size_t>(totalSymbols));
        for (int k = 0; k < totalSymbols; ++k) {
            const int start = ph + k * sps;
            if (start + sps > static_cast<int>(samples.size())) break;
            float accI = 0.0f;
            float accQ = 0.0f;
            for (int i = 0; i < sps; ++i) {
                const float x = samples[static_cast<size_t>(start + i)];
                // 用 e^{-jθ} 混频：x = sin(Ψ) 经此混频后 z ∝ e^{+jφ}，
                // 差分相位即 +Δφ。若用 +j sin 混频会得到 e^{-jφ}，差分相位
                // 取反（DQPSK/D8PSK 会整体翻转而解错）。
                accI += x * loCos[static_cast<size_t>(i)];
                accQ -= x * loSin[static_cast<size_t>(i)];
            }
            zI.push_back(accI);
            zQ.push_back(accQ);
        }
        const int n = static_cast<int>(zI.size());
        if (n - 1 < matchLen) continue;

        // 差分判决：d_k = z_k · conj(z_{k-1})，其相位即差分相位增量 Δφ_k。
        // 观测量 obs[j] 对应发送符号 symbols[j+1]。
        std::vector<uint8_t> obs;
        obs.reserve(static_cast<size_t>(n - 1));
        for (int k = 1; k < n; ++k) {
            const float dRe = zI[static_cast<size_t>(k)] * zI[static_cast<size_t>(k - 1)]
                            + zQ[static_cast<size_t>(k)] * zQ[static_cast<size_t>(k - 1)];
            const float dIm = zQ[static_cast<size_t>(k)] * zI[static_cast<size_t>(k - 1)]
                            - zI[static_cast<size_t>(k)] * zQ[static_cast<size_t>(k - 1)];
            const double ang = std::atan2(static_cast<double>(dIm), static_cast<double>(dRe));
            int g = static_cast<int>(std::lround(ang * static_cast<double>(M) / twoPi));
            g %= M;
            if (g < 0) g += M;
            // Gray 逆表：相位增量索引 -> 符号索引
            obs.push_back(grayInv[static_cast<size_t>(g)]);
        }

        // 前导匹配：期望 obs[off+p] == pre[p+1]（前导符号序列的轮转特性）
        const int obsLen = static_cast<int>(obs.size());
        int score = 0;
        int dataStart = -1;
        for (int off = 0; off + matchLen <= obsLen; ++off) {
            int sc = 0;
            for (int p = 0; p < matchLen; ++p) {
                const uint8_t expected =
                    pre[static_cast<size_t>(p + 1) % pre.size()];
                if (obs[static_cast<size_t>(off + p)] == expected) ++sc;
            }
            if (sc > score) {
                score = sc;
                // obd[j] 对应 symbols[j+1]，前导最后一个是 symbols[P-1]，
                // 故数据符号 symbols[P] 对应 obs 下标 off + P - 1。
                dataStart = off + config_.preambleSymbols - 1;
            }
        }

        if (score > bestScore && dataStart >= 0) {
            bestScore = score;
            bestBits.clear();
            for (int j = dataStart; j < obsLen; ++j) {
                const int sym = obs[static_cast<size_t>(j)];
                for (int b = bps - 1; b >= 0; --b) {
                    bestBits.push_back(static_cast<uint8_t>((sym >> b) & 1));
                }
            }
        }
        // 完美前导匹配，无需再扫其它相位
        if (bestScore >= matchLen) break;
    }

    // 前导至少需 3/4 匹配，否则视为未找到信号
    if (bestScore < matchLen * 3 / 4) {
        return std::vector<uint8_t>();
    }
    return bestBits;
}

} // namespace audiot
