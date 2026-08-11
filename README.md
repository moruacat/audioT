# AudioT

AudioT 是一个基于音频信号的数据收发软件：它将**文本 / 文件**编码为音频，可通过**磁带机、广播电台**等途径进行数据传输（传输前可用底噪录音自动调参以抑制麦克风电流声与交流电干扰）。实时模式与 WAV 文件模式均可。

## 功能特性

### 数据传输
- **调制方式**（两种可选，在"设置"中切换）：
  - **AFSK**（1200/2200 Hz，Bell 202 风格）—— 默认，成熟稳定
  - **MFSK（4-FSK）**（基于 FFT）—— 每个符号在 4 个正交频点中选一，携带 2 bit；解调采用 FFT 峰值判决，对窄带单频干扰与白噪声有较强的抵抗力
- **MFSK 档位**（速度/抗噪折中，在"设置"中选择）：
  - **稳健** —— 50 符号/秒 ≈ 100 bps，最抗噪
  - **均衡**（默认）—— 100 符号/秒 ≈ 200 bps
  - **高速** —— 150 符号/秒 ≈ 300 bps，吞吐最高但更敏感，仅在信道质量良好时使用
- **自动检测**（可选开关，默认开启）：接收端自动尝试 AFSK 与 M-FSK 解调，任一方能解出合法帧即采用，无需求助双方协商调制方式
- **前向纠错（FEC）**—— 三档可选：
  - None —— 无纠错
  - Hamming(7,4) —— 默认，纠正单符号错误，适合真实声学链路
  - Reed-Solomon —— 块级纠错，抗噪能力最强，适合噪声较大的信道
- **编码支持**：
  - 文本编码（TextCodec）
  - 文件编码（FileCodec）

> 说明：PSK、QAM 调制器实现已从主源码移除，归档至 [beta/](beta/)（实验性、未经实测）；LDPC 纠错实现同样未接入流水线，故均不列为已支持功能。

### 音频处理
- 多种音频后端支持（Qt Multimedia for Windows/macOS、ALSA for Linux）
- WAV 文件读写（编码输出 / 解码输入）
- 底噪录音导入与降噪（电流声、交流声、梳状陷波滤除）

## 项目架构

```
AudioT/
├── src/core/           # 核心库（静态库 audiot_core）
│   ├── codec/          # 各种编解码器
│   ├── dsp/            # 数字信号处理（FFT、滤波器、相关器）
│   ├── fec/            # 前向纠错编码
│   ├── modem/          # 调制解调器实现
│   ├── noise/          # 噪声处理
│   └── pipeline/       # TX/RX 管道
├── src/audio/          # 音频层（静态库 audiot_audio）
├── src/ui/             # UI 层（静态库 audiot_ui，Qt5）
├── src/app/            # 应用入口（GUI + 命令行）
├── tests/              # 单元测试
└── docs/               # 文档
```

## 构建指南

### Windows（MinGW + Qt）
```bash
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
```

### Windows（MSVC / Visual Studio）
```bash
cmake -S . -B build -G "Visual Studio 18 2026"  # 或 32 位：加 -A win32
cmake --build build
```

### Linux
```bash
cmake -S . -B build
cmake --build build
```

> 依赖 Qt 5.15（找到 Qt 后自动启用 GUI 与自动工具；未找到则仅构建核心库与命令行）。构建出的可执行文件位于 `build/src/app/`。

## 使用方法

### 命令行工具
```bash
# 编码文本到 WAV 文件
AudioT encode "Hello, World!" message.wav

# 解码 WAV 文件为文本
AudioT decode input.wav output.txt

# 设备自检 / 版本信息
AudioT devtest
AudioT version
```

### GUI 应用
启动后主界面包含：
- **TX Panel**：发送数据面板（文本 / 文件，可选 FEC，实时播放或导出 WAV）
- **RX Panel**：接收数据面板（麦克风实时解析，带底噪抑制）
- **Radio Panel**：电台模式收发界面
- **Audio Monitor**：音频监视器

## 测试

项目包含完整的单元测试（Catch2，位于 `tests/`）：
- `test_afsk.cpp` - AFSK 调制解调器
- `test_fec.cpp` - 纠错编码
- `test_mftsk.cpp` - MFSK 调制解调器
- `test_pipeline.cpp` - TX/RX 管道
- 以及 fft / filter / wav / tone / file_codec / reed_solomon 等

运行测试：
```bash
ctest --test-dir build
# 或逐个运行
./build/tests/test_afsk.exe
```

## 依赖项

- CMake 3.16+
- Qt 5.15（GUI 必需）
- C++11 兼容编译器
- KissFFT（随 FetchContent 自动获取）

## 协议规范

详细协议规范请参阅 [docs/protocol_spec.md](docs/protocol_spec.md)。

## 贡献

欢迎提交 Issue 和 Pull Request。

## 许可证

本项目遵循 [BSD-3-Clause](LICENSE) 许可证。
