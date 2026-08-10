// main.cpp - AudioT entry point (CLI or Qt GUI)
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include "pipeline/tx_pipeline.h"
#include "pipeline/rx_pipeline.h"
#include "pipeline/pipeline_config.h"
#include "audio/wav_reader.h"
#include "core/util/file_utils.h"

#ifdef AUDIOT_HAS_QT
#include "audio/audio_device.h"
#endif

#ifdef AUDIOT_HAS_QT
#include <QApplication>
#include <QFont>
#include <QAudioDeviceInfo>
#include "ui/main_window.h"
#endif

#ifndef AUDIOT_VERSION
#define AUDIOT_VERSION "1.3.0"
#endif

#ifdef _WIN32
// MinGW's CRT hands CLI args as the ANSI code page (GBK on Chinese
// systems), while the rest of the app uses UTF-8 paths. Convert so
// Chinese paths typed on the command line reach openFile() as UTF-8.
static std::string ansiToUtf8(const char* s) {
    if (!s) return std::string();
    const int wlen = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
    if (wlen <= 0) return std::string(s);
    std::vector<wchar_t> wbuf(static_cast<size_t>(wlen));
    MultiByteToWideChar(CP_ACP, 0, s, -1, wbuf.data(), wlen);
    const int ulen = WideCharToMultiByte(CP_UTF8, 0, wbuf.data(), -1, nullptr, 0, nullptr, nullptr);
    if (ulen <= 0) return std::string(s);
    std::vector<char> ubuf(static_cast<size_t>(ulen));
    WideCharToMultiByte(CP_UTF8, 0, wbuf.data(), -1, ubuf.data(), ulen, nullptr, nullptr);
    return std::string(ubuf.data());
}
#endif

static void printUsage() {
    printf("Usage:\n");
    printf("  AudioT encode <input_text> <output.wav>  - Encode text to WAV\n");
    printf("  AudioT decode <input.wav> <output.txt>   - Decode WAV to text\n");
    printf("  AudioT test                              - Run built-in self-test\n");
    printf("  AudioT version                           - Show version info\n");
#ifdef AUDIOT_HAS_QT
    printf("  AudioT (no args)                         - Launch GUI\n");
#endif
}

static void printVersion() {
    printf("AudioT version %s\n", AUDIOT_VERSION);
    printf("  Build: CMake + KissFFT + MinGW\n");
#ifdef AUDIOT_HAS_QT
    printf("  UI: Qt 5\n");
#endif
#ifdef AUDIOT_64BIT
    printf("  Architecture: 64-bit\n");
#else
    printf("  Architecture: 32-bit\n");
#endif
}

static int cmdEncode(const std::string& text, const std::string& wavPath) {
    audiot::PipelineConfig cfg;
    audiot::TxPipeline tx(cfg);

    printf("Encoding text: \"%s\"\n", text.c_str());
    printf("Output WAV: %s\n", wavPath.c_str());

    if (!tx.encodeToFile(text, wavPath)) {
        fprintf(stderr, "Error: Failed to write WAV file\n");
        return 1;
    }

    printf("Encoding complete.\n");
    return 0;
}

static int cmdDecode(const std::string& wavPath, const std::string& outPath) {
    audiot::PipelineConfig cfg;
    audiot::RxPipeline rx(cfg);

    printf("Input WAV: %s\n", wavPath.c_str());

    audiot::DecodeResult result = rx.decodeFromFile(wavPath);
    if (!result.valid()) {
        fprintf(stderr, "Error: Failed to decode or empty result\n");
        return 1;
    }

    if (result.type == audiot::DecodeResult::TEXT) {
        printf("Decoded text: \"%s\"\n", result.text.c_str());

        // Write to output file (Unicode-safe: Chinese/non-ASCII paths)
        FILE* f = audiot::openFile(outPath, "w");
        if (!f) {
            fprintf(stderr, "Error: Cannot open output file: %s\n", outPath.c_str());
            return 1;
        }
        fprintf(f, "%s", result.text.c_str());
        fclose(f);
    } else {
        printf("Decoded file: \"%s\" (%zu bytes)\n",
               result.filename.c_str(), result.fileData.size());

        // Write raw data to output file (Unicode-safe)
        FILE* f = audiot::openFile(outPath, "wb");
        if (!f) {
            fprintf(stderr, "Error: Cannot open output file: %s\n", outPath.c_str());
            return 1;
        }
        fwrite(result.fileData.data(), 1, result.fileData.size(), f);
        fclose(f);
    }

    printf("Output written to: %s\n", outPath.c_str());
    return 0;
}

