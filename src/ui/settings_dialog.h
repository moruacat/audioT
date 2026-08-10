// settings_dialog.h - Settings dialog for audio device and pipeline configuration
#ifndef AUDIOT_SETTINGS_DIALOG_H
#define AUDIOT_SETTINGS_DIALOG_H

#include <QDialog>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QAudioDeviceInfo>

#include "audio/audio_device.h"
#include "core/pipeline/pipeline_config.h"

namespace audiot {

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(AudioDeviceManager* deviceManager, QWidget* parent = nullptr);

    // Get current settings
    PipelineConfig getPipelineConfig() const;
    QAudioDeviceInfo selectedInputDevice() const;
    QAudioDeviceInfo selectedOutputDevice() const;

private slots:
    void loadSettings();
    void saveSettings();
    void onRefreshDevices();

protected:
    void showEvent(QShowEvent* event) override;

private:
    void setupUI();
    void populateDevices();

    AudioDeviceManager* deviceManager_;
    bool devicesLoaded_;

    // Audio devices
    QComboBox* comboInputDevice_;
    QComboBox* comboOutputDevice_;
    QPushButton* btnRefreshDevices_;

    // Audio parameters
    QComboBox* comboSampleRate_;
    QComboBox* comboBufferSize_;

    // Modulation parameters
    QComboBox* comboModem_;
    QSpinBox* spinMarkFreq_;
    QSpinBox* spinSpaceFreq_;
    QSpinBox* spinBaudRate_;

    // M-FSK tier + receive-side auto-detection
    QComboBox* comboMftskTier_;
    QCheckBox* checkAutoDetect_;  // auto-detect AFSK / M-FSK on receive

    // FEC
    QComboBox* comboFEC_;

    // Buttons
    QDialogButtonBox* buttonBox_;
};

} // namespace audiot
#endif // AUDIOT_SETTINGS_DIALOG_H
