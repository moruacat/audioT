// main_window.cpp - Main application window implementation
#include "main_window.h"
#include "tx_panel.h"
#include "rx_panel.h"
#include "radio_panel.h"
#include "audio_monitor.h"
#include "settings_dialog.h"

#include <QMenuBar>
#include <QStatusBar>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QApplication>
#include <QFile>
#include <QFileDialog>
#include <QDir>
#include <QLineEdit>
#include <QIcon>
#include <QMetaObject>
#include <QCoreApplication>
#include <cmath>

#include "core/pipeline/tx_pipeline.h"
#include "core/pipeline/rx_pipeline.h"
#include "core/sstv/sstv_encoder.h"
#include "core/sstv/sstv_protocols.h"
#include "audio/wav_reader.h"
#include "audio/wav_writer.h"

#include <QDateTime>

namespace audiot {

namespace {

// 判断输入是否为图片路径（SSTV 发送/导出专用入口）
bool isImagePath(const QString& s)
{
    const QString lower = s.toLower();
    return lower.endsWith(QStringLiteral(".png")) ||
           lower.endsWith(QStringLiteral(".jpg")) ||
           lower.endsWith(QStringLiteral(".jpeg")) ||
           lower.endsWith(QStringLiteral(".bmp"));
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , txPanel_(nullptr)
    , rxPanel_(nullptr)
    , audioMonitor_(nullptr)
    , tabWidget_(nullptr)
    , settingsDialog_(nullptr)
    , audioDeviceManager_(nullptr)
    , audioInput_(nullptr)
    , audioOutput_(nullptr)
    , txPlaybackTimer_(nullptr)
    , txPlaybackPos_(0)
    , txDataStart_(0)
    , txDrainTicks_(0)
    , radioListening_(false)
    , radioParseThread_(nullptr)
    , radioAborted_(false)
    , radioParseGen_(0)
    , rxMicListening_(false)
    , rxMicParseThread_(nullptr)
    , rxMicAborted_(false)
    , rxMicParseGen_(0)
    , sstvListening_(false)
    , sstvHeaderFound_(false)
    , sstvNeedSamples_(0)
    , sstvDetectCounter_(0)
    , sstvParseThread_(nullptr)
    , sstvAborted_(false)
    , sstvParseGen_(0)
    , rxParseThread_(nullptr)
    , rxParseAborted_(false)
    , rxParseGen_(0)
    , txExportThread_(nullptr)
    , txExportGen_(0)
{
    setWindowIcon(QIcon(QStringLiteral(":/icons/app-logo.svg")));
    setWindowTitle(QStringLiteral("AudioT - 音频数据通信"));
    setMinimumSize(700, 500);

    // Initialize core objects
    audioDeviceManager_ = new AudioDeviceManager(this);
    audioInput_ = new QtAudioInputStream(this);
    audioOutput_ = new QtAudioOutputStream(this);

    // Pacing timer for real-time TX playback (push writes to QAudioOutput)
    txPlaybackTimer_ = new QTimer(this);
    connect(txPlaybackTimer_, &QTimer::timeout, this, &MainWindow::onTxPlaybackTick);

    setupUI();
    setupMenuBar();
    setupStatusBar();
    connectSignals();

    statusBar()->showMessage(QStringLiteral("就绪"));
}

MainWindow::~MainWindow()
{
    txPlaybackTimer_->stop();
    // 停止电台监听并回收段解码线程
    radioListening_ = false;
    radioAborted_ = true;
    if (radioParseThread_ && radioParseThread_->joinable()) {
        radioParseThread_->join();
        delete radioParseThread_;
    }
    // 停止接收面板麦克风监听并回收段解码线程
    rxMicListening_ = false;
    rxMicAborted_ = true;
    if (rxMicParseThread_ && rxMicParseThread_->joinable()) {
        rxMicParseThread_->join();
        delete rxMicParseThread_;
    }
    // 停止接收面板 SSTV 监听并回收解码线程
    sstvListening_ = false;
    sstvAborted_ = true;
    if (sstvParseThread_ && sstvParseThread_->joinable()) {
        sstvParseThread_->join();
        delete sstvParseThread_;
    }
    // 取消并等待后台 WAV 解析线程结束
    rxParseAborted_ = true;
    if (rxParseThread_ && rxParseThread_->joinable()) {
        rxParseThread_->join();
        delete rxParseThread_;
    }
    // 等待后台导出线程结束
    if (txExportThread_ && txExportThread_->joinable()) {
        txExportThread_->join();
        delete txExportThread_;
    }
    if (audioInput_->isRunning()) {
        audioInput_->stop();
    }
    if (audioOutput_->isRunning()) {
        audioOutput_->stop();
    }
}

void MainWindow::setupUI()
{
    // Central widget with vertical layout
    QWidget* central = new QWidget(this);
    QVBoxLayout* mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(4, 4, 4, 4);

    // Audio monitor at top
    audioMonitor_ = new AudioMonitor(this);
    mainLayout->addWidget(audioMonitor_);

    // Tab widget for TX/RX/Radio
    tabWidget_ = new QTabWidget(this);

    txPanel_ = new TxPanel(this);
    rxPanel_ = new RxPanel(this);
    radioPanel_ = new RadioPanel(this);

    tabWidget_->addTab(txPanel_, QStringLiteral("发射"));
    tabWidget_->addTab(rxPanel_, QStringLiteral("接收"));
    tabWidget_->addTab(radioPanel_, QStringLiteral("电台"));

    mainLayout->addWidget(tabWidget_);

    setCentralWidget(central);

    // Create settings dialog (lazy, parented to this)
    settingsDialog_ = new SettingsDialog(audioDeviceManager_, this);
}

void MainWindow::setupMenuBar()
{
    // File menu
    QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("文件"));
    QAction* actionExit = fileMenu->addAction(QStringLiteral("退出"));
    actionExit->setShortcut(QKeySequence::Quit);
    connect(actionExit, &QAction::triggered, qApp, &QApplication::quit);

    // Settings menu
    QMenu* toolsMenu = menuBar()->addMenu(QStringLiteral("工具"));
    actionSettings_ = toolsMenu->addAction(QStringLiteral("设置..."));
    actionSettings_->setShortcut(QKeySequence::Preferences);
    connect(actionSettings_, &QAction::triggered, this, &MainWindow::onSettings);

    // Help menu
    QMenu* helpMenu = menuBar()->addMenu(QStringLiteral("帮助"));
    actionAbout_ = helpMenu->addAction(QStringLiteral("关于 AudioT"));
    connect(actionAbout_, &QAction::triggered, this, &MainWindow::onAbout);
}

void MainWindow::setupStatusBar()
{
    statusBar()->showMessage(QStringLiteral("就绪"));
}

void MainWindow::connectSignals()
{
    // TX panel signals
    connect(txPanel_, &TxPanel::startTransmission,
            this, &MainWindow::onStartTransmission);
    connect(txPanel_, &TxPanel::stopTransmission,
            this, &MainWindow::onStopTransmission);
    connect(txPanel_, &TxPanel::exportWavRequested,
            this, &MainWindow::onExportWav);

    // RX panel signals
    connect(rxPanel_, &RxPanel::startReceiving,
            this, &MainWindow::onStartReceiving);
    connect(rxPanel_, &RxPanel::stopReceiving,
            this, &MainWindow::onStopReceiving);
    // RX panel SSTV 图片信号
    connect(rxPanel_, &RxPanel::startSstv,
            this, &MainWindow::onStartSstv);
    connect(rxPanel_, &RxPanel::stopSstv,
            this, &MainWindow::onStopSstv);

    // Radio panel signals (双向电台模式)
    connect(radioPanel_, &RadioPanel::startListening,
            this, &MainWindow::onRadioStartListening);
    connect(radioPanel_, &RadioPanel::stopListening,
            this, &MainWindow::onRadioStopListening);
    connect(radioPanel_, &RadioPanel::sendRequested,
            this, &MainWindow::onRadioSend);

    // Audio input callback -> waveform display + radio/rx-mic/sstv segmenting
    audioInput_->setCallback([this](const float* samples, int count) {
        audioMonitor_->updateWaveform(samples, count);
        if (radioListening_) {
            accumulateRadio(samples, count);
        } else if (rxMicListening_) {
            accumulateRxMic(samples, count);
        } else if (sstvListening_) {
            accumulateSstvMic(samples, count);
        }
    });
}

void MainWindow::onSettings()
{
    if (settingsDialog_->exec() == QDialog::Accepted) {
        config_ = settingsDialog_->getPipelineConfig();
        // Apply selected audio devices to the streams
        QAudioDeviceInfo inDev = settingsDialog_->selectedInputDevice();
        QAudioDeviceInfo outDev = settingsDialog_->selectedOutputDevice();
        if (!inDev.isNull()) audioInput_->setDevice(inDev);
        if (!outDev.isNull()) audioOutput_->setDevice(outDev);
        statusBar()->showMessage(QStringLiteral("设置已更新"), 3000);
    }
}

void MainWindow::onAbout()
{
    QMessageBox::about(this, QStringLiteral("关于 AudioT"),
        QStringLiteral("<h3>AudioT v1.0.0</h3>"
                       "<p>音频数据通信软件</p>"
                       "<p>支持 AFSK/PSK/QAM 调制，"
                       "Hamming/Reed-Solomon 纠错编码，"
                       "SSTV 图片收发。</p>"
                       "<p>基于 Qt 5.15 + CMake 构建。</p>"));
}

void MainWindow::onStartTransmission(const QString& textOrFile)
{
    // Abort any in-flight playback before starting a new one
    if (txPlaybackTimer_->isActive() || audioOutput_->isRunning()) {
        finishTxPlayback(true);
    }

    config_ = settingsDialog_->getPipelineConfig();
    statusBar()->showMessage(QStringLiteral("开始发射..."));

    // SSTV 图片：编码为 SSTV 音频（自带头部），直接播放
    if (isImagePath(textOrFile)) {
        std::vector<float> dataSamples;
        QString err;
        if (!encodeSstvImage(textOrFile, config_.sampleRate, dataSamples, err)) {
            txPanel_->onTransmissionStatus(QStringLiteral("失败: ") + err);
            statusBar()->showMessage(QStringLiteral("发射失败"), 5000);
            return;
        }
        // SSTV 信号自带 Leader/VIS 头，无需 AFSK 准备音
        if (!beginTxPlayback(dataSamples, false)) {
            txPanel_->onTransmissionStatus(QStringLiteral("失败: 无法启动音频输出"));
            statusBar()->showMessage(QStringLiteral("发射失败: 音频输出启动失败"), 5000);
        }
        return;
    }

    // Encode text -> modulated audio samples (fast, pure CPU work)
    std::vector<float> dataSamples;
    QString err;
    if (!encodeTxInput(textOrFile, config_, dataSamples, err)) {
        txPanel_->onTransmissionStatus(QStringLiteral("失败: ") + err);
        statusBar()->showMessage(QStringLiteral("发射失败"), 5000);
        return;
    }

    // Real-time playback to the speaker: prep tone first, then data audio
    if (!beginTxPlayback(dataSamples)) {
        txPanel_->onTransmissionStatus(QStringLiteral("失败: 无法启动音频输出"));
        statusBar()->showMessage(QStringLiteral("发射失败: 音频输出启动失败"), 5000);
    }
}

bool MainWindow::encodeTxInput(const QString& textOrFile, const PipelineConfig& config,
                               std::vector<float>& outSamples, QString& errorMsg)
{
    // Determine if input is text or file path
    bool isFile = textOrFile.contains(QStringLiteral(".")) &&
                  (textOrFile.contains(QStringLiteral("/")) || textOrFile.contains(QStringLiteral("\\")));

    QString content = textOrFile;
    if (isFile) {
        // 中文/非 ASCII 路径无法传输：Windows 下 FileCodec 通过
        // std::ifstream 打开路径（要求 ANSI/ASCII），而 Qt 输入框的
        // 中文路径会变成 UTF-8，导致文件打不开或打错。
        for (QChar c : textOrFile) {
            if (c.unicode() > 127) {
                errorMsg = QStringLiteral("无法使用中文路径");
                return false;
            }
        }
        // 文件发送：仅检查可读性，内容由 FileCodec 直接读取打包
        QFile file(textOrFile);
        if (!file.open(QIODevice::ReadOnly)) {
            errorMsg = QStringLiteral("无法打开文件");
            return false;
        }
        file.close();
    }

    // Encode text -> modulated audio samples. The type marker frame
    // ('T' text / 'F' file) is encoded together with the data frame as
    // one continuous burst: it directly follows the prep tone on air,
    // so the receiver can decode the data frame with the right codec.
    audiot::TxPipeline tx(config);
    std::vector<float> dataSamples;
    if (isFile) {
        // 文件发送：以 FileCodec 帧打包（文件名+类型+CRC32）后调制
        dataSamples = tx.encodeTypedFile(textOrFile.toStdString());
    } else {
        dataSamples = tx.encodeTypedText(content.toStdString());
    }
    if (dataSamples.empty()) {
        errorMsg = QStringLiteral("编码错误");
        return false;
    }

    // Append the end-of-frame control marker: a silent interval after
    // the modulated data (AFSK postamble), so the receiving side can
    // unambiguously detect frame end.
    std::vector<float> endSilence = audiot::TxPipeline::makeSilence(
        config.endControlSeconds, config.sampleRate);
    dataSamples.insert(dataSamples.end(), endSilence.begin(), endSilence.end());

    outSamples = std::move(dataSamples);
    return true;
}

bool MainWindow::encodeSstvImage(const QString& imagePath, int sampleRate,
                                 std::vector<float>& outSamples,
                                 QString& errorMsg)
{
    QImage img(imagePath);
    if (img.isNull()) {
        errorMsg = QStringLiteral("无法读取图片");
        return false;
    }

    img = img.convertToFormat(QImage::Format_RGB888);
    // 缩放到 Martin M1 分辨率（320x256，SSTV 最常用模式；接收端
    // 通过 VIS 码自动识别，不影响后续扩展其他模式）
    const SSTVProtocolInfo& info = getProtocolInfo(SSTVMode::MartinM1);
    if (img.width() != info.width || img.height() != info.height) {
        img = img.scaled(info.width, info.height, Qt::IgnoreAspectRatio,
                         Qt::SmoothTransformation);
    }
    // 确保行数据无填充（QImage 可能按 4 字节对齐）
    if (img.bytesPerLine() != info.width * 3) {
        img = img.copy();
    }

    audiot::SSTVEncoder enc;
    enc.setMode(SSTVMode::MartinM1);
    outSamples = enc.encode(img.constBits(), info.width, info.height,
                            sampleRate);
    if (outSamples.empty()) {
        errorMsg = QStringLiteral("SSTV 编码错误");
        return false;
    }

    // 帧尾静音：便于接收端判断帧结束（与 AFSK 帧一致）
    std::vector<float> tail = audiot::TxPipeline::makeSilence(
        1.0f, sampleRate);
    outSamples.insert(outSamples.end(), tail.begin(), tail.end());
    return true;
}

void MainWindow::onExportWav(const QString& textOrFile, const QString& path)
{
    // 与发射无关：直接取当前输入 + 配置现场编码后写入文件。
    // 编码/写文件在后台线程进行（大输入不阻塞 UI），进度经
    // onExportProgress 回报到发射面板进度条。
    config_ = settingsDialog_->getPipelineConfig();
    txPanel_->onTransmissionProgress(0);
    txPanel_->onTransmissionStatus(QStringLiteral("导出中..."));
    statusBar()->showMessage(QStringLiteral("导出 WAV 中..."));

    // 回收上一次未结束的导出线程（若用户连续导出）
    if (txExportThread_ && txExportThread_->joinable()) {
        txExportThread_->join();
        delete txExportThread_;
        txExportThread_ = nullptr;
    }

    PipelineConfig cfg = config_;
    const int gen = ++txExportGen_;
    txExportThread_ = new std::thread([this, textOrFile, path, cfg, gen]() {
        // SSTV 图片导出：直接读取图片编码（不走文本/文件分支）
        if (isImagePath(textOrFile)) {
            QMetaObject::invokeMethod(
                this, [this, gen]() {
                    if (gen != txExportGen_.load()) return;
                    txPanel_->onTransmissionProgress(30);
                }, Qt::QueuedConnection);

            std::vector<float> dataSamples;
            QString err;
            if (!encodeSstvImage(textOrFile, cfg.sampleRate, dataSamples, err)) {
                QMetaObject::invokeMethod(
                    this, [this, gen, err]() {
                        if (gen != txExportGen_.load()) return;
                        txPanel_->onTransmissionProgress(0);
                        txPanel_->onTransmissionStatus(QStringLiteral("导出失败: ") + err);
                        statusBar()->showMessage(QStringLiteral("导出 WAV 失败"), 5000);
                    }, Qt::QueuedConnection);
                return;
            }

            QMetaObject::invokeMethod(
                this, [this, gen]() {
                    if (gen != txExportGen_.load()) return;
                    txPanel_->onTransmissionProgress(70);
                }, Qt::QueuedConnection);

            AudioFormat fmt;
            fmt.sampleRate = cfg.sampleRate;
            fmt.channels = 1;
            fmt.bitsPerSample = 16;

            WavWriter writer;
            bool ok = false;
            if (writer.open(path.toStdString(), fmt)) {
                writer.write(dataSamples);
                writer.close();
                ok = true;
            }

            QMetaObject::invokeMethod(
                this, [this, gen, ok, path]() {
                    if (gen != txExportGen_.load()) return;
                    txPanel_->onTransmissionProgress(100);
                    if (ok) {
                        txPanel_->onTransmissionStatus(QStringLiteral("已导出 WAV: ") + path);
                        statusBar()->showMessage(QStringLiteral("已导出 WAV: ") + path, 5000);
                    } else {
                        txPanel_->onTransmissionStatus(QStringLiteral("导出失败: 无法写入文件"));
                        statusBar()->showMessage(QStringLiteral("导出 WAV 失败"), 5000);
                    }
                }, Qt::QueuedConnection);
            return;
        }

        // 阶段 1：读取输入（文本直接过；文件按字节比例 0-30%）
        QString content = textOrFile;
        bool isFile = textOrFile.contains(QStringLiteral(".")) &&
                      (textOrFile.contains(QStringLiteral("/")) || textOrFile.contains(QStringLiteral("\\")));
        if (isFile) {
            // 中文/非 ASCII 路径无法传输：Windows 下 FileCodec 通过
            // std::ifstream 打开路径（要求 ANSI/ASCII），而 Qt 输入框的
            // 中文路径会变成 UTF-8，导致文件打不开或打错。
            bool hasNonAscii = false;
            for (QChar c : textOrFile) {
                if (c.unicode() > 127) { hasNonAscii = true; break; }
            }
            if (hasNonAscii) {
                QMetaObject::invokeMethod(
                    this, [this, gen]() {
                        if (gen != txExportGen_.load()) return;
                        txPanel_->onTransmissionProgress(0);
                        txPanel_->onTransmissionStatus(QStringLiteral("导出失败: 无法使用中文路径"));
                        statusBar()->showMessage(QStringLiteral("导出 WAV 失败"), 5000);
                    }, Qt::QueuedConnection);
                return;
            }
            QFile file(textOrFile);
            if (!file.open(QIODevice::ReadOnly)) {
                QMetaObject::invokeMethod(
                    this, [this, gen]() {
                        if (gen != txExportGen_.load()) return;
                        txPanel_->onTransmissionProgress(0);
                        txPanel_->onTransmissionStatus(QStringLiteral("导出失败: 无法打开文件"));
                        statusBar()->showMessage(QStringLiteral("导出 WAV 失败"), 5000);
                    }, Qt::QueuedConnection);
                return;
            }
            const qint64 totalBytes = file.size();
            content.clear();
            QByteArray buf;
            while (file.bytesAvailable() > 0) {
                QByteArray part = file.read(1024 * 256);
                if (part.isEmpty()) break;
                buf.append(part);
                const int pct = 10 + static_cast<int>(
                    static_cast<qint64>(buf.size()) * 20 /
                    (totalBytes > 0 ? totalBytes : 1));
                QMetaObject::invokeMethod(
                    this, [this, gen, pct]() {
                        if (gen != txExportGen_.load()) return;
                        txPanel_->onTransmissionProgress(pct);
                    }, Qt::QueuedConnection);
            }
            file.close();
            content = QString::fromUtf8(buf);
        }

        // 阶段 2：编码（30-70%）
        QMetaObject::invokeMethod(
            this, [this, gen]() {
                if (gen != txExportGen_.load()) return;
                txPanel_->onTransmissionProgress(30);
            }, Qt::QueuedConnection);

        audiot::TxPipeline tx(cfg);
        std::vector<float> dataSamples;
        if (isFile) {
            // 文件发送：以 FileCodec 帧打包（文件名+类型+CRC32）后调制
            dataSamples = tx.encodeTypedFile(textOrFile.toStdString());
        } else {
            dataSamples = tx.encodeTypedText(content.toStdString());
        }
        if (dataSamples.empty()) {
            QMetaObject::invokeMethod(
                this, [this, gen]() {
                    if (gen != txExportGen_.load()) return;
                    txPanel_->onTransmissionProgress(0);
                    txPanel_->onTransmissionStatus(QStringLiteral("导出失败: 编码错误"));
                    statusBar()->showMessage(QStringLiteral("导出 WAV 失败"), 5000);
                }, Qt::QueuedConnection);
            return;
        }

        // 导出的 WAV 与实时发射保持相同信号结构：准备音 → 类型帧 →
        // 数据帧 → 帧尾静音，回放/解析时接收端无需区分来源。准备音
        // 长度对齐 AFSK 符号网格，保证其后数据与解调网格对齐。
        std::vector<float> prepTone = audiot::TxPipeline::makePrepAligned(
            cfg.markFreq, cfg.prepToneSeconds, cfg.sampleRate, cfg.baudRate);
        dataSamples.insert(dataSamples.begin(),
                           prepTone.begin(), prepTone.end());

        std::vector<float> endSilence = audiot::TxPipeline::makeSilence(
            cfg.endControlSeconds, cfg.sampleRate);
        dataSamples.insert(dataSamples.end(), endSilence.begin(), endSilence.end());

        QMetaObject::invokeMethod(
            this, [this, gen]() {
                if (gen != txExportGen_.load()) return;
                txPanel_->onTransmissionProgress(70);
            }, Qt::QueuedConnection);

        // 阶段 3：写 WAV 文件（70-100%）
        AudioFormat fmt;
        fmt.sampleRate = cfg.sampleRate;
        fmt.channels = 1;
        fmt.bitsPerSample = 16;

        WavWriter writer;
        bool ok = false;
        if (writer.open(path.toStdString(), fmt)) {
            writer.write(dataSamples);
            writer.close();
            ok = true;
        }

        QMetaObject::invokeMethod(
            this, [this, gen, ok, path]() {
                if (gen != txExportGen_.load()) return;
                txPanel_->onTransmissionProgress(100);
                if (ok) {
                    txPanel_->onTransmissionStatus(QStringLiteral("已导出 WAV: ") + path);
                    statusBar()->showMessage(QStringLiteral("已导出 WAV: ") + path, 5000);
                } else {
                    txPanel_->onTransmissionStatus(QStringLiteral("导出失败: 无法写入文件"));
                    statusBar()->showMessage(QStringLiteral("导出 WAV 失败"), 5000);
                }
            }, Qt::QueuedConnection);
    });
}

bool MainWindow::beginTxPlayback(const std::vector<float>& dataSamples,
                                 bool withPrepTone)
{
    // Continuous prep tone (modem handshake style) at the AFSK mark
    // frequency, giving the receiving device time to prepare. Length is
    // aligned to the AFSK symbol grid so the data that follows stays
    // bit-aligned after demodulation. SSTV 发射跳过准备音（信号自带
    // Leader/VIS 头），txDataStart_ 置 0 使进度/状态直接进入数据阶段。
    if (withPrepTone) {
        std::vector<float> prepTone = audiot::TxPipeline::makePrepAligned(
            config_.markFreq, config_.prepToneSeconds,
            config_.sampleRate, config_.baudRate);
        txPlaybackSamples_ = prepTone;
        txPlaybackSamples_.insert(txPlaybackSamples_.end(),
                                  dataSamples.begin(), dataSamples.end());
        txDataStart_ = prepTone.size();
    } else {
        txPlaybackSamples_ = dataSamples;
        txDataStart_ = 0;
    }
    txPlaybackPos_ = 0;
    txDrainTicks_ = 0;

    // 16-bit mono push-mode output。缓冲加大到 8192 样本（~186ms）：
    // qtaudio_windows 的 bytesFree() 按 period 量化（见 onTxPlaybackTick
    // 注释），20ms 轮询写入在 period 边界会漏写，导致输出缓冲欠载、
    // 声卡静音填充——播放断续，符号被静音打断，接收端解调失败。
    // 大缓冲吸收轮询抖动；启动后立即预填充（见下），避免前 20ms 静音。
    if (!audioOutput_->start(config_.sampleRate, 1, 8192)) {
        txPlaybackSamples_.clear();
        return false;
    }

    // 预填充输出缓冲：启动即写入尽可能多的样本（QAudioOutput 推模式
    // 的 write 受内部缓冲节流，写入量不超过 bytesFree 报出的空闲量，
    // 不会阻塞 GUI），让声卡立即发声且缓冲水位从满开始。
    const int initFreeBytes = audioOutput_->bytesFree();
    if (initFreeBytes > 0) {
        const size_t initWrite = std::min(
            static_cast<size_t>(initFreeBytes / 2),
            txPlaybackSamples_.size());
        if (initWrite > 0) {
            audioOutput_->write(txPlaybackSamples_.data(),
                                static_cast<int>(initWrite));
            txPlaybackPos_ = initWrite;
        }
    }

    txPanel_->onTransmissionProgress(0);
    txPanel_->onTransmissionStatus(withPrepTone
        ? QStringLiteral("准备音中...")
        : QStringLiteral("发射中..."));
    statusBar()->showMessage(withPrepTone
        ? (QStringLiteral("准备音中... (") +
           QString::number(static_cast<double>(config_.prepToneSeconds), 'f', 0) +
           QStringLiteral(" 秒后发送数据)"))
        : QStringLiteral("SSTV 发送中..."));

    // Pace the writes: ~20ms ticks, each tick fills whatever the
    // QAudioOutput buffer currently has free. Never dumps all samples at
    // once, so Stop can interrupt immediately.
    txPlaybackTimer_->start(20);
    return true;
}

void MainWindow::onTxPlaybackTick()
{
    if (!audioOutput_->isRunning()) {
        // Output died underneath us (e.g. stopped elsewhere)
        finishTxPlayback(true);
        return;
    }

    const size_t total = txPlaybackSamples_.size();

    if (txPlaybackPos_ < total) {
        // Write only as many samples as the output buffer can accept now
        const int freeBytes = audioOutput_->bytesFree();
        size_t canWrite = static_cast<size_t>(freeBytes > 0 ? freeBytes / 2 : 0);
        if (canWrite > total - txPlaybackPos_) {
            canWrite = total - txPlaybackPos_;
        }
        if (canWrite > 0) {
            audioOutput_->write(txPlaybackSamples_.data() + txPlaybackPos_,
                                static_cast<int>(canWrite));
            txPlaybackPos_ += canWrite;
        }

        // Phase-aware status + overall progress
        const bool inPrep = txPlaybackPos_ < txDataStart_;
        txPanel_->onTransmissionStatus(inPrep
            ? QStringLiteral("准备音中...")
            : QStringLiteral("发射中..."));
        statusBar()->showMessage(inPrep
            ? QStringLiteral("准备音中...")
            : QStringLiteral("发射中..."));
        if (total > 0) {
            txPanel_->onTransmissionProgress(
                static_cast<int>(txPlaybackPos_ * 100 / total));
        }
    } else {
        // All samples handed to QAudioOutput. Wait for the hardware
        // buffer to drain before declaring completion.
        const int bufferBytes = audioOutput_->bufferBytes();
        if (bufferBytes <= 0 || audioOutput_->bytesFree() >= bufferBytes) {
            finishTxPlayback(false);   // fast path: buffer fully drained
            return;
        }
        // Fallback for the qtaudio_windows plugin: bufferSize() reports
        // the *requested* setBufferSize value while bytesFree() is
        // period-quantized and never reaches the full size once the
        // stream idles, so the drain check above can stay false forever
        // (the old "stuck at 发射中..." bug). After the last write, at
        // most bufferBytes of audio (~23 ms) can still be in flight,
        // so a fixed tail wait of 400 ms safely covers the drain; the
        // drain fast path above doubles as the safety valve, and this
        // tick counter guarantees completion in any case (<= 400 ms).
        ++txDrainTicks_;
        static const int kTailTicks = 20;   // 20 x 20 ms = 400 ms tail
        if (txDrainTicks_ >= kTailTicks) {
            finishTxPlayback(false);
        }
    }
}

void MainWindow::finishTxPlayback(bool aborted)
{
    txPlaybackTimer_->stop();
    txPlaybackSamples_.clear();
    txPlaybackPos_ = 0;
    txDataStart_ = 0;
    txDrainTicks_ = 0;

    if (audioOutput_->isRunning()) {
        audioOutput_->stop();
    }

    if (aborted) {
        txPanel_->onTransmissionStatus(QStringLiteral("已停止"));
        statusBar()->showMessage(QStringLiteral("发射已停止"));
    } else {
        txPanel_->onTransmissionProgress(100);
        txPanel_->onTransmissionStatus(QStringLiteral("发射完成"));
        statusBar()->showMessage(QStringLiteral("发射完成"), 5000);
    }
}

void MainWindow::onStopTransmission()
{
    // Interruptible at any moment: stop pacing timer + audio output
    finishTxPlayback(true);
}

void MainWindow::onStartReceiving(const QString& wavPath)
{
    if (wavPath.isEmpty()) {
        // 麦克风实时模式：能量门控切段 + 后台段解码（与电台复用）
        if (radioListening_) {
            onRadioStopListening();
        }
        if (sstvListening_) {
            stopSstvListening();
        }
        config_ = settingsDialog_->getPipelineConfig();

        // 回收上一次未结束的段解码线程（若有）
        rxMicAborted_ = true;
        if (rxMicParseThread_ && rxMicParseThread_->joinable()) {
            rxMicParseThread_->join();
            delete rxMicParseThread_;
            rxMicParseThread_ = nullptr;
        }

        // 重置切段状态
        rxMicListening_ = true;
        rxMicSeg_.buffer.clear();
        rxMicSeg_.signalSeen = false;
        rxMicSeg_.quietSamples = 0;
        rxMicAborted_ = false;
        ++rxMicParseGen_;   // 使任何过期段解码回调失效

        // 大缓冲（8192 样本 ≈ 186ms）监听，与电台模式一致
        if (!audioInput_->start(config_.sampleRate, 1, 8192)) {
            rxMicListening_ = false;
            rxPanel_->onReceiveProgress(0);
            rxPanel_->onReceiveStatus(QStringLiteral("错误: 无法启动麦克风监听"));
            statusBar()->showMessage(QStringLiteral("监听启动失败"), 5000);
            return;
        }

        rxPanel_->clearReceived();
        rxPanel_->onReceiveProgress(-1);   // 不确定进度（忙碌动画）
        rxPanel_->onReceiveStatus(QStringLiteral("监听中..."));
        statusBar()->showMessage(QStringLiteral("麦克风监听中..."));
        return;
    }

    config_ = settingsDialog_->getPipelineConfig();

    // WAV 文件模式：后台线程一次性解析整个文件，不阻塞 UI
    rxPanel_->clearReceived();
    rxPanel_->onReceiveProgress(0);
    rxPanel_->onReceiveStatus(QStringLiteral("解析中..."));
    statusBar()->showMessage(QStringLiteral("解析 WAV 文件..."));

    // 回收上一次未结束的解析线程（若用户连续解析）
    rxParseAborted_ = true;
    if (rxParseThread_ && rxParseThread_->joinable()) {
        rxParseThread_->join();
        delete rxParseThread_;
        rxParseThread_ = nullptr;
    }
    rxParseAborted_ = false;

    // 拷贝配置供后台线程使用（主线程后续修改 config_ 不影响解析）
    PipelineConfig cfg = config_;
    const int gen = ++rxParseGen_;
    rxParseThread_ = new std::thread([this, wavPath, cfg, gen]() {
        // 分块读取整个 WAV，按读取字节回报解析进度（0-90%）
        audiot::WavReader reader;
        if (!reader.open(wavPath.toStdString())) {
            QMetaObject::invokeMethod(
                this,
                [this, gen]() {
                    if (rxParseAborted_.load() || gen != rxParseGen_.load()) return;
                    rxPanel_->onReceiveProgress(0);
                    rxPanel_->onReceiveStatus(QStringLiteral("解析失败: 无法打开文件"));
                    statusBar()->showMessage(QStringLiteral("解析失败: 无法打开文件"), 5000);
                },
                Qt::QueuedConnection);
            return;
        }

        const size_t total = reader.totalSamples();
        std::vector<float> samples;
        samples.reserve(total);
        const size_t chunk = static_cast<size_t>(cfg.sampleRate) * 2; // 2 秒音频/块
        while (samples.size() < total) {
            std::vector<float> part = reader.read(chunk);
            if (part.empty()) break;
            samples.insert(samples.end(), part.begin(), part.end());
            const int pct = static_cast<int>(samples.size() * 90 / (total ? total : 1));
            QMetaObject::invokeMethod(
                this,
                [this, gen, pct]() {
                    if (rxParseAborted_.load() || gen != rxParseGen_.load()) return;
                    rxPanel_->onReceiveProgress(pct);
                },
                Qt::QueuedConnection);
        }
        reader.close();

        // 解调/解码阶段（通常很快），进入前标记 95%
        QMetaObject::invokeMethod(
            this,
            [this, gen]() {
                if (rxParseAborted_.load() || gen != rxParseGen_.load()) return;
                rxPanel_->onReceiveProgress(95);
            },
            Qt::QueuedConnection);

        audiot::RxPipeline rx(cfg);
        DecodeResult result = rx.decode(samples, true);
        QMetaObject::invokeMethod(
            this,
            [this, gen, result]() {
                // 已被停止或已被新解析取代：丢弃过期结果
                if (rxParseAborted_.load() || gen != rxParseGen_.load()) {
                    return;
                }
                onWavParseFinished(result);
            },
            Qt::QueuedConnection);
    });
}

void MainWindow::presentDecodeResult(const DecodeResult& result)
{
    if (!result.valid()) {
        rxPanel_->onReceiveProgress(100);
        return;
    }

    if (result.type == DecodeResult::TEXT) {
        rxPanel_->onDataReceived(QString::fromUtf8(result.text.c_str()));
    } else {
        // 文件：弹出保存对话框，默认文件名取帧内携带的名字
        const QString defaultPath = QDir::currentPath() + QLatin1Char('/') +
                                    QString::fromUtf8(result.filename.c_str());
        const QString savePath = QFileDialog::getSaveFileName(
            this, QStringLiteral("保存接收文件"), defaultPath);
        if (savePath.isEmpty()) {
            rxPanel_->onReceiveStatus(QStringLiteral("已放弃保存接收文件"));
            statusBar()->showMessage(QStringLiteral("已放弃保存接收文件"), 5000);
        } else {
            QFile file(savePath);
            if (file.open(QIODevice::WriteOnly)) {
                file.write(reinterpret_cast<const char*>(result.fileData.data()),
                           static_cast<qint64>(result.fileData.size()));
                file.close();
                rxPanel_->onDataReceived(
                    QStringLiteral("已保存接收文件: ") + savePath +
                    QStringLiteral(" (%1 字节)").arg(result.fileData.size()));
                statusBar()->showMessage(QStringLiteral("文件已保存: ") + savePath, 5000);
            } else {
                rxPanel_->onReceiveStatus(QStringLiteral("保存失败: 无法写入文件"));
                statusBar()->showMessage(QStringLiteral("保存失败"), 5000);
            }
        }
    }
    rxPanel_->onReceiveProgress(100);
}

void MainWindow::onWavParseFinished(const DecodeResult& result)
{
    // 调用方（排队 lambda）已校验：非停止、非过期解析
    if (rxParseThread_ && rxParseThread_->joinable()) {
        rxParseThread_->join();
        delete rxParseThread_;
        rxParseThread_ = nullptr;
    }

    presentDecodeResult(result);
    if (result.valid()) {
        rxPanel_->onReceiveStatus(QStringLiteral("解析完成"));
        statusBar()->showMessage(QStringLiteral("解析完成"), 5000);
    } else {
        rxPanel_->onReceiveStatus(QStringLiteral("解析完成: 未解析出数据"));
        statusBar()->showMessage(QStringLiteral("解析完成: 未解析出数据"), 5000);
    }
}

void MainWindow::onStopReceiving()
{
    // 麦克风实时模式：停止监听并回收段解码线程
    if (rxMicListening_) {
        stopRxMicListening();
    }
    // 中止后台 WAV 解析（结果丢弃，线程自行收尾）
    rxParseAborted_ = true;
    rxPanel_->onReceiveProgress(0);
    rxPanel_->onReceiveStatus(QStringLiteral("已取消"));
    statusBar()->showMessage(QStringLiteral("解析已取消"), 5000);
}

void MainWindow::onRadioStartListening()
{
    // 接收面板麦克风监听与电台监听互斥（共享同一输入设备）
    if (rxMicListening_) {
        stopRxMicListening();
        rxPanel_->onReceiveStatus(QStringLiteral("已停止"));
    }
    if (sstvListening_) {
        stopSstvListening();
        rxPanel_->onReceiveStatus(QStringLiteral("已停止"));
    }

    config_ = settingsDialog_->getPipelineConfig();

    // 回收上一次未结束的段解码线程（若有）
    radioAborted_ = true;
    if (radioParseThread_ && radioParseThread_->joinable()) {
        radioParseThread_->join();
        delete radioParseThread_;
        radioParseThread_ = nullptr;
    }

    // 重置切段状态
    radioListening_ = true;
    radioSeg_.buffer.clear();
    radioSeg_.signalSeen = false;
    radioSeg_.quietSamples = 0;
    radioAborted_ = false;
    ++radioParseGen_;   // 使任何过期段解码回调失效

    // 大缓冲（8192 样本 ≈ 186ms）监听：麦克风与扬声器分离，
    // 发射期间监听不受影响
    if (!audioInput_->start(config_.sampleRate, 1, 8192)) {
        radioListening_ = false;
        radioPanel_->appendMessage(RadioPanel::MSG_INFO,
                                   QStringLiteral("错误: 无法启动麦克风监听"));
        radioPanel_->setListening(false);
        statusBar()->showMessage(QStringLiteral("监听启动失败"), 5000);
        return;
    }

    radioPanel_->appendMessage(RadioPanel::MSG_INFO, QStringLiteral("开始监听..."));
    radioPanel_->setListening(true);
    statusBar()->showMessage(QStringLiteral("电台监听中..."));
}

void MainWindow::onRadioStopListening()
{
    radioListening_ = false;
    if (audioInput_->isRunning()) {
        audioInput_->stop();
    }

    // 丢弃未完成的信号段与在途解码结果
    radioAborted_ = true;
    if (radioParseThread_ && radioParseThread_->joinable()) {
        radioParseThread_->join();
        delete radioParseThread_;
        radioParseThread_ = nullptr;
    }
    radioSeg_.buffer.clear();
    radioSeg_.signalSeen = false;
    radioSeg_.quietSamples = 0;

    radioPanel_->appendMessage(RadioPanel::MSG_INFO, QStringLiteral("停止监听"));
    radioPanel_->setListening(false);
    statusBar()->showMessage(QStringLiteral("电台监听已停止"), 5000);
}

void MainWindow::onRadioSend(const QString& textOrFile)
{
    // 与发射面板共用编码+播放链路；发射前中止当前播放（如有）
    if (txPlaybackTimer_->isActive() || audioOutput_->isRunning()) {
        finishTxPlayback(true);
    }

    config_ = settingsDialog_->getPipelineConfig();
    std::vector<float> dataSamples;
    QString err;
    if (!encodeTxInput(textOrFile, config_, dataSamples, err)) {
        radioPanel_->appendMessage(RadioPanel::MSG_INFO,
                                   QStringLiteral("发送失败: ") + err);
        return;
    }
    if (!beginTxPlayback(dataSamples)) {
        radioPanel_->appendMessage(RadioPanel::MSG_INFO,
                                   QStringLiteral("发送失败: 无法启动音频输出"));
        return;
    }

    // 自己的消息上屏（文件消息带标记）
    const bool isFile = textOrFile.contains(QStringLiteral(".")) &&
                        (textOrFile.contains(QStringLiteral("/")) ||
                         textOrFile.contains(QStringLiteral("\\")));
    radioPanel_->appendMessage(RadioPanel::MSG_SEND,
        isFile ? (QStringLiteral("[文件] ") + textOrFile) : textOrFile);
}

void MainWindow::accumulateRadio(const float* samples, int count)
{
    std::vector<float> segment;
    accumulateSegment(radioSeg_, samples, count, segment);
    if (!segment.empty()) {
        submitRadioSegment(segment);
    }
}

void MainWindow::accumulateSegment(SegmentState& st,
                                   const float* samples, int count,
                                   std::vector<float>& outSegment)
{
    // 能量门控自动切段：信号出现开始累积，帧尾静音超过切段门限
    // （0.8s）自动提交解码；段超长（30s）强制提交。
    const float signalRms = 0.002f;   // 静音 ~0.0005，信号 ~0.35
    const size_t quietCut = static_cast<size_t>(config_.sampleRate) * 8 / 10;
    const size_t maxSeg = static_cast<size_t>(config_.sampleRate) * 30;

    double sumSq = 0.0;
    for (int i = 0; i < count; ++i) {
        const float v = samples[i];
        sumSq += static_cast<double>(v) * v;
    }
    const double rms = std::sqrt(sumSq / (count > 0 ? count : 1));

    if (rms > signalRms) {
        // 信号块：重置静音计数并累积
        st.signalSeen = true;
        st.quietSamples = 0;
        st.buffer.insert(st.buffer.end(), samples, samples + count);
    } else if (st.signalSeen) {
        // 信号后的静音块：计入段尾，用于切段判定
        st.quietSamples += static_cast<size_t>(count);
        st.buffer.insert(st.buffer.end(), samples, samples + count);
    } else {
        // 段外静音不累积
        st.quietSamples = 0;
    }

    // 切段：信号后静音足够长，或段超长；裁掉段尾静音（保留 0.2s
    // 供帧尾判定），降低解码量
    if (st.signalSeen &&
        (st.quietSamples >= quietCut || st.buffer.size() >= maxSeg)) {
        const size_t keepTail = static_cast<size_t>(config_.sampleRate) / 5;
        if (st.quietSamples > keepTail) {
            st.buffer.resize(st.buffer.size() - (st.quietSamples - keepTail));
        }
        outSegment.swap(st.buffer);
        st.signalSeen = false;
        st.quietSamples = 0;
    }
}

void MainWindow::submitRadioSegment(std::vector<float>& segment)
{
    // 提交到后台线程解码（与 WAV 解析相同的 RxPipeline 路径）
    if (radioParseThread_ && radioParseThread_->joinable()) {
        radioParseThread_->join();
        delete radioParseThread_;
        radioParseThread_ = nullptr;
    }
    PipelineConfig cfg = config_;
    const int gen = ++radioParseGen_;
    radioParseThread_ = new std::thread([this, segment, cfg, gen]() {
        audiot::RxPipeline rx(cfg);
        DecodeResult result = rx.decode(segment, true);
        QMetaObject::invokeMethod(
            this,
            [this, gen, result]() {
                // 已停止监听或已被新段取代：丢弃过期结果
                if (radioAborted_.load() || gen != radioParseGen_.load()) {
                    return;
                }
                presentRadioResult(result);
            },
            Qt::QueuedConnection);
    });
}

void MainWindow::presentRadioResult(const DecodeResult& result)
{
    if (!result.valid()) {
        radioPanel_->appendMessage(RadioPanel::MSG_INFO,
            QStringLiteral("检测到信号但未解出数据"));
        return;
    }

    if (result.type == DecodeResult::TEXT) {
        radioPanel_->appendMessage(RadioPanel::MSG_RECV,
            QString::fromUtf8(result.text.c_str()));
    } else {
        // 文件：自动保存到 exe 目录/接收文件/（聊天软件风格）
        const QString dir = QCoreApplication::applicationDirPath() +
                            QStringLiteral("/接收文件");
        QDir().mkpath(dir);
        const QString savePath = dir + QLatin1Char('/') +
                                 QString::fromUtf8(result.filename.c_str());
        QFile file(savePath);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(reinterpret_cast<const char*>(result.fileData.data()),
                       static_cast<qint64>(result.fileData.size()));
            file.close();
            radioPanel_->appendMessage(RadioPanel::MSG_RECV,
                QStringLiteral("收到文件: ") + savePath +
                QStringLiteral(" (%1 字节)").arg(result.fileData.size()));
        } else {
            radioPanel_->appendMessage(RadioPanel::MSG_INFO,
                QStringLiteral("文件保存失败: ") + savePath);
        }
    }
    statusBar()->showMessage(QStringLiteral("收到电台消息"), 5000);
}

void MainWindow::accumulateRxMic(const float* samples, int count)
{
    std::vector<float> segment;
    accumulateSegment(rxMicSeg_, samples, count, segment);
    if (!segment.empty()) {
        submitRxMicSegment(segment);
    }
}

void MainWindow::submitRxMicSegment(std::vector<float>& segment)
{
    // 提交到后台线程解码（与电台段解码相同的路径）
    if (rxMicParseThread_ && rxMicParseThread_->joinable()) {
        rxMicParseThread_->join();
        delete rxMicParseThread_;
        rxMicParseThread_ = nullptr;
    }
    PipelineConfig cfg = config_;
    const int gen = ++rxMicParseGen_;
    rxMicParseThread_ = new std::thread([this, segment, cfg, gen]() {
        audiot::RxPipeline rx(cfg);
        DecodeResult result = rx.decode(segment, true);
        QMetaObject::invokeMethod(
            this,
            [this, gen, result]() {
                // 已停止监听或已被新段取代：丢弃过期结果
                if (rxMicAborted_.load() || gen != rxMicParseGen_.load()) {
                    return;
                }
                presentRxMicResult(result);
            },
            Qt::QueuedConnection);
    });
}

void MainWindow::presentRxMicResult(const DecodeResult& result)
{
    if (!result.valid()) {
        rxPanel_->onReceiveStatus(QStringLiteral("检测到信号但未解出数据"));
        return;
    }

    // 文本上屏 / 文件弹出保存对话框（与 WAV 解析同一展示路径）
    presentDecodeResult(result);

    // 自动停止机制：解出完整帧（CRC 校验通过）即视为接收完成
    stopRxMicListening();
    rxPanel_->onReceiveStatus(QStringLiteral("接收完成"));
    statusBar()->showMessage(QStringLiteral("接收完成"), 5000);
}

void MainWindow::stopRxMicListening()
{
    rxMicListening_ = false;
    if (audioInput_->isRunning()) {
        audioInput_->stop();
    }

    // 丢弃未完成的信号段与在途解码结果
    rxMicAborted_ = true;
    if (rxMicParseThread_ && rxMicParseThread_->joinable()) {
        rxMicParseThread_->join();
        delete rxMicParseThread_;
        rxMicParseThread_ = nullptr;
    }
    rxMicSeg_.buffer.clear();
    rxMicSeg_.signalSeen = false;
    rxMicSeg_.quietSamples = 0;
}

void MainWindow::onStartSstv(const QString& wavPath)
{
    config_ = settingsDialog_->getPipelineConfig();

    if (wavPath.isEmpty()) {
        // SSTV 麦克风监听：头部检测 + 整帧累积（与电台/麦克风监听互斥）
        if (radioListening_) {
            onRadioStopListening();
        }
        if (rxMicListening_) {
            stopRxMicListening();
        }

        // 回收上一次未结束的解码线程（若有）
        sstvAborted_ = true;
        if (sstvParseThread_ && sstvParseThread_->joinable()) {
            sstvParseThread_->join();
            delete sstvParseThread_;
            sstvParseThread_ = nullptr;
        }

        sstvListening_ = true;
        sstvMicBuffer_.clear();
        sstvHeaderFound_ = false;
        sstvNeedSamples_ = 0;
        sstvDetectCounter_ = 0;
        sstvAborted_ = false;
        ++sstvParseGen_;   // 使任何过期解码回调失效

        if (!audioInput_->start(config_.sampleRate, 1, 8192)) {
            sstvListening_ = false;
            rxPanel_->onReceiveProgress(0);
            rxPanel_->onReceiveStatus(QStringLiteral("错误: 无法启动麦克风监听"));
            statusBar()->showMessage(QStringLiteral("监听启动失败"), 5000);
            return;
        }

        rxPanel_->clearSstv();
        rxPanel_->onReceiveProgress(-1);   // 不确定进度（忙碌动画）
        rxPanel_->onReceiveStatus(QStringLiteral("SSTV 监听中..."));
        statusBar()->showMessage(QStringLiteral("SSTV 监听中..."));
        return;
    }

    // WAV 文件 SSTV 解析：后台线程一次性解码，不阻塞 UI
    rxPanel_->clearSstv();
    rxPanel_->onReceiveProgress(0);
    rxPanel_->onReceiveStatus(QStringLiteral("SSTV 解析中..."));
    statusBar()->showMessage(QStringLiteral("解析 SSTV WAV 文件..."));

    sstvAborted_ = true;
    if (sstvParseThread_ && sstvParseThread_->joinable()) {
        sstvParseThread_->join();
        delete sstvParseThread_;
        sstvParseThread_ = nullptr;
    }
    sstvAborted_ = false;

    const int sr = config_.sampleRate;
    const int gen = ++sstvParseGen_;
    sstvParseThread_ = new std::thread([this, wavPath, sr, gen]() {
        // 分块读取整个 WAV
        audiot::WavReader reader;
        if (!reader.open(wavPath.toStdString())) {
            QMetaObject::invokeMethod(
                this,
                [this, gen]() {
                    if (sstvAborted_.load() || gen != sstvParseGen_.load()) return;
                    rxPanel_->onReceiveProgress(0);
                    rxPanel_->onReceiveStatus(QStringLiteral("解析失败: 无法打开文件"));
                    statusBar()->showMessage(QStringLiteral("解析失败: 无法打开文件"), 5000);
                },
                Qt::QueuedConnection);
            return;
        }

        const size_t total = reader.totalSamples();
        std::vector<float> samples;
        samples.reserve(total);
        const size_t chunk = static_cast<size_t>(sr) * 2; // 2 秒音频/块
        while (samples.size() < total) {
            std::vector<float> part = reader.read(chunk);
            if (part.empty()) break;
            samples.insert(samples.end(), part.begin(), part.end());
            const int pct = static_cast<int>(samples.size() * 90 / (total ? total : 1));
            QMetaObject::invokeMethod(
                this,
                [this, gen, pct]() {
                    if (sstvAborted_.load() || gen != sstvParseGen_.load()) return;
                    rxPanel_->onReceiveProgress(pct);
                },
                Qt::QueuedConnection);
        }
        reader.close();

        audiot::SSTVDecoder dec;
        SSTVDecoder::DecodeResult result = dec.decode(samples, sr);
        QMetaObject::invokeMethod(
            this,
            [this, gen, result]() {
                // 已被停止或已被新解析取代：丢弃过期结果
                if (sstvAborted_.load() || gen != sstvParseGen_.load()) {
                    return;
                }
                onSstvParseFinished(result);
            },
            Qt::QueuedConnection);
    });
}

void MainWindow::onStopSstv()
{
    // 麦克风监听：停止并回收解码线程
    if (sstvListening_) {
        stopSstvListening();
    }
    // 中止后台 WAV 解析（结果丢弃，线程自行收尾）
    sstvAborted_ = true;
    rxPanel_->onReceiveProgress(0);
    rxPanel_->onReceiveStatus(QStringLiteral("已取消"));
    statusBar()->showMessage(QStringLiteral("SSTV 已停止"), 5000);
}

void MainWindow::accumulateSstvMic(const float* samples, int count)
{
    // SSTV 帧（Martin M1 约 38 秒）远长于 AFSK 切段上限（30 秒），
    // 因此不走能量门控切段：先检测 Leader/VIS 头部，再按模式帧长
    // 累积完整一帧后提交后台解码。
    const int sr = config_.sampleRate;
    sstvMicBuffer_.insert(sstvMicBuffer_.end(), samples, samples + count);

    if (!sstvHeaderFound_) {
        // 未找到头部：只保留最近 3 秒做滑动窗口，避免缓冲无限增长
        const size_t maxPre = static_cast<size_t>(sr) * 3;
        if (sstvMicBuffer_.size() > maxPre) {
            sstvMicBuffer_.erase(sstvMicBuffer_.begin(),
                                 sstvMicBuffer_.begin() +
                                     (sstvMicBuffer_.size() - maxPre));
        }

        // 头部检测较贵（全窗口扫描），且回调在 UI 线程，降低频率：
        // 每 4 块（约 0.75 秒）检测一次；SSTV 头部仅 0.6 秒，
        // 检测间隔远小于 3 秒窗口，不会漏检。
        if (++sstvDetectCounter_ < 4) return;
        sstvDetectCounter_ = 0;

        audiot::SSTVDecoder dec;
        SSTVDecoder::DetectionResult det = dec.detect(sstvMicBuffer_, sr);
        if (!det.found) return;

        // 找到头部：按模式参数计算完整帧所需样本数
        sstvHeaderFound_ = true;
        const SSTVProtocolInfo& info = getProtocolInfo(det.mode);
        const size_t syncSamples = static_cast<size_t>(
            info.syncDurMs * 0.001 * sr + 0.5);
        const double scanMs = static_cast<double>(info.lineTimeMs) -
                              info.syncDurMs;
        const double pixelMs = scanMs / info.width;
        const size_t pixelSamples = static_cast<size_t>(
            pixelMs * 0.001 * sr + 0.5);
        const size_t lineSamples = syncSamples + info.width * pixelSamples;
        sstvNeedSamples_ = det.imageStart + info.height * lineSamples;
        rxPanel_->onReceiveStatus(QStringLiteral("已检测到 SSTV 信号，接收中..."));
        return;
    }

    // 整帧收完：提交后台解码（帧含头部，detect 会重新定位）
    if (sstvMicBuffer_.size() >= sstvNeedSamples_) {
        std::vector<float> frame;
        frame.swap(sstvMicBuffer_);
        sstvHeaderFound_ = false;
        sstvNeedSamples_ = 0;
        submitSstvDecode(frame);
    }
}

void MainWindow::submitSstvDecode(std::vector<float>& frame)
{
    if (sstvParseThread_ && sstvParseThread_->joinable()) {
        sstvParseThread_->join();
        delete sstvParseThread_;
        sstvParseThread_ = nullptr;
    }
    const int sr = config_.sampleRate;
    const int gen = ++sstvParseGen_;
    sstvParseThread_ = new std::thread([this, frame, sr, gen]() {
        audiot::SSTVDecoder dec;
        SSTVDecoder::DecodeResult result = dec.decode(frame, sr);
        QMetaObject::invokeMethod(
            this,
            [this, gen, result]() {
                if (sstvAborted_.load() || gen != sstvParseGen_.load()) {
                    return;
                }
                presentSstvResult(result);
            },
            Qt::QueuedConnection);
    });
}

void MainWindow::onSstvParseFinished(const SSTVDecoder::DecodeResult& result)
{
    // 调用方（排队 lambda）已校验：非停止、非过期解析
    if (sstvParseThread_ && sstvParseThread_->joinable()) {
        sstvParseThread_->join();
        delete sstvParseThread_;
        sstvParseThread_ = nullptr;
    }
    presentSstvResult(result);
}

void MainWindow::presentSstvResult(const SSTVDecoder::DecodeResult& result)
{
    // 麦克风监听期间回调：成功后自动停止监听；WAV 解析则无需停止
    const bool fromMic = sstvListening_;

    if (!result.success) {
        rxPanel_->onReceiveProgress(100);
        if (fromMic) {
            // 解出的帧无效（噪声误检等）：复位继续监听下一帧
            rxPanel_->onReceiveStatus(QStringLiteral("SSTV 解码失败，继续监听..."));
        } else {
            rxPanel_->onReceiveStatus(QStringLiteral("解析完成: 未检测到 SSTV 图像"));
            statusBar()->showMessage(QStringLiteral("未检测到 SSTV 图像"), 5000);
        }
        return;
    }

    // 像素转 QImage 上屏（copy 使数据独立于 result.pixels）
    QImage img(result.pixels.data(), result.width, result.height,
               result.width * 3, QImage::Format_RGB888);
    rxPanel_->showSstvImage(img.copy());

    // 自动保存到 exe 目录/接收文件/（与电台文件保存一致）
    const QString dir = QCoreApplication::applicationDirPath() +
                        QStringLiteral("/接收文件");
    QDir().mkpath(dir);
    const QString ts = QDateTime::currentDateTime().toString(
        QStringLiteral("yyyyMMdd_HHmmss"));
    const QString savePath = dir + QLatin1Char('/') +
                             QStringLiteral("SSTV_") + ts +
                             QStringLiteral(".png");
    if (img.save(savePath)) {
        rxPanel_->onReceiveStatus(QStringLiteral("接收完成: 已保存 ") + savePath);
    } else {
        rxPanel_->onReceiveStatus(QStringLiteral("接收完成"));
    }
    statusBar()->showMessage(QStringLiteral("SSTV 接收完成"), 5000);
    rxPanel_->onReceiveProgress(100);

    if (fromMic) {
        stopSstvListening();
    }
}

void MainWindow::stopSstvListening()
{
    sstvListening_ = false;
    if (audioInput_->isRunning()) {
        audioInput_->stop();
    }

    // 丢弃未收完的帧与在途解码结果
    sstvAborted_ = true;
    if (sstvParseThread_ && sstvParseThread_->joinable()) {
        sstvParseThread_->join();
        delete sstvParseThread_;
        sstvParseThread_ = nullptr;
    }
    sstvMicBuffer_.clear();
    sstvHeaderFound_ = false;
    sstvNeedSamples_ = 0;
    sstvDetectCounter_ = 0;
}

} // namespace audiot
