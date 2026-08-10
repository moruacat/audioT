# AudioT

AudioT is an open-source Software-Defined Radio (SDR) application that transmits and receives data via audio signals. It supports multiple modulation schemes, error correction coding, and provides full SSTV (Slow Scan Television) image transmission functionality.

## Features

### Data Transmission
- **Multiple Modems**: AFSK (default), PSK, QAM
- **Forward Error Correction (FEC)**:
  - Hamming(7,4) - Default link error correction
  - Reed-Solomon - Block-level error correction
  - LDPC - Experimental error correction
- **Encoding Support**:
  - Text encoding (TextCodec)
  - File encoding (FileCodec)
  - Image encoding (ImageCodec)
  - Video encoding (VideoCodec)

### SSTV Image Transmission
Supports multiple SSTV modes, including:
- Robot 36/72
- Scottie S1/S2/DX
- Martin M1/M2
- PD50/PD120

### Audio Processing
- Multiple audio backend support (Qt Multimedia, ALSA)
- WAV file read/write
- Noise filtering and reduction

## Project Architecture

```
AudioT/
├── src/core/           # Core library (static library audioget_core)
│   ├── codec/          # Various codecs
│   ├── dsp/            # Digital signal processing (FFT, filters, correlators)
│   ├── fec/            # Forward error correction coding
│   ├── modem/          # Modem implementation
│   ├── noise/          # Noise processing
│   ├── pipeline/       # TX/RX Pipelines
│   └── sstv/           # SSTV Codec
├── src/audio/          # Audio layer (static library audioget_audio)
├── src/ui/             # UI layer (static library audioget_ui)
├── src/app/            # Command-line application
├── tests/              # Unit tests
└── docs/               # Documentation
```

## Build Guide

### Windows (MinGW)
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

## Usage

### Command Line Tool

```bash
# Encode text to WAV file
audiot --encode "Hello, World!" -o message.wav

# Decode WAV file
audiot --decode input.wav -o output.txt

# Run tests
audiot --test
```

### GUI Application
After launching, you can see the main interface, which includes:
- **TX Panel**: Transmitter panel
- **RX Panel**: Receiver panel
- **SSTV Viewer**: Image preview
- **Audio Monitor**: Audio monitor

## Testing

The project includes comprehensive unit tests:
- `test_afsk.cpp` - AFSK Modem tests
- `test_fec.cpp` - Error correction coding tests
- `test_pipeline.cpp` - Pipeline tests
- `test_sstv.cpp` - SSTV tests
- More test cases

Run tests:
```bash
./build/tests/audiot_test
```

## Dependencies

- CMake 3.16+
- Qt 5.15+ (Required for GUI)
- C++17 compatible compiler
- FFTW3 (Audio processing)

## Protocol Specifications

For detailed protocol specifications, please refer to [docs/protocol_spec.md](docs/protocol_spec.md).

## Contributing

Issues and Pull Requests are welcome.

## License

This project follows an open-source license agreement.