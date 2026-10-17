// main_window.cpp - Main application window implementation
#include "main_window.h"
#include "tx_panel.h"
#include "rx_panel.h"
#include "radio_panel.h"
#include "audio_monitor.h"
#include "settings_dialog.h"
#include "modem_settings_widget.h"

#include <QMenuBar>
#include <QStatusBar>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>
#include <QApplication>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QLineEdit>
#include <QIcon>
#include <QMetaObject>
#include <QCoreApplication>
#include <QTabWidget>
#include <cmath>

#include "core/pipeline/tx_pipeline.h"
#include "core/pipeline/rx_pipeline.h"
#include "audio/wav_reader.h"
#include "audio/wav_writer.h"

#include <QDateTime>

namespace audiot {

namespace {
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
    , txFromRadio_(false)
    , radioListening_(false)
    , radioParseThread_(nullptr)
    , radioAborted_(false)
    , radioParseGen_(0)
    , radioParseRunning_(false)
    , rxMicListening_(false)
    , rxMicParseThread_(nullptr)
    , rxMicAborted_(false)
    , rxMicParseGen_(0)
    , rxMicParseRunning_(false)
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

    // 设置面板常驻右侧：延迟到事件循环开始后枚举音频设备，
    // 避免启动时立即枚举 QAudioDeviceInfo 在部分 Windows 系统上崩溃。
    // 同时加载各收发选项卡的调制设置（INI 配置文件，失败即默认）。
    QTimer::singleShot(0, this, [this]() {
        if (settingsDialog_) {
            settingsDialog_->ensureDevicesLoaded();
        }
        if (txPanel_) txPanel_->loadPanelSettings();
        if (rxPanel_) rxPanel_->loadPanelSettings();
        if (radioPanel_) radioPanel_->loadPanelSettings();
        // 调制设置载入后刷新右侧"高级设置"面板（初始选项卡为"发射"）
        if (settingsDialog_) {
            settingsDialog_->setActiveModem(txPanel_ ? txPanel_->modemSettings() : nullptr);
        }
        // 应用设备选择（原"工具→设置"入口逻辑，菜单已移除）
        onSettings();
    });

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
    // Central widget with horizontal layout: main area + settings panel
    QWidget* central = new QWidget(this);
    QHBoxLayout* rootLayout = new QHBoxLayout(central);
    rootLayout->setContentsMargins(4, 4, 4, 4);
    rootLayout->setSpacing(6);

    // Left: audio monitor + tab widget
    QWidget* left = new QWidget(central);
    QVBoxLayout* mainLayout = new QVBoxLayout(left);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(4);

    // Audio monitor at top
    audioMonitor_ = new AudioMonitor(left);
    mainLayout->addWidget(audioMonitor_);

    // Tab widget for TX/RX/Radio
    tabWidget_ = new QTabWidget(left);

    txPanel_ = new TxPanel(left);
    rxPanel_ = new RxPanel(left);
    radioPanel_ = new RadioPanel(left);

    tabWidget_->addTab(txPanel_, QStringLiteral("发射"));
    tabWidget_->addTab(rxPanel_, QStringLiteral("接收"));
    tabWidget_->addTab(radioPanel_, QStringLiteral("电台"));

    mainLayout->addWidget(tabWidget_);

    rootLayout->addWidget(left, 1);

    // Right: persistent settings panel (常驻，无需再打开弹窗)
    settingsDialog_ = new SettingsDialog(audioDeviceManager_, central);
    settingsDialog_->setObjectName(QStringLiteral("settingsPanel"));
    rootLayout->addWidget(settingsDialog_, 0);

    setCentralWidget(central);
}

void MainWindow::setupMenuBar()
{
    // File menu
    QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("文件"));
    QAction* actionExit = fileMenu->addAction(QStringLiteral("退出"));
    actionExit->setShortcut(QKeySequence::Quit);
    connect(actionExit, &QAction::triggered, qApp, &QApplication::quit);

    // (工具菜单已移除：调制/FEC 设置移入各收发选项卡，
    //  设备/高级设置常驻右侧面板，无需弹窗入口)

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

    // Radio panel signals (双向电台模式)
    connect(radioPanel_, &RadioPanel::startListening,
            this, &MainWindow::onRadioStartListening);
    connect(radioPanel_, &RadioPanel::stopListening,
            this, &MainWindow::onRadioStopListening);
    connect(radioPanel_, &RadioPanel::sendRequested,
            this, &MainWindow::onRadioSend);
    connect(radioPanel_, &RadioPanel::stopSendRequested,
            this, &MainWindow::onRadioStopSend);

