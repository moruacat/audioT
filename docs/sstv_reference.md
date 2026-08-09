# AudioT SSTV 参考

SSTV（Slow Scan Television，慢扫描电视）是业余无线电中通过音频传输静态图像
的标准。AudioT 实现 9 种常用模式的编码与解码（`src/core/sstv/`）。

## 1. 信号结构

一帧完整的 SSTV 信号按时间顺序由以下部分组成：

```
┌──────────┬───────┬──────────────┬──────────────────────────────┐
│ Leader   │ Break │ VIS 信令     │ 图像扫描（逐行）             │
│ 1900Hz   │1200Hz │ 起始/8bit/停止│ 同步脉冲 + 像素调频扫描       │
│ 300ms    │ 10ms  │ 30+8×30+30ms │ lineTimeMs × height 行       │
└──────────┴───────┴──────────────┴──────────────────────────────┘
```

### 1.1 头部时序（sstv_timing.h）

| 段 | 频率 | 时长 | 说明 |
|----|------|------|------|
| Leader | 1900 Hz | 300 ms | 校准导频；解码端要求 ≥100 ms 才确认 |
| Break | 1200 Hz | 10 ms | Leader 与 VIS 之间的分隔音 |
| VIS 起始位 | 1200 Hz | 300 ms | |
| VIS 数据位 | 1100 Hz = 1，1300 Hz = 0 | 30 ms/bit | 7 位模式码 + 1 位偶校验 |
| VIS 停止位 | 1200 Hz | 30 ms | |

### 1.2 像素扫描（FM 亮度/色度）

| 参数 | 值 |
|------|-----|
| 同步脉冲频率 | 1200 Hz |
| 黑色频率 | 1500 Hz |
| 白色频率 | 2300 Hz |
| 亮度→频率映射 | 线性：f = 1500 + 亮度 × 800 |
| 频率匹配容差 | ±6%（解码检测） |

## 2. 支持的模式列表

参数来自 `sstv_protocols.cpp`（标准 SSTV 值）：

| 模式 | VIS 码 | 分辨率 | 行时长 (ms) | 同步时长 (ms) | 色彩 | 色分量 |
|------|--------|--------|------------|--------------|------|--------|
| Robot 36 | 8 | 320×240 | 88.000 | 9.000 | YCrCb | 3 |
| Robot 72 | 12 | 320×240 | 138.000 | 9.000 | YCrCb | 3 |
| Scottie S1 | 60 | 320×256 | 138.240 | 9.000 | RGB | 3 |
| Scottie S2 | 56 | 320×256 | 88.064 | 9.000 | RGB | 3 |
| Scottie DX | 76 | 320×256 | 263.816 | 9.000 | RGB | 3 |
| Martin M1 | 44 | 320×256 | 146.432 | 4.862 | RGB | 3 |
| Martin M2 | 40 | 320×256 | 73.216 | 4.862 | RGB | 3 |
| PD50 | 82 | 640×496 | 63.500 | 20.800 | YCrCb | 3 |
| PD120 | 97 | 640×496 | 121.600 | 20.800 | YCrCb | 3 |

所有模式共享相同的频率约定（同步 1200 Hz、黑 1500 Hz、白 2300 Hz），
差异在于分辨率、每行时序与色彩编码（RGB 顺序扫描或 YCrCb 分量扫描）。

### 模式时长估算

总时长 ≈ 300(leader) + 10(break) + 300(VIS 起始) + 8×30(VIS 位) + 30(停止)
+ lineTimeMs × height

| 模式 | 约耗时 |
|------|--------|
| Robot 36 | ≈ 21.8 s |
| Robot 72 | ≈ 33.9 s |
| Scottie S1 | ≈ 36.0 s |
| Scottie S2 | ≈ 23.2 s |
| Scottie DX | ≈ 68.3 s |
| Martin M1 | ≈ 38.2 s |
| Martin M2 | ≈ 19.4 s |
| PD50 | ≈ 32.2 s |
| PD120 | ≈ 61.0 s |

## 3. VIS 码处理

- 编码：模式码 7 bit + 偶校验 1 bit（`sstv_encoder`）
- 解码：逐位 Goertzel/能量检测 1100/1300 Hz，校验失败时按位表决恢复
- 未知 VIS 码：默认回退为 Martin M1（`modeFromVISCode`）

## 4. 解码流程

1. **Leader 检测**：滑动窗口能量检测 1900 Hz（容差 ±6%），持续 ≥100 ms
2. **Break 检测**：1200 Hz 短音确认头部结束
3. **VIS 解析**：读取 8 个数据位 + 校验，查表得到模式
4. **逐行扫描**：按模式参数定位同步脉冲（1200 Hz），
   将 1500–2300 Hz 扫描段的瞬时频率线性映射回亮度/色度
5. **色彩合成**：RGB 模式直接合成；YCrCb 模式转换回 RGB 输出

## 5. API 速查

```cpp
#include "sstv/sstv_protocols.h"
#include "sstv/sstv_encoder.h"
#include "sstv/sstv_decoder.h"

// 协议信息
const SSTVProtocolInfo& info = getProtocolInfo(SSTVMode::MartinM1);
SSTVMode mode = modeFromVISCode(44);   // -> MartinM1

// 编码：图像 -> 音频采样
// 解码：音频采样 -> 图像
```
