# AudioT

AudioT is an audio-based data communication application: it encodes **text / files** into audio signals that can be carried through **tape recorders, broadcast radios**, and similar channels (a floor-noise recording can be used to auto-tune noise suppression for microphone hum and mains interference). Both real-time and WAV-file modes are supported.

## Features

### Data Transmission
- **Modulation** (two options, switchable in Settings):
  - **AFSK** (1200/2200 Hz, Bell 202 style) — default; mature and stable
  - **MFSK (4-FSK)** (FFT-based) — each symbol picks one of 4 orthogonal
    frequency slots, carrying 2 bits; demodulation uses an FFT peak
    decision, giving strong resistance to narrowband single-tone
    interference and white noise
- **MFSK tiers** (speed/robustness trade-off, chosen in Settings):
  - **Robust** — 50 symbols/s ≈ 100 bps, most resilient
  - **Balanced** (default) — 100 symbols/s ≈ 200 bps
  - **Sport** — 150 symbols/s ≈ 300 bps, highest throughput but more
    sensitive; use only on good-quality links
- **Auto-detect** (optional switch, on by default): the receiver tries
  both AFSK and M-FSK demodulation and accepts whichever yields a valid
  frame — no need to coordinate the modulation between the two stations
- **Forward Error Correction (FEC)** — three options:
  - None
  - Hamming(7,4) — default; corrects single-symbol errors, suited to real acoustic links
  - Reed-Solomon — block-level correction; strongest in noisy channels
- **Coding support**:
  - Text (TextCodec)
  - File (FileCodec)
  - **Image / video digital transfer**: packaged with a type marker (`'I'` image / `'V'` video) over the same FEC + modulation link, losslessly reassembled at the receiver

> Note: the LDPC FEC implementation is not wired into the TX/RX pipeline, so it is not listed as a supported feature. The old PSK/QAM coherent implementations (failed even in loopback) have been replaced by `src/core/modem/dpsk_modem` and `src/core/modem/qam16_modem`; the former `beta/` archive has been removed.

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
│   └── pipeline/       # TX/RX pipelines
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
- **Radio Panel**: radio-mode transceiver interface
- **Audio Monitor**: audio monitor

## Testing

The project includes comprehensive unit tests (Catch2, in `tests/`):
- `test_afsk.cpp` - AFSK modem
- `test_fec.cpp` - error correction coding
- `test_mftsk.cpp` - MFSK modem
- `test_pipeline.cpp` - TX/RX pipeline
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