    // 设置面板：音频设备选择变更 -> 即时应用（onSettings 里 setDevice）
    connect(settingsDialog_, &SettingsDialog::devicesChanged,
            this, &MainWindow::onSettings);

    // 右侧"高级设置"跟随当前收发选项卡：切到哪个选项卡，就显示/编辑
    // 那个选项卡的调制高级参数（并按其调制方式显隐）
    connect(tabWidget_, &QTabWidget::currentChanged, this, [this](int idx) {
        ModemSettingsWidget* m = nullptr;
        if (idx == 0) m = txPanel_->modemSettings();
        else if (idx == 1) m = rxPanel_->modemSettings();
        else if (idx == 2) m = radioPanel_->modemSettings();
        settingsDialog_->setActiveModem(m);
    });
    settingsDialog_->setActiveModem(txPanel_->modemSettings());

    // Audio input callback -> waveform display + radio/rx-mic segmenting
    audioInput_->setCallback([this](const float* samples, int count) {
        audioMonitor_->updateWaveform(samples, count);
        if (radioListening_) {
            accumulateRadio(samples, count);
        } else if (rxMicListening_) {
            accumulateRxMic(samples, count);
        }
    });
}

void MainWindow::onSettings()
{
    // 设置面板已常驻右侧，无需弹窗；此处仅将当前面板配置应用到收发链路
    // （设备选择/采样率/调制/FEC 等变更即时生效）。
    config_ = settingsDialog_->getPipelineConfig();
    QAudioDeviceInfo inDev = settingsDialog_->selectedInputDevice();
    QAudioDeviceInfo outDev = settingsDialog_->selectedOutputDevice();
    if (!inDev.isNull()) audioInput_->setDevice(inDev);
    if (!outDev.isNull()) audioOutput_->setDevice(outDev);
    statusBar()->showMessage(QStringLiteral("设置已更新"), 3000);
}

PipelineConfig MainWindow::surfaceConfig(const ModemSettingsWidget* modem) const
{
    // 以右侧面板（设备/采样率/高级参数）为基础配置，
    // 再用面板自有的调制设置覆盖调制相关字段。
    PipelineConfig cfg = settingsDialog_->getPipelineConfig();
    if (modem) {
        const PipelineConfig m = modem->getPipelineConfig();
        cfg.modemType = m.modemType;
        cfg.fecType = m.fecType;
        cfg.mftskTier = m.mftskTier;
        cfg.pskOrder = m.pskOrder;
        cfg.pskQamRate = m.pskQamRate;
        cfg.autoDetectModem = m.autoDetectModem;
        // AFSK 的 Mark/Space/波特率同样是 per-tab（由右侧设置面板代理编辑，
        // 保存在该选项卡的 ModemSettingsWidget 里）
        cfg.markFreq = m.markFreq;
        cfg.spaceFreq = m.spaceFreq;
        cfg.baudRate = m.baudRate;
    }
    return cfg;
}

void MainWindow::onAbout()
{
    QMessageBox::about(this, QStringLiteral("关于 AudioT"),
        QStringLiteral("<h3>AudioT v1.7.0</h3>"
                       "<p>音频数据通信软件</p>"
                       "<p>支持 AFSK / MFSK / PSK(差分) / 16-QAM 调制，"
                       "Hamming/Reed-Solomon 纠错编码。</p>"
                       "<p>基于 Qt 5.15 + CMake 构建。</p>"));
}

