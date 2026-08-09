// settings_dialog.cpp - Settings dialog implementation
#include "settings_dialog.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QSettings>
#include <QList>
#include <QShowEvent>

namespace audiot {

SettingsDialog::SettingsDialog(AudioDeviceManager* deviceManager, QWidget* parent)
    : QDialog(parent)
    , deviceManager_(deviceManager)
    , devicesLoaded_(false)
{
    setWindowTitle(QStringLiteral("AudioT 设置"));
    setMinimumWidth(400);
    setupUI();
    loadSettings();
}

void SettingsDialog::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // --- Audio devices ---
    QGroupBox* deviceGroup = new QGroupBox(QStringLiteral("音频设备"), this);
    QFormLayout* deviceLayout = new QFormLayout(deviceGroup);

    comboInputDevice_ = new QComboBox(this);
    comboOutputDevice_ = new QComboBox(this);
    btnRefreshDevices_ = new QPushButton(QStringLiteral("刷新设备"), this);

    // Device enumeration is deferred to first show (early enumeration of
    // QAudioDeviceInfo::availableDevices() can crash on some Windows systems)
    comboInputDevice_->addItem(QStringLiteral("默认输入设备"));
    comboOutputDevice_->addItem(QStringLiteral("默认输出设备"));
    connect(btnRefreshDevices_, &QPushButton::clicked,
            this, &SettingsDialog::onRefreshDevices);

    deviceLayout->addRow(QStringLiteral("输入设备:"), comboInputDevice_);
    deviceLayout->addRow(QStringLiteral("输出设备:"), comboOutputDevice_);
    deviceLayout->addRow(QString(), btnRefreshDevices_);
    mainLayout->addWidget(deviceGroup);

    // --- Audio parameters ---
    QGroupBox* audioGroup = new QGroupBox(QStringLiteral("音频参数"), this);
    QFormLayout* audioLayout = new QFormLayout(audioGroup);

    comboSampleRate_ = new QComboBox(this);
    comboSampleRate_->addItem(QStringLiteral("22050"), 22050);
    comboSampleRate_->addItem(QStringLiteral("44100"), 44100);
    comboSampleRate_->addItem(QStringLiteral("48000"), 48000);
    comboSampleRate_->setCurrentIndex(1);
    audioLayout->addRow(QStringLiteral("采样率:"), comboSampleRate_);

    comboBufferSize_ = new QComboBox(this);
    comboBufferSize_->addItem(QStringLiteral("256"), 256);
    comboBufferSize_->addItem(QStringLiteral("512"), 512);
    comboBufferSize_->addItem(QStringLiteral("1024"), 1024);
    comboBufferSize_->addItem(QStringLiteral("2048"), 2048);
    comboBufferSize_->addItem(QStringLiteral("4096"), 4096);
    comboBufferSize_->setCurrentIndex(2); // 1024 default
    audioLayout->addRow(QStringLiteral("缓冲区大小:"), comboBufferSize_);

    mainLayout->addWidget(audioGroup);

    // --- Modulation parameters ---
    QGroupBox* modemGroup = new QGroupBox(QStringLiteral("调制参数"), this);
    QFormLayout* modemLayout = new QFormLayout(modemGroup);

    comboModem_ = new QComboBox(this);
    comboModem_->addItem(QStringLiteral("AFSK"), static_cast<int>(ModemType::AFSK));
    // PSK/QAM exist in the codebase but are NOT wired into the TX/RX
    // pipeline yet (the pipeline always modulates with AFSK), so they
    // are intentionally left out of the selection list.
    modemLayout->addRow(QStringLiteral("调制方式:"), comboModem_);

    spinMarkFreq_ = new QSpinBox(this);
    spinMarkFreq_->setRange(300, 8000);
    spinMarkFreq_->setValue(1200);
    spinMarkFreq_->setSuffix(QStringLiteral(" Hz"));
    modemLayout->addRow(QStringLiteral("Mark 频率:"), spinMarkFreq_);

    spinSpaceFreq_ = new QSpinBox(this);
    spinSpaceFreq_->setRange(300, 8000);
    spinSpaceFreq_->setValue(2200);
    spinSpaceFreq_->setSuffix(QStringLiteral(" Hz"));
    modemLayout->addRow(QStringLiteral("Space 频率:"), spinSpaceFreq_);

    spinBaudRate_ = new QSpinBox(this);
    spinBaudRate_->setRange(100, 9600);
    spinBaudRate_->setValue(1200);
    spinBaudRate_->setSuffix(QStringLiteral(" bps"));
    modemLayout->addRow(QStringLiteral("波特率:"), spinBaudRate_);

    mainLayout->addWidget(modemGroup);

    // --- FEC ---
    QGroupBox* fecGroup = new QGroupBox(QStringLiteral("纠错编码"), this);
    QFormLayout* fecLayout = new QFormLayout(fecGroup);

    comboFEC_ = new QComboBox(this);
    comboFEC_->addItem(QStringLiteral("None"), 0);
    comboFEC_->addItem(QStringLiteral("Hamming"), 1);
    comboFEC_->addItem(QStringLiteral("Reed-Solomon"), 2);
    // 默认 Hamming：真实声学链路（扬声器→麦克风、磁带机）存在符号
    // 级错误，无纠错（None）时任意 1 位错误都会使 CRC 失败整帧作废；
    // Hamming(7,4) 能纠正单符号错误。
    comboFEC_->setCurrentIndex(1);
    fecLayout->addRow(QStringLiteral("FEC 类型:"), comboFEC_);