static int cmdTest() {
    printf("=== AudioT Built-in Self-Test ===\n\n");

    const char* testText = "Hello, AudioT!";
    const char* tempWav = "audiot_selftest.wav";

    printf("Test text: \"%s\"\n", testText);

    // Encode
    audiot::PipelineConfig cfg;
    audiot::TxPipeline tx(cfg);

    std::vector<float> samples = tx.encode(testText);
    printf("Encoded to %zu audio samples\n", samples.size());

    // Write to WAV
    if (!tx.encodeToFile(testText, tempWav)) {
        fprintf(stderr, "FAIL: Cannot write test WAV file\n");
        return 1;
    }
    printf("Written to: %s\n", tempWav);

    // Read back and decode
    audiot::RxPipeline rx(cfg);
    audiot::DecodeResult decoded = rx.decodeFromFile(tempWav);

    printf("Decoded text: \"%s\"\n", decoded.text.c_str());

    if (decoded.valid() && decoded.type == audiot::DecodeResult::TEXT &&
        decoded.text == testText) {
        printf("\n*** SELF-TEST PASSED ***\n");
        return 0;
    } else {
        printf("\n*** SELF-TEST FAILED ***\n");
        printf("Expected: \"%s\"\n", testText);
        printf("Got:      \"%s\"\n", decoded.text.c_str());
        return 1;
    }
}

