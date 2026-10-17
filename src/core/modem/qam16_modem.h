// qam16_modem.h - 16-QAM modem (preamble-assisted coherent demodulation)
#ifndef AUDIOT_QAM16_MODEM_H
#define AUDIOT_QAM16_MODEM_H

#include <vector>
#include <cstdint>
#include "interfaces/IModem.h"

namespace audiot {

// 16-QAM 调制解调器。
//
// 发送端：每个符号携带 4 bit（I/Q 各 2 bit，Gray 映射），矩形脉冲，载波相位
// 连续。前导符号轮转遍历全部 16 个星座点。
//
// 接收端：QAM 必须相干解调，但**不需要 Costas 环**——前导符号已知，兼作
// 导频序列直接估计复数信道增益 g（幅度 + 相位）；数据段再用判决导向（DD）
// 以 LMS 方式微调 g。这规避了旧 beta/qam_modem "假设载波已同步"（自由振荡
// 固定相位）导致星座塌缩的问题。
//
// 符号级定时同样用整数样本相位扫描 + 能量门限定位信号起点解决。
class QAM16Modem : public IModem {
public:
    struct Config {
        int sampleRate;      // 采样率
        float carrierFreq;   // 载波频率，取符号率的整数倍以保证符号内整数周期
        int symbolRate;      // 符号率，sampleRate/symbolRate 必须能整除
        int preambleSymbols; // 前导符号数（同步 + 信道估计用）
        float amplitude;     // 输出幅度（16-QAM 峰值因数约 1.34，故小于 AFSK）

        Config()
            : sampleRate(44100)
            , carrierFreq(2100.0f)  // = 2 x symbolRate
            , symbolRate(1050)      // 44100 / 1050 = 42 样本/符号
            , preambleSymbols(32)
            , amplitude(0.37f)
        {}
    };

    explicit QAM16Modem(const Config& config = Config());
    ~QAM16Modem();

    std::vector<float> modulate(const std::vector<uint8_t>& bits) override;
    std::vector<uint8_t> demodulate(const std::vector<float>& samples) override;
    int sampleRate() const override;
    ModemType type() const override;
    int bitsPerSecond() const override;

    // 每符号比特数（固定 4）
    int bitsPerSymbol() const;
    // 每符号样本数
    int samplesPerSymbol() const;

private:
    Config config_;

    // 前导符号序列（轮转覆盖全部星座点）
    std::vector<uint8_t> generatePreamble() const;
};

} // namespace audiot
#endif // AUDIOT_QAM16_MODEM_H