    mainLayout->addWidget(fecGroup);

    // --- Buttons ---
    buttonBox_ = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox_, &QDialogButtonBox::accepted, this, [this]() {
        saveSettings();
        accept();
    });
    connect(buttonBox_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox_);
}

void SettingsDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    // Defer device enumeration until the dialog is actually shown
    // (avoids startup crash in QAudioDeviceInfo::availableDevices())
    if (!devicesLoaded_) {
        populateDevices();
    }
}

void SettingsDialog::onRefreshDevices()
{
    populateDevices();
}

void SettingsDialog::populateDevices()
{
    if (!deviceManager_) return;

    deviceManager_->refreshDevices();

    QString currentIn = comboInputDevice_->currentText();
    QString currentOut = comboOutputDevice_->currentText();

    comboInputDevice_->clear();
    comboOutputDevice_->clear();

    const QList<AudioDeviceInfo> inputs = deviceManager_->inputDevices();
    for (const auto& info : inputs) {
        comboInputDevice_->addItem(info.deviceName);
    }
    if (comboInputDevice_->count() == 0) {
        comboInputDevice_->addItem(QStringLiteral("(未找到输入设备)"));
    }

    const QList<AudioDeviceInfo> outputs = deviceManager_->outputDevices();
    for (const auto& info : outputs) {
        comboOutputDevice_->addItem(info.deviceName);
    }
    if (comboOutputDevice_->count() == 0) {
        comboOutputDevice_->addItem(QStringLiteral("(未找到输出设备)"));
    }

    // Restore previous selection if still available
    int idx = comboInputDevice_->findText(currentIn);
    comboInputDevice_->setCurrentIndex(idx >= 0 ? idx : 0);
    idx = comboOutputDevice_->findText(currentOut);
    comboOutputDevice_->setCurrentIndex(idx >= 0 ? idx : 0);

    devicesLoaded_ = true;
}

void SettingsDialog::loadSettings()
{
    QSettings settings(QStringLiteral("AudioT"), QStringLiteral("AudioT"));

    comboSampleRate_->setCurrentIndex(
        comboSampleRate_->findData(settings.value(QStringLiteral("sampleRate"), 44100).toInt()));
    comboBufferSize_->setCurrentIndex(
        comboBufferSize_->findData(settings.value(QStringLiteral("bufferSize"), 1024).toInt()));
    comboModem_->setCurrentIndex(
        comboModem_->findData(settings.value(QStringLiteral("modemType"), 0).toInt()));
    // Fall back to AFSK if the saved modem type is no longer selectable
    if (comboModem_->currentIndex() < 0) {
        comboModem_->setCurrentIndex(0);
    }
    spinMarkFreq_->setValue(settings.value(QStringLiteral("markFreq"), 1200).toInt());
    spinSpaceFreq_->setValue(settings.value(QStringLiteral("spaceFreq"), 2200).toInt());
    spinBaudRate_->setValue(settings.value(QStringLiteral("baudRate"), 1200).toInt());
    comboFEC_->setCurrentIndex(
        comboFEC_->findData(settings.value(QStringLiteral("fecType"), 1).toInt()));
}

void SettingsDialog::saveSettings()
{
    QSettings settings(QStringLiteral("AudioT"), QStringLiteral("AudioT"));

    settings.setValue(QStringLiteral("sampleRate"), comboSampleRate_->currentData().toInt());
    settings.setValue(QStringLiteral("bufferSize"), comboBufferSize_->currentData().toInt());
    settings.setValue(QStringLiteral("modemType"), comboModem_->currentData().toInt());
    settings.setValue(QStringLiteral("markFreq"), spinMarkFreq_->value());
    settings.setValue(QStringLiteral("spaceFreq"), spinSpaceFreq_->value());
    settings.setValue(QStringLiteral("baudRate"), spinBaudRate_->value());
    settings.setValue(QStringLiteral("fecType"), comboFEC_->currentData().toInt());
}

PipelineConfig SettingsDialog::getPipelineConfig() const
{
    PipelineConfig cfg;
    cfg.sampleRate = comboSampleRate_->currentData().toInt();
    cfg.modemType = static_cast<ModemType>(comboModem_->currentData().toInt());
    cfg.fecType = static_cast<FecType>(comboFEC_->currentData().toInt());
    cfg.markFreq = static_cast<float>(spinMarkFreq_->value());
    cfg.spaceFreq = static_cast<float>(spinSpaceFreq_->value());
    cfg.baudRate = spinBaudRate_->value();
    return cfg;
}

QAudioDeviceInfo SettingsDialog::selectedInputDevice() const
{
    if (!deviceManager_) return QAudioDeviceInfo();
    // Use the cached enumeration; re-calling availableDevices() and letting
    // its QAudioDeviceInfo objects go out of scope can crash the audio plugin
    return deviceManager_->qtInputDeviceByName(comboInputDevice_->currentText());
}

QAudioDeviceInfo SettingsDialog::selectedOutputDevice() const
{
    if (!deviceManager_) return QAudioDeviceInfo();
    return deviceManager_->qtOutputDeviceByName(comboOutputDevice_->currentText());
}

} // namespace audiot
