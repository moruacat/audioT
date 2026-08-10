# AudioT

AudioT is an audio-based data communication application: it encodes **text / files** into audio signals that can be carried through **tape recorders, broadcast radios**, and similar channels (a floor-noise recording can be used to auto-tune noise suppression for microphone hum and mains interference), and it provides full **SSTV (Slow Scan Television)** image transmit/receive. Both real-time and WAV-file modes are supported.

## Features

### Data Transmission
- **Modulation** (two options, switchable in Settings):
  - **AFSK** (1200/2200 Hz, Bell 202 style) — default; mature and stable
  - **MFSK (4-FSK)** (FFT-based) — each symbol picks one of 4 orthogonal
    frequency slots, carrying 2 bits (default 50 symbols/s, base 1500 Hz /
    50 Hz slot spacing); demodulation uses an FFT peak decision, giving
    strong resistance to narrowband single-tone interference and white noise
- **Forward Error Correction (FEC)** — three options:
  - None
  - Hamming(7,4) — default; corrects single-symbol errors, suited to real acoustic links
  - Reed-Solomon — block-level correction; strongest in noisy channels
- **Coding support**:
  - Text (TextCodec)
  - File (FileCodec)

> Note: the PSK / QAM modem implementations were moved out of the main source tree into [beta/](beta/) (experimental, untested); the LDPC FEC implementation is likewise not wired into the TX/RX pipeline, so none of them are listed as supported features.

### SSTV Image Transmission
Supported SSTV modes:
- Robot 36 / 72
- Scottie S1 / S2 / DX
- Martin M1 / M2
- PD50 / PD120

### Audio Processing
- Multiple audio backends (Qt Multimedia for Windows/macOS, ALSA for Linux)
- WAV file read/write (encoded output / decoded input)
- Floor-noise recording import and noise reduction (hum, mains, comb-notch filtering)

## Project Architecture

```
AudioT/
├── src/core/           # Core library (static lib audiot_core)
│   ├── codec/          # Various codecs
│   ├── dsp/            # Digital signal processing (FFT, filters, correlators)
│   ├── fec/            # Forward error correction
│   ├── modem/          # Modem implementations
│   ├── noise/          # Noise processing
│   ├── pipeline/       # TX/RX pipelines
│   └── sstv/           # SSTV codec
├── src/audio/          # Audio layer (static lib audiot_audio)
├── src/ui/             # UI layer (static lib audiot_ui, Qt5)
├── src/app/            # Application entry (GUI + CLI)
├── tests/              # Unit tests
└── docs/               # Documentation
```

## Build Guide

### Windows (MinGW + Qt)
```bash
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
```

### Windows (MSVC / Visual Studio)
```bash
cmake -S . -B build -G "Visual Studio 18 2026"  # 32-bit: add -A win32
cmake --build build
```

### Linux
```bash
cmake -S . -B build
cmake --build build
```

> Requires Qt 5.15 (when found, the GUI and Qt auto-tools are enabled; otherwise only the core library and CLI are built). The executable is produced at `build/src/app/`.

## Usage

### Command Line Tool
```bash
# Encode text to a WAV file
AudioT encode "Hello, World!" message.wav

# Decode a WAV file to text
AudioT decode input.wav output.txt

# Device self-test / version info
AudioT devtest
AudioT version
```

### GUI Application
The main window includes:
- **TX Panel**: transmitter (text / file, optional FEC, live play or export WAV)
- **RX Panel**: receiver (live microphone decoding with noise suppression)
- **SSTV Viewer**: image send/receive preview
- **Audio Monitor**: audio monitor

## Testing

The project includes comprehensive unit tests (Catch2, in `tests/`):
- `test_afsk.cpp` - AFSK modem
- `test_fec.cpp` - error correction coding
- `test_pipeline.cpp` - TX/RX pipeline
- `test_sstv.cpp` - SSTV encode/decode
- plus fft / filter / wav / tone / file_codec / reed_solomon, etc.

Run tests:
```bash
ctest --test-dir build
# or individually
./build/tests/test_afsk.exe
```

## Dependencies

- CMake 3.16+
- Qt 5.15 (required for GUI)
- A C++11 compatible compiler
- KissFFT (fetched automatically via FetchContent)

## Protocol Specifications

For detailed protocol specifications, please refer to [docs/protocol_spec.md](docs/protocol_spec.md).

## Contributing

Issues and Pull Requests are welcome.

## License

This project is licensed under the [BSD-3-Clause](LICENSE) license.
