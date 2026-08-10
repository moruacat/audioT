// settings_dialog.cpp - Persistent settings panel (right-hand side)
#include "settings_dialog.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QSettings>
#include <QList>
#include <QCheckBox>
#include <QMessageBox>

namespace audiot {

SettingsDialog::SettingsDialog(AudioDeviceManager* deviceManager, QWidget* parent)
    : QWidget(parent)
    , deviceManager_(deviceManager)
    , devicesLoaded_(false)
{
    setMinimumWidth(230);
    setupUI();
    loadSettings();
}

void SettingsDialog::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(2, 2, 2, 2);

    // --- Audio devices ---
    QGroupBox* deviceGroup = new QGroupBox(QStringLiteral("音频设备"), this);
    QFormLayout* deviceLayout = new QFormLayout(deviceGroup);

    comboInputDevice_ = new QComboBox(deviceGroup);
    comboOutputDevice_ = new QComboBox(deviceGroup);
    btnRefreshDevices_ = new QPushButton(QStringLiteral("刷新设备"), deviceGroup);

    // Device enumeration is deferred to ensureDevicesLoaded() (early
    // enumeration of QAudioDeviceInfo::availableDevices() can crash on
    // some Windows systems).
    comboInputDevice_->addItem(QStringLiteral("默认输入设备"));
    comboOutputDevice_->addItem(QStringLiteral("默认输出设备"));
    connect(btnRefreshDevices_, &QPushButton::clicked,
            this, &SettingsDialog::onRefreshDevices);

    deviceLayout->addRow(QStringLiteral("输入设备:"), comboInputDevice_);
    deviceLayout->addRow(QStringLiteral("输出设备:"), comboOutputDevice_);
    deviceLayout->addRow(QString(), btnRefreshDevices_);
    mainLayout->addWidget(deviceGroup);

    // --- Modulation parameters ---
    QGroupBox* modemGroup = new QGroupBox(QStringLiteral("调制参数"), this);
    QFormLayout* modemLayout = new QFormLayout(modemGroup);

    comboModem_ = new QComboBox(modemGroup);
    comboModem_->addItem(QStringLiteral("AFSK"), static_cast<int>(ModemType::AFSK));
    comboModem_->addItem(QStringLiteral("MFSK (4-FSK)"), static_cast<int>(ModemType::MFSK));
    modemLayout->addRow(QStringLiteral("调制方式:"), comboModem_);

    comboMftskTier_ = new QComboBox(modemGroup);
    comboMftskTier_->addItem(QStringLiteral("稳健 (50/s)"), static_cast<int>(MftskTier::ROBUST));
    comboMftskTier_->addItem(QStringLiteral("均衡 (100/s)"), static_cast<int>(MftskTier::BALANCED));
    comboMftskTier_->addItem(QStringLiteral("高速 (150/s)"), static_cast<int>(MftskTier::SPORT));
    modemLayout->addRow(QStringLiteral("MFSK 档位:"), comboMftskTier_);

    checkAutoDetect_ = new QCheckBox(QStringLiteral("自动检测调制方式"), modemGroup);
    modemLayout->addRow(QStringLiteral(""), checkAutoDetect_);

    QObject::connect(comboMftskTier_, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
        this, [this](int idx) {
            if (idx >= 0 && comboMftskTier_->itemData(idx).toInt() == static_cast<int>(MftskTier::SPORT)) {
                QMessageBox::warning(this, QStringLiteral("高速档提示"),
                    QStringLiteral("高速档 (M-FSK 150 符号/s) 吞吐更高，但对噪声/干扰更敏感，\n"
                                   "在较差链路上更容易出错。建议仅在信道质量良好时使用。"));
            }
            saveSettings();
        });

    // 调制方式、自动检测变更时即时保存
    connect(comboModem_, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &SettingsDialog::saveSettings);
    connect(checkAutoDetect_, &QCheckBox::toggled, this, &SettingsDialog::saveSettings);

    mainLayout->addWidget(modemGroup);

    // --- FEC ---
    QGroupBox* fecGroup = new QGroupBox(QStringLiteral("纠错编码"), this);
    QFormLayout* fecLayout = new QFormLayout(fecGroup);

    comboFEC_ = new QComboBox(fecGroup);
    comboFEC_->addItem(QStringLiteral("None"), 0);
    comboFEC_->addItem(QStringLiteral("Hamming"), 1);
    comboFEC_->addItem(QStringLiteral("Reed-Solomon"), 2);
    comboFEC_->setCurrentIndex(1);
    fecLayout->addRow(QStringLiteral("FEC 类型:"), comboFEC_);
    connect(comboFEC_, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &SettingsDialog::saveSettings);

