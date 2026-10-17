// dpsk_modem.h - Differential PSK modem (DBPSK / DQPSK / D8PSK)
#ifndef AUDIOT_DPSK_MODEM_H
#define AUDIOT_DPSK_MODEM_H

#include <vector>
#include <cstdint>
#include "interfaces/IModem.h"

namespace audiot {

// 差分相移键控（DPSK）调制解调器。
//
// 发送端：对每个符号做差分编码（相位增量 Δφ），符号内以恒定载波合成，
// 相位在符号之间连续（矩形脉冲，无脉冲成型），因此不存在升余弦滤波器
// 与接收端不匹配造成的符号间干扰。
//
// 接收端：对每个符号用本地载波做正交积分得到复数观测 z_k，再做相邻符号
// 共轭相乘 d_k = z_k · conj(z_{k-1})，其相位即为差分相位增量。本地载波的
// 绝对相位（以及信道引入的公共相移）在差分中自动抵消，所以**不需要载波
// 相位恢复**——这正是旧 beta/psk_modem 用 Costas 环失败的地方。
//
// 符号级定时用一个整数样本相位扫描解决（与 MFSK 相同思路）：发送/接收
// 之间没有样本级对齐，接收端对 0..sps-1 每个候选相位都解一遍，取前导匹配
// 最好的那个。
class DPSKModem : public IModem {
public:
    struct Config {
        int sampleRate;      // 采样率
        float carrierFreq;   // 载波频率，取符号率的整数倍以保证符号内整数周期
        int symbolRate;      // 符号率，sampleRate/symbolRate 必须能整除
        int order;           // 每符号比特数：1=DBPSK, 2=DQPSK, 3=D8PSK
        int preambleSymbols; // 前导符号数（同步用）
        float amplitude;     // 输出幅度

        Config()
            : sampleRate(44100)
            , carrierFreq(2100.0f)  // = 2 x symbolRate，符号内整 2 个载波周期
            , symbolRate(1050)      // 44100 / 1050 = 42 样本/符号
            , order(2)              // 默认 DQPSK
            , preambleSymbols(32)
            , amplitude(0.5f)
        {}
    };

    explicit DPSKModem(const Config& config = Config());
    ~DPSKModem();

    std::vector<float> modulate(const std::vector<uint8_t>& bits) override;
    std::vector<uint8_t> demodulate(const std::vector<float>& samples) override;
    int sampleRate() const override;
    ModemType type() const override;
    int bitsPerSecond() const override;

    // 比特数/符号
    int bitsPerSymbol() const;
    // 每符号样本数
    int samplesPerSymbol() const;
    // 星座点数 2^order
    int slotCount() const;

private:
    Config config_;

    // 符号索引 <-> 相位增量索引的 Gray 表（自逆：编码解码同一张表）。
    // 相邻相位增量只差一个比特，误判只影响 1 bit。
    std::vector<uint8_t> grayTable() const;

    // 前导符号序列（已知，供接收端定位数据起点）
    std::vector<uint8_t> generatePreamble() const;
};

} // namespace audiot
#endif // AUDIOT_DPSK_MODEM_H
