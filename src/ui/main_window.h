// main_window.h - Main application window
#ifndef AUDIOT_MAIN_WINDOW_H
#define AUDIOT_MAIN_WINDOW_H

#include <QMainWindow>
#include <QTabWidget>
#include <QAction>
#include <QTimer>

#include <vector>
#include <thread>
#include <atomic>

#include "core/pipeline/pipeline_config.h"
#include "core/pipeline/rx_pipeline.h"
#include "audio/audio_device.h"
#include "audio/qt_audio_stream.h"

namespace audiot {

class TxPanel;
class RxPanel;
class RadioPanel;
class AudioMonitor;
class SettingsDialog;
class ModemSettingsWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private slots:
    void onSettings();
    void onAbout();
    void onStartTransmission(const QString& textOrFile);
    void onStopTransmission();
    void onExportWav(const QString& textOrFile, const QString& path);
    void onStartReceiving(const QString& wavPath);
    void onStopReceiving();
    void onTxPlaybackTick();
    void onWavParseFinished(const DecodeResult& result);

    // 电台模式（双向）：监听切段解码 + 发送
    void onRadioStartListening();
    void onRadioStopListening();
    void onRadioSend(const QString& textOrFile);

private:
    void setupUI();
    void setupMenuBar();
    void setupStatusBar();
    void connectSignals();

    // 展示一次解码结果（文本显示 / 文件保存对话框），供 WAV 解析使用
    void presentDecodeResult(const DecodeResult& result);

    // 实时监听切段累积状态（电台与接收面板麦克风模式共用）
    struct SegmentState {
        std::vector<float> buffer;   // 当前累积的信号段
        bool signalSeen = false;     // 段内是否见过信号
        size_t quietSamples = 0;     // 段内连续静音样本数
    };
    // 能量门控切段状态机，回调线程每块调用；形成完整信号段时
    // 填充 outSegment（已裁段尾静音）并重置状态
    void accumulateSegment(SegmentState& st, const float* samples, int count,
                           std::vector<float>& outSegment);

    // 电台模式：展示一段信号解码结果（文本显示 / 文件自动保存到
    // 接收文件目录），失败时给出提示
    void presentRadioResult(const DecodeResult& result);
    // 电台模式：能量门控切段（回调线程），完整段提交后台解码
    void accumulateRadio(const float* samples, int count);
    // 电台模式：提交一段完整信号到后台线程解码
    void submitRadioSegment(std::vector<float>& segment);

    // 接收面板麦克风模式：切段 + 后台解码 + 结果展示 + 停止
    void accumulateRxMic(const float* samples, int count);
    void submitRxMicSegment(std::vector<float>& segment);
    void presentRxMicResult(const DecodeResult& result);
    // 停止麦克风监听并回收段解码线程（幂等）
    void stopRxMicListening();

    // Real-time TX playback orchestration (prep tone + modulated data)
    bool beginTxPlayback(const std::vector<float>& dataSamples,
                         bool withPrepTone = true);
    void finishTxPlayback(bool aborted);

    // 读取输入（文本或文件）并编码为调制音频样本（含帧末静音），
    // 发射与 WAV 导出共用；失败时返回 false 并设置 errorMsg
    bool encodeTxInput(const QString& textOrFile, const PipelineConfig& config,
                       std::vector<float>& outSamples, QString& errorMsg);

    // 合并配置：以右侧面板（设备/采样率/高级参数）为基础，用指定
    // 面板的调制设置（调制方式/FEC/档位/自动检测）覆盖调制字段。
    PipelineConfig surfaceConfig(const ModemSettingsWidget* modem) const;

    // UI components
    TxPanel* txPanel_;
    RxPanel* rxPanel_;
    RadioPanel* radioPanel_;
    AudioMonitor* audioMonitor_;
    QTabWidget* tabWidget_;

    // Settings
    SettingsDialog* settingsDialog_;

    // Core objects
    AudioDeviceManager* audioDeviceManager_;
    QtAudioInputStream* audioInput_;
    QtAudioOutputStream* audioOutput_;
    PipelineConfig config_;

    // Menu actions
    QAction* actionAbout_;

    // TX playback state
    QTimer* txPlaybackTimer_;
    std::vector<float> txPlaybackSamples_;  // prep tone + data samples
    size_t txPlaybackPos_;
    size_t txDataStart_;                    // index where data audio begins
    int txDrainTicks_;                      // ticks elapsed after last sample written

    // 电台模式状态（监听切段 + 后台段解码）
    bool radioListening_;                   // 麦克风监听中
    SegmentState radioSeg_;                 // 切段累积状态
    std::thread* radioParseThread_;         // 段解码线程（结果回 UI）
    std::atomic<bool> radioAborted_;        // 监听停止后丢弃过期解析结果
    std::atomic<int> radioParseGen_;        // 区分新旧段解码回调

    // 接收面板麦克风实时模式状态（与电台监听互斥，共享输入设备）
    bool rxMicListening_;                   // 麦克风监听中
    SegmentState rxMicSeg_;                 // 切段累积状态
    std::thread* rxMicParseThread_;         // 段解码线程（结果回 UI）
    std::atomic<bool> rxMicAborted_;        // 监听停止后丢弃过期解析结果
    std::atomic<int> rxMicParseGen_;        // 区分新旧段解码回调

    // RX WAV 后台解析：长音频不阻塞 UI，停止时丢弃结果
    std::thread* rxParseThread_;
    std::atomic<bool> rxParseAborted_;
    std::atomic<int> rxParseGen_;           // 区分新旧解析的回调（丢弃过期结果）

    // TX WAV 后台导出：编码+写文件不阻塞 UI，防止旧回调串台
    std::thread* txExportThread_;
    std::atomic<int> txExportGen_;
};

} // namespace audiot
#endif // AUDIOT_MAIN_WINDOW_H