    mainLayout->addWidget(fecGroup);

    // --- Advanced / collapsible ---
    advancedGroup_ = new QGroupBox(QStringLiteral("高级设置"), this);
    advancedGroup_->setCheckable(true);
    advancedGroup_->setChecked(false);  // collapsed by default
    QFormLayout* advLayout = new QFormLayout(advancedGroup_);

    comboSampleRate_ = new QComboBox(advancedGroup_);
    comboSampleRate_->addItem(QStringLiteral("22050"), 22050);
    comboSampleRate_->addItem(QStringLiteral("44100"), 44100);
    comboSampleRate_->addItem(QStringLiteral("48000"), 48000);
    comboSampleRate_->setCurrentIndex(1);
    advLayout->addRow(QStringLiteral("采样率:"), comboSampleRate_);

    comboBufferSize_ = new QComboBox(advancedGroup_);
    comboBufferSize_->addItem(QStringLiteral("256"), 256);
    comboBufferSize_->addItem(QStringLiteral("512"), 512);
    comboBufferSize_->addItem(QStringLiteral("1024"), 1024);
    comboBufferSize_->addItem(QStringLiteral("2048"), 2048);
    comboBufferSize_->addItem(QStringLiteral("4096"), 4096);
    comboBufferSize_->setCurrentIndex(2); // 1024 default
    advLayout->addRow(QStringLiteral("缓冲区大小:"), comboBufferSize_);

    spinMarkFreq_ = new QSpinBox(advancedGroup_);
    spinMarkFreq_->setRange(300, 8000);
    spinMarkFreq_->setValue(1200);
    spinMarkFreq_->setSuffix(QStringLiteral(" Hz"));
    advLayout->addRow(QStringLiteral("Mark 频率:"), spinMarkFreq_);

    spinSpaceFreq_ = new QSpinBox(advancedGroup_);
    spinSpaceFreq_->setRange(300, 8000);
    spinSpaceFreq_->setValue(2200);
    spinSpaceFreq_->setSuffix(QStringLiteral(" Hz"));
    advLayout->addRow(QStringLiteral("Space 频率:"), spinSpaceFreq_);

    spinBaudRate_ = new QSpinBox(advancedGroup_);
    spinBaudRate_->setRange(100, 9600);
    spinBaudRate_->setValue(1200);
    spinBaudRate_->setSuffix(QStringLiteral(" bps"));
    advLayout->addRow(QStringLiteral("波特率:"), spinBaudRate_);

    connect(comboSampleRate_, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &SettingsDialog::saveSettings);
    connect(comboBufferSize_, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &SettingsDialog::saveSettings);
    connect(spinMarkFreq_, static_cast<void(QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            this, &SettingsDialog::saveSettings);
    connect(spinSpaceFreq_, static_cast<void(QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            this, &SettingsDialog::saveSettings);
    connect(spinBaudRate_, static_cast<void(QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            this, &SettingsDialog::saveSettings);

    mainLayout->addWidget(advancedGroup_);
    mainLayout->addStretch();
}

void SettingsDialog::ensureDevicesLoaded()
{
    // Defer device enumeration until the panel is actually visible
    // (avoids startup crash in QAudioDeviceInfo::availableDevices()).
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
    if (comboMftskTier_) {
        comboMftskTier_->setCurrentIndex(
            comboMftskTier_->findData(settings.value(QStringLiteral("mftskTier"),
                static_cast<int>(MftskTier::BALANCED)).toInt()));
        if (comboMftskTier_->currentIndex() < 0) {
            comboMftskTier_->setCurrentIndex(
                comboMftskTier_->findData(static_cast<int>(MftskTier::BALANCED)));
        }
        checkAutoDetect_->setChecked(
            settings.value(QStringLiteral("autoDetectModem"), true).toBool());
    }
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
    if (comboMftskTier_) {
        settings.setValue(QStringLiteral("mftskTier"), comboMftskTier_->currentData().toInt());
        settings.setValue(QStringLiteral("autoDetectModem"), checkAutoDetect_->isChecked());
    }
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
    if (comboMftskTier_) {
        cfg.mftskTier = static_cast<MftskTier>(comboMftskTier_->currentData().toInt());
    }
    if (checkAutoDetect_) {
        cfg.autoDetectModem = checkAutoDetect_->isChecked();
    }
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
