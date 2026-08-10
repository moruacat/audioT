

# AudioT

AudioT 是一个开源的软件定义无线电（SDR）应用程序，通过音频信号进行数据传输和接收。它支持多种调制方式、纠错编码，并提供完整的 SSTV（慢扫描电视）图像传输功能。

## 功能特性

### 数据传输
- **多种调制解调器**：AFSK（默认）、PSK、QAM
- **前向纠错（FEC）**：
  - Hamming(7,4) - 默认链路纠错
  - Reed-Solomon - 块级纠错
  - LDPC - 实验性纠错
- **编码支持**：
  - 文本编码（TextCodec）
  - 文件编码（FileCodec）
  - 图像编码（ImageCodec）
  - 视频编码（VideoCodec）

### SSTV 图像传输
支持多种 SSTV 模式，包括：
- Robot 36/72
- Scottie S1/S2/DX
- Martin M1/M2
- PD50/PD120

### 音频处理
- 多种音频后端支持（Qt Multimedia、ALSA）
- WAV 文件读写
- 噪声过滤与降噪

## 项目架构

```
AudioT/
├── src/core/           # 核心库（静态库 audioget_core）
│   ├── codec/          # 各种编解码器
│   ├── dsp/            # 数字信号处理（FFT、滤波器、相关器）
│   ├── fec/            # 前向纠错编码
│   ├── modem/          # 调制解调器实现
│   ├── noise/          # 噪声处理
│   ├── pipeline/       # TX/RX 管道
│   └── sstv/           # SSTV 编解码
├── src/audio/          # 音频层（静态库 audioget_audio）
├── src/ui/             # UI 层（静态库 audioget_ui）
├── src/app/            # 命令行应用
├── tests/              # 单元测试
└── docs/               # 文档
```

## 构建指南

### Windows（MinGW）
```bash
mkdir build && cd build
cmake .. -G "MinGW Makefiles"
cmake --build . --config Release
```

### Linux
```bash
mkdir build && cd build
cmake ..
cmake --build .
```

## 使用方法

### 命令行工具

```bash
# 编码文本到 WAV 文件
audiot --encode "Hello, World!" -o message.wav

# 解码 WAV 文件
audiot --decode input.wav -o output.txt

# 运行测试
audiot --test
```

### GUI 应用
启动后可以看到主界面，包含：
- **TX Panel**：发送数据面板
- **RX Panel**：接收数据面板
- **SSTV Viewer**：图像预览
- **Audio Monitor**：音频监视器

## 测试

项目包含完整的单元测试：
- `test_afsk.cpp` - AFSK 调制解调器测试
- `test_fec.cpp` - 纠错编码测试
- `test_pipeline.cpp` - 管道测试
- `test_sstv.cpp` - SSTV 测试
- 更多测试用例

运行测试：
```bash
./build/tests/audiot_test
```

## 依赖项

- CMake 3.16+
- Qt 5.15+（GUI 必需）
- C++17 兼容编译器
- FFTW3（音频处理）

## 协议规范

详细协议规范请参阅 [docs/protocol_spec.md](docs/protocol_spec.md)。

## 贡献

欢迎提交 Issue 和 Pull Request。

## 许可证

本项目遵循开源许可证协议。