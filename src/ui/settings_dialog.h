// settings_dialog.h - Persistent settings panel (right-hand side of the
// main window). 常驻显示：音频设备 + 全局参数（采样率/缓冲区）+
// **跟随当前收发选项卡**的调制高级参数。
#ifndef AUDIOT_SETTINGS_DIALOG_H
#define AUDIOT_SETTINGS_DIALOG_H

#include <QWidget>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QAudioDeviceInfo>

#include "audio/audio_device.h"
#include "core/pipeline/pipeline_config.h"

class QGroupBox;

namespace audiot {

class ModemSettingsWidget;

class SettingsDialog : public QWidget {
    Q_OBJECT
public:
    explicit SettingsDialog(AudioDeviceManager* deviceManager, QWidget* parent = nullptr);

    // Get current settings（全局部分：采样率）
    PipelineConfig getPipelineConfig() const;
    QAudioDeviceInfo selectedInputDevice() const;
    QAudioDeviceInfo selectedOutputDevice() const;

    // Deferred device enumeration; call once after the panel becomes visible.
    void ensureDevicesLoaded();

    // 高级设置面板跟随当前收发选项卡：显示并编辑该选项卡的调制高级参数
    // （Mark/Space/波特率、MFSK 档位、PSK 阶数、PSK/QAM 档位），并按该
    // 选项卡当前的调制方式决定显示哪些项。传 nullptr 表示无活动选项卡。
    void setActiveModem(ModemSettingsWidget* modem);

public slots:
    void saveSettings();

signals:
    // 音频设备选择变更：主窗口据此即时切换设备（本类已负责持久化）
    void devicesChanged();

private slots:
    void onRefreshDevices();
    void onDeviceChanged();
    void onAdvancedChanged();      // 高级控件被用户改动 -> 写回活动选项卡

private:
    void setupUI();
    void populateDevices();
    void loadSettings();
    void refreshFromActiveModem();   // 从活动选项卡读值填充高级控件
    void updateAdvancedVisibility(); // 按调制方式显隐高级项

    AudioDeviceManager* deviceManager_;
    bool devicesLoaded_;
    bool updating_;                    // 刷新控件期间抑制写回
    ModemSettingsWidget* activeModem_; // 当前选项卡的调制设置（可空）

    // Audio devices
    QComboBox* comboInputDevice_;
    QComboBox* comboOutputDevice_;
    QPushButton* btnRefreshDevices_;

    // 全局参数
    QComboBox* comboSampleRate_;
    QComboBox* comboBufferSize_;

    // 调制高级参数（跟随活动选项卡，按调制方式显隐）
    QSpinBox* spinMarkFreq_;      // AFSK：Mark 频率
    QSpinBox* spinSpaceFreq_;     // AFSK：Space 频率
    QSpinBox* spinBaudRate_;      // AFSK：波特率
    QComboBox* comboMftskTier_;   // MFSK：档位
    QComboBox* comboPskOrder_;    // PSK：阶数
    QComboBox* comboPskQamRate_;  // PSK / QAM：档位

    QGroupBox* advancedGroup_;
};

} // namespace audiot
#endif // AUDIOT_SETTINGS_DIALOG_H