int main(int argc, char* argv[]) {
#ifdef AUDIOT_HAS_QT
    // High-DPI setup must happen before any QApplication is constructed.
    // PassThrough keeps fractional scales (125%/150%) exact instead of
    // rounding to 100%/200%, so text stays crisp on scaled displays.
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling, true);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps, true);
#endif
#ifdef AUDIOT_HAS_QT
    // If no arguments, launch GUI
    if (argc < 2) {
        QApplication app(argc, argv);
        app.setApplicationName(QStringLiteral("AudioT"));
        app.setApplicationVersion(QStringLiteral(AUDIOT_VERSION));

        // 全局字体改为微软雅黑（Windows 中文默认字体，渲染更清晰），
        // 保持默认字重（Normal），不加粗
        QFont uiFont = app.font();
        uiFont.setFamily(QStringLiteral("Microsoft YaHei"));
        app.setFont(uiFont);

        audiot::MainWindow window;
        window.show();

        int rc = app.exec();
#ifdef _WIN32
        // Skip CRT teardown: unloading the Qt Multimedia audio plugin during
        // normal process exit can corrupt the heap on some Windows systems.
        TerminateProcess(GetCurrentProcess(), static_cast<UINT>(rc));
#endif
        return rc;
    }

    if (argc >= 2 && std::string(argv[1]) == "devices") {
        // List audio devices. QApplication initializes COM, which the
        // Windows audio plugin needs for device enumeration. All device
        // handles are kept alive inside the manager (destroying the last
        // reference to a QAudioDeviceInfo can crash the audio plugin).
        QApplication app(argc, argv);
        audiot::AudioDeviceManager mgr;
        mgr.refreshDevices();
        const QList<audiot::AudioDeviceInfo> inputs = mgr.inputDevices();
        const QList<audiot::AudioDeviceInfo> outputs = mgr.outputDevices();
        printf("Input devices (%d):\n", inputs.size());
        for (const audiot::AudioDeviceInfo& d : inputs) {
            printf("  - %s\n", d.deviceName.toUtf8().constData());
        }
        printf("Output devices (%d):\n", outputs.size());
        for (const audiot::AudioDeviceInfo& d : outputs) {
            printf("  - %s\n", d.deviceName.toUtf8().constData());
        }
        fflush(stdout);
#ifdef _WIN32
        // Skip normal process exit cleanup: unloading the Qt Multimedia
        // audio plugin during CRT teardown corrupts the heap on some
        // Windows systems. TerminateProcess-based exit avoids this.
        TerminateProcess(GetCurrentProcess(), 0);
#endif
        return 0;
    }

    if (argc >= 2 && std::string(argv[1]) == "devtest") {
        // Self-test of the settings-dialog device path: enumerate once,
        // convert, look up by name, and destroy all local copies. Before
        // the keep-alive fix this crashed with an access violation.
        QApplication app(argc, argv);
        audiot::AudioDeviceManager mgr;
        mgr.refreshDevices();
        const QList<audiot::AudioDeviceInfo> ins = mgr.inputDevices();
        const QList<audiot::AudioDeviceInfo> outs = mgr.outputDevices();
        printf("inputs: %d, outputs: %d\n", ins.size(), outs.size());
        for (const audiot::AudioDeviceInfo& d : ins) {
            printf("  in: %s (rate=%d ch=%d)\n",
                   d.deviceName.toUtf8().constData(), d.defaultSampleRate, d.maxChannels);
        }
        for (const audiot::AudioDeviceInfo& d : outs) {
            printf("  out: %s (rate=%d ch=%d)\n",
                   d.deviceName.toUtf8().constData(), d.defaultSampleRate, d.maxChannels);
        }
        // Name lookup + deliberate destruction of copies (crashed before the fix)
        {
            QAudioDeviceInfo inSel = mgr.qtInputDeviceByName(
                ins.isEmpty() ? QString() : ins.first().deviceName);
            QAudioDeviceInfo outSel = mgr.qtOutputDeviceByName(
                outs.isEmpty() ? QString() : outs.first().deviceName);
            printf("selected in: %s\n", inSel.deviceName().toUtf8().constData());
            printf("selected out: %s\n", outSel.deviceName().toUtf8().constData());
        }
        printf("devtest OK (in=%d out=%d)\n",
               mgr.inputDevices().size(), mgr.outputDevices().size());
        fflush(stdout);
#ifdef _WIN32
        TerminateProcess(GetCurrentProcess(), 0);
#endif
        return 0;
    }
#else
    if (argc < 2) {
        printUsage();
        return 0;
    }
#endif

#ifdef _WIN32
    // CLI mode: attach to parent console if available (skip AllocConsole and
    // freopen: they interfere with Qt Multimedia's Windows audio plugin).
    AttachConsole(ATTACH_PARENT_PROCESS);
#endif

    std::string cmd = argv[1];

    if (cmd == "version" || cmd == "--version" || cmd == "-v") {
        printVersion();
        return 0;
    }

    if (cmd == "encode") {
        if (argc < 4) {
            fprintf(stderr, "Error: encode requires <input_text> and <output.wav>\n");
            printUsage();
            return 1;
        }
#ifdef _WIN32
        // CRT argv is ANSI (GBK on Chinese systems); convert to UTF-8 so
        // Chinese output paths work with openFile().
        return cmdEncode(ansiToUtf8(argv[2]), ansiToUtf8(argv[3]));
#else
        return cmdEncode(argv[2], argv[3]);
#endif
    }

    if (cmd == "decode") {
        if (argc < 4) {
            fprintf(stderr, "Error: decode requires <input.wav> and <output.txt>\n");
            printUsage();
            return 1;
        }
#ifdef _WIN32
        return cmdDecode(ansiToUtf8(argv[2]), ansiToUtf8(argv[3]));
#else
        return cmdDecode(argv[2], argv[3]);
#endif
    }

    if (cmd == "test") {
        return cmdTest();
    }

    fprintf(stderr, "Unknown command: %s\n", cmd.c_str());
    printUsage();
    return 1;
}
