// settings_dialog.h - Persistent settings panel (right-hand side of the
// main window). Replaces the old modal dialog: audio device + pipeline
// configuration is always visible and applied live.
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

class SettingsDialog : public QWidget {
    Q_OBJECT
public:
    explicit SettingsDialog(AudioDeviceManager* deviceManager, QWidget* parent = nullptr);

    // Get current settings
    PipelineConfig getPipelineConfig() const;
    QAudioDeviceInfo selectedInputDevice() const;
    QAudioDeviceInfo selectedOutputDevice() const;

    // Deferred device enumeration; call once after the panel becomes visible.
    void ensureDevicesLoaded();

public slots:
    void saveSettings();

private slots:
    void onRefreshDevices();

private:
    void setupUI();
    void populateDevices();
    void loadSettings();

    AudioDeviceManager* deviceManager_;
    bool devicesLoaded_;

    // Audio devices
    QComboBox* comboInputDevice_;
    QComboBox* comboOutputDevice_;
    QPushButton* btnRefreshDevices_;

    // Audio parameters (advanced/collapsible)
    QComboBox* comboSampleRate_;
    QComboBox* comboBufferSize_;
    QSpinBox* spinMarkFreq_;
    QSpinBox* spinSpaceFreq_;
    QSpinBox* spinBaudRate_;

    // Advanced controls group (sample rate, buffer, mark/space, baud)
    QGroupBox* advancedGroup_;
};

} // namespace audiot
#endif // AUDIOT_SETTINGS_DIALOG_H