void MainWindow::onStartTransmission(const QString& textOrFile)
{
    // Abort any in-flight playback before starting a new one
    if (txPlaybackTimer_->isActive() || audioOutput_->isRunning()) {
        finishTxPlayback(true);
    }

    config_ = surfaceConfig(txPanel_->modemSettings());
    txFromRadio_ = false;   // 本次发射来自发射面板
    statusBar()->showMessage(QStringLiteral("开始发射..."));

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
        // 文件发送：仅检查可读性，内容由 FileCodec 直接读取打包。
        // （中文/非 ASCII 路径已支持：底层改用宽字符打开文件。）
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
        // 文件发送：以 FileCodec 帧打包（文件名+类型+CRC32）后调制。
        // 载荷类别按扩展名判定：图片 -> 'I'，视频 -> 'V'，其余 -> 'F'。
        // 三者走同一条数字链路（FEC + 调制），接收端据此区分呈现。
        static const QStringList kImageExt = { QStringLiteral("png"), QStringLiteral("jpg"),
            QStringLiteral("jpeg"), QStringLiteral("bmp"), QStringLiteral("gif"),
            QStringLiteral("webp"), QStringLiteral("tif"), QStringLiteral("tiff") };
        static const QStringList kVideoExt = { QStringLiteral("mp4"), QStringLiteral("avi"),
            QStringLiteral("mkv"), QStringLiteral("mov"), QStringLiteral("wmv"),
            QStringLiteral("flv"), QStringLiteral("webm"), QStringLiteral("m4v") };
        const QString suffix = textOrFile.section(QLatin1Char('.'), -1).toLower();
        if (kImageExt.contains(suffix)) {
            dataSamples = tx.encodeTypedImage(textOrFile.toStdString());
        } else if (kVideoExt.contains(suffix)) {
            dataSamples = tx.encodeTypedVideo(textOrFile.toStdString());
        } else {
            dataSamples = tx.encodeTypedFile(textOrFile.toStdString());
        }
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

void MainWindow::onExportWav(const QString& textOrFile, const QString& path)
{
    // 与发射无关：直接取当前输入 + 配置现场编码后写入文件。
    // 编码/写文件在后台线程进行（大输入不阻塞 UI），进度经
    // onExportProgress 回报到发射面板进度条。
    config_ = surfaceConfig(txPanel_->modemSettings());
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

        // 阶段 1：读取输入（文本直接过；文件按字节比例 0-30%）
        QString content = textOrFile;
        bool isFile = textOrFile.contains(QStringLiteral(".")) &&
                      (textOrFile.contains(QStringLiteral("/")) || textOrFile.contains(QStringLiteral("\\")));
        if (isFile) {
            // 中文/非 ASCII 路径已支持（底层宽字符打开），仅检查可读性
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
    // bit-aligned after demodulation. When withPrepTone is false
    // (no prep tone) txDataStart_ is set to 0 so the progress/status
    // goes straight into the data phase.
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

    if (txFromRadio_) {
        // 电台面板发射：进度/状态回报到电台面板（发送中锁定选项、显示进度条）
        radioPanel_->setSending(true);
        radioPanel_->setSendProgress(withPrepTone ? -1 : 0);
    } else {
        txPanel_->onTransmissionProgress(0);
        txPanel_->onTransmissionStatus(withPrepTone
            ? QStringLiteral("准备音中...")
            : QStringLiteral("发射中..."));
    }
    statusBar()->showMessage(withPrepTone
        ? (QStringLiteral("准备音中... (") +
           QString::number(static_cast<double>(config_.prepToneSeconds), 'f', 0) +
           QStringLiteral(" 秒后发送数据)"))
        : QStringLiteral("发射中..."));

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
        statusBar()->showMessage(inPrep
            ? QStringLiteral("准备音中...")
            : QStringLiteral("发射中..."));
        if (total > 0) {
            const int pct = static_cast<int>(txPlaybackPos_ * 100 / total);
            if (txFromRadio_) {
                radioPanel_->setSendProgress(pct);
            } else {
                txPanel_->onTransmissionStatus(inPrep
                    ? QStringLiteral("准备音中...")
                    : QStringLiteral("发射中..."));
                txPanel_->onTransmissionProgress(pct);
            }
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

    if (txFromRadio_) {
        // 电台面板：结束"发送中"状态（按钮/选项恢复），不写发射面板
        radioPanel_->setSending(false);
        statusBar()->showMessage(aborted ? QStringLiteral("发送已停止")
                                         : QStringLiteral("发送完成"), 5000);
        return;
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
    config_ = surfaceConfig(rxPanel_->modemSettings());

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

    config_ = surfaceConfig(rxPanel_->modemSettings());

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
        // 文件/图片/视频：弹出保存对话框，默认文件名取帧内携带的名字
        const QString kind = (result.type == DecodeResult::IMAGE) ? QStringLiteral("图片")
                           : (result.type == DecodeResult::VIDEO) ? QStringLiteral("视频")
                                                                  : QStringLiteral("文件");
        const QString defaultPath = QDir::currentPath() + QLatin1Char('/') +
                                    QString::fromUtf8(result.filename.c_str());
        const QString savePath = QFileDialog::getSaveFileName(
            this, QStringLiteral("保存接收") + kind, defaultPath);
        if (savePath.isEmpty()) {
            rxPanel_->onReceiveStatus(QStringLiteral("已放弃保存接收") + kind);
            statusBar()->showMessage(QStringLiteral("已放弃保存接收") + kind, 5000);
        } else {
            QFile file(savePath);
            if (file.open(QIODevice::WriteOnly)) {
                file.write(reinterpret_cast<const char*>(result.fileData.data()),
                           static_cast<qint64>(result.fileData.size()));
                file.close();
                rxPanel_->onDataReceived(
                    QStringLiteral("已保存接收") + kind + QStringLiteral(": ") + savePath +
                    QStringLiteral(" (%1 字节)").arg(result.fileData.size()));
                statusBar()->showMessage(kind + QStringLiteral("已保存: ") + savePath, 5000);
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

    config_ = surfaceConfig(radioPanel_->modemSettings());

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

    txFromRadio_ = true;   // 本次发射来自电台面板
    config_ = surfaceConfig(radioPanel_->modemSettings());
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

    // 自己的消息上屏：文件用可点击链接气泡，文本用普通气泡
    const bool isFile = textOrFile.contains(QStringLiteral(".")) &&
                        (textOrFile.contains(QStringLiteral("/")) ||
                         textOrFile.contains(QStringLiteral("\\")));
    if (isFile) {
        radioPanel_->appendFileMessage(RadioPanel::MSG_SEND,
            QFileInfo(textOrFile).fileName(), textOrFile);
    } else {
        radioPanel_->appendMessage(RadioPanel::MSG_SEND, textOrFile);
    }
}

void MainWindow::onRadioStopSend()
{
    // 发送中点击"停止"（面板已二次确认）：中止发射，并给对方发一条
    // 「停止消息」，让正在收听的对方知道发送已中止。
    const bool wasRadio = txFromRadio_;
    if (txPlaybackTimer_->isActive() || audioOutput_->isRunning()) {
        finishTxPlayback(true);
    }
    if (wasRadio) {
        onRadioSend(QStringLiteral("[发送已停止]"));
    }
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
    // 提交到后台线程解码（与 WAV 解析相同的 RxPipeline 路径）。
    //
    // 本函数由音频回调线程调用，因此**绝不能 join 正在解码的线程**：
    // 一次解码可能耗时数百毫秒，若在此阻塞，音频回调会被拖住，进而使
    // QAudioInput::stop() 长时间等待（表现为"停止监听无响应"）。
    // 上一次解码尚未完成时直接丢弃本段 —— 实时监听宁丢帧、不卡顿。
    if (radioParseRunning_.load()) {
        return;
    }
    if (radioParseThread_ && radioParseThread_->joinable()) {
        radioParseThread_->join();   // 此时线程已结束，join 立即返回
        delete radioParseThread_;
        radioParseThread_ = nullptr;
    }
    PipelineConfig cfg = config_;
    const int gen = ++radioParseGen_;
    radioParseRunning_ = true;
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
        radioParseRunning_ = false;   // 本线程已结束，可被安全回收
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
        // 文件/图片/视频：保存到系统「下载」目录（聊天软件风格），并在聊天
        // 界面显示为可点击的文件气泡（点击用系统默认程序打开）。
        const QString kind = (result.type == DecodeResult::IMAGE) ? QStringLiteral("图片")
                           : (result.type == DecodeResult::VIDEO) ? QStringLiteral("视频")
                                                                  : QStringLiteral("文件");
        QString dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        if (dir.isEmpty()) {
            dir = QCoreApplication::applicationDirPath() + QStringLiteral("/接收文件");
        }
        QDir().mkpath(dir);
        const QString fileName = QString::fromUtf8(result.filename.c_str());
        const QString savePath = dir + QLatin1Char('/') + fileName;
        QFile file(savePath);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(reinterpret_cast<const char*>(result.fileData.data()),
                       static_cast<qint64>(result.fileData.size()));
            file.close();
            radioPanel_->appendFileMessage(RadioPanel::MSG_RECV, fileName, savePath);
        } else {
            radioPanel_->appendMessage(RadioPanel::MSG_INFO,
                kind + QStringLiteral("保存失败: ") + savePath);
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
    // 提交到后台线程解码（与电台段解码相同的路径）。
    // 同 submitRadioSegment：本函数在音频回调线程里执行，绝不 join 正在
    // 解码的线程，未完成时丢弃本段。
    if (rxMicParseRunning_.load()) {
        return;
    }
    if (rxMicParseThread_ && rxMicParseThread_->joinable()) {
        rxMicParseThread_->join();   // 此时线程已结束，join 立即返回
        delete rxMicParseThread_;
        rxMicParseThread_ = nullptr;
    }
    PipelineConfig cfg = config_;
    const int gen = ++rxMicParseGen_;
    rxMicParseRunning_ = true;
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
        rxMicParseRunning_ = false;   // 本线程已结束，可被安全回收
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


} // namespace audiot
