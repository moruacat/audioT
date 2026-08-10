# beta/ - 实验性 / 未接入代码

本目录存放**未接入（未启用）的实验性调制解调器实现**。

这些调制器已实现 `IModem` 接口（`modulate` / `demodulate`），但经验证：

- PSK（BPSK / QPSK / 8PSK）：自环调制→解调**全部失败**（Costas 环未锁定载波，解出 0 bit）
- QAM（16 / 32 / 64-QAM)：自环调制→解调**失败**（载波相位基准错误，星座点塌缩，仅约 50% 正确）

即它们处于**半成品/实验状态**，因此从主源码（`src/core/modem/`）中移出，
以避免被当作可用功能展示在 UI 中。主链路当前仅支持 **AFSK** 调制。

## 文件清单

| 文件 | 说明 |
|---|---|
| `psk_modem.cpp` / `psk_modem.h` | PSK 调制器（BPSK/QPSK/8PSK，Costas 环） |
| `qam_modem.cpp` / `qam_modem.h` | QAM 调制器（16/32/64-QAM，相干解调） |
| `modem_factory.cpp` / `modem_factory.h` | 调制器工厂（原为创建 AFSK/PSK/QAM 实例，无人调用） |

> 这些是从 `src/core/modem/` 移除时的原样归档副本，仅供后续参考或修复。
> 若未来要重新接入，需先修复其载波同步/定时恢复等 DSP 算法，并通过自环测试。

## 相关说明

- `src/core/interfaces/IModem.h` 中的 `ModemType` 枚举现仅含 `AFSK`
- UI（TX Panel）的调制方式下拉现仅提供 AFSK
- 文档（README.md）不再把 PSK/QAM 列为支持功能
