// settings_dialog.cpp - Persistent settings panel (right-hand side)
#include "settings_dialog.h"
#include "modem_settings_widget.h"
#include "app_config.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QSettings>
#include <QMessageBox>
#include <QList>

namespace audiot {

namespace {
// Qt 5.15 MinGW 的 QFormLayout 没有 setRowVisible()，通过同时隐藏字段控件
// 与其行标签实现"整行显隐"。
void setFormRowVisible(QFormLayout* layout, QWidget* field, bool visible)
{
    if (!layout || !field) return;
    field->setVisible(visible);
    if (QWidget* label = layout->labelForField(field)) {
        label->setVisible(visible);
    }
}
} // namespace

SettingsDialog::SettingsDialog(AudioDeviceManager* deviceManager, QWidget* parent)
    : QWidget(parent)
    , deviceManager_(deviceManager)
    , devicesLoaded_(false)
    , updating_(false)
    , activeModem_(nullptr)
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
    // 设备下拉变更：即时通知主窗口切换 + 持久化
    connect(comboInputDevice_, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &SettingsDialog::onDeviceChanged);
    connect(comboOutputDevice_, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &SettingsDialog::onDeviceChanged);

    deviceLayout->addRow(QStringLiteral("输入设备:"), comboInputDevice_);
    deviceLayout->addRow(QStringLiteral("输出设备:"), comboOutputDevice_);
    deviceLayout->addRow(QString(), btnRefreshDevices_);
    mainLayout->addWidget(deviceGroup);

    // --- Advanced / collapsible ---
    // 调制方式、FEC、自动检测属于各收发选项卡（ModemSettingsWidget）；这里
    // 只保留全局参数 + **跟随当前选项卡**的调制高级参数，避免同一组控件在
    // 三个选项卡里重复出现。
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
    comboBufferSize_->setCurrentIndex(2);  // 1024 default
    advLayout->addRow(QStringLiteral("缓冲区大小:"), comboBufferSize_);

    // AFSK 专用
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

    // MFSK 专用
    comboMftskTier_ = new QComboBox(advancedGroup_);
    comboMftskTier_->addItem(QStringLiteral("稳健 (50/s)"), static_cast<int>(MftskTier::ROBUST));
    comboMftskTier_->addItem(QStringLiteral("均衡 (100/s)"), static_cast<int>(MftskTier::BALANCED));
    comboMftskTier_->addItem(QStringLiteral("高速 (150/s)"), static_cast<int>(MftskTier::SPORT));
    advLayout->addRow(QStringLiteral("MFSK 档位:"), comboMftskTier_);

    // PSK 专用
    comboPskOrder_ = new QComboBox(advancedGroup_);
    comboPskOrder_->addItem(QStringLiteral("DBPSK (1 bit/符号)"), 1);
    comboPskOrder_->addItem(QStringLiteral("DQPSK (2 bit/符号)"), 2);
    comboPskOrder_->addItem(QStringLiteral("D8PSK (3 bit/符号)"), 3);
    advLayout->addRow(QStringLiteral("PSK 阶数:"), comboPskOrder_);

    // PSK / QAM 共用
    comboPskQamRate_ = new QComboBox(advancedGroup_);
    comboPskQamRate_->addItem(QStringLiteral("稳健 (735 符号/s, 载波 1470 Hz)"),
                              static_cast<int>(PskQamRate::ROBUST));
    comboPskQamRate_->addItem(QStringLiteral("均衡 (1050 符号/s, 载波 2100 Hz)"),
                              static_cast<int>(PskQamRate::BALANCED));
    comboPskQamRate_->addItem(QStringLiteral("高速 (1470 符号/s, 载波 2940 Hz)"),
                              static_cast<int>(PskQamRate::SPORT));
    advLayout->addRow(QStringLiteral("PSK/QAM 档位:"), comboPskQamRate_);

    // 全局参数改动即时保存；调制高级参数改动写回当前选项卡
    connect(comboSampleRate_, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &SettingsDialog::saveSettings);
    connect(comboBufferSize_, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &SettingsDialog::saveSettings);
    connect(spinMarkFreq_, static_cast<void(QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            this, &SettingsDialog::onAdvancedChanged);
    connect(spinSpaceFreq_, static_cast<void(QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            this, &SettingsDialog::onAdvancedChanged);
    connect(spinBaudRate_, static_cast<void(QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            this, &SettingsDialog::onAdvancedChanged);
    connect(comboMftskTier_, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &SettingsDialog::onAdvancedChanged);
    connect(comboPskOrder_, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &SettingsDialog::onAdvancedChanged);
    connect(comboPskQamRate_, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &SettingsDialog::onAdvancedChanged);

    mainLayout->addWidget(advancedGroup_);
    mainLayout->addStretch();

    // 初始：无活动选项卡时隐藏全部调制高级项
    updateAdvancedVisibility();
}

void SettingsDialog::setActiveModem(ModemSettingsWidget* modem)
{
    if (activeModem_) {
        disconnect(activeModem_, nullptr, this, nullptr);
    }
    activeModem_ = modem;
    if (activeModem_) {
        // 切换调制方式时刷新显隐
        connect(activeModem_, &ModemSettingsWidget::modemTypeChanged,
                this, [this](int) { updateAdvancedVisibility(); });
    }
    refreshFromActiveModem();
    updateAdvancedVisibility();
}

void SettingsDialog::refreshFromActiveModem()
{
    updating_ = true;
    if (activeModem_) {
        spinMarkFreq_->setValue(activeModem_->markFreq());
        spinSpaceFreq_->setValue(activeModem_->spaceFreq());
        spinBaudRate_->setValue(activeModem_->baudRate());
        comboMftskTier_->setCurrentIndex(comboMftskTier_->findData(activeModem_->mftskTier()));
        comboPskOrder_->setCurrentIndex(comboPskOrder_->findData(activeModem_->pskOrder()));
        comboPskQamRate_->setCurrentIndex(comboPskQamRate_->findData(activeModem_->pskQamRate()));
    }
    updating_ = false;
}

void SettingsDialog::updateAdvancedVisibility()
{
    if (!advancedGroup_) return;
    QFormLayout* advLayout = qobject_cast<QFormLayout*>(advancedGroup_->layout());
    if (!advLayout) return;

    const int mt = activeModem_ ? activeModem_->modemTypeValue()
                                : -1;   // -1：无活动选项卡 -> 全部隐藏
    // AFSK 专用
    setFormRowVisible(advLayout, spinMarkFreq_,  mt == static_cast<int>(ModemType::AFSK));
    setFormRowVisible(advLayout, spinSpaceFreq_, mt == static_cast<int>(ModemType::AFSK));
    setFormRowVisible(advLayout, spinBaudRate_,  mt == static_cast<int>(ModemType::AFSK));
    // MFSK 专用
    setFormRowVisible(advLayout, comboMftskTier_, mt == static_cast<int>(ModemType::MFSK));
    // PSK 专用
    setFormRowVisible(advLayout, comboPskOrder_,  mt == static_cast<int>(ModemType::PSK));
    // PSK / QAM 共用
    setFormRowVisible(advLayout, comboPskQamRate_,
        mt == static_cast<int>(ModemType::PSK) || mt == static_cast<int>(ModemType::QAM));
}

void SettingsDialog::onAdvancedChanged()
{
    if (updating_ || !activeModem_) return;

    activeModem_->setMarkFreq(spinMarkFreq_->value());
    activeModem_->setSpaceFreq(spinSpaceFreq_->value());
    activeModem_->setBaudRate(spinBaudRate_->value());

    const int tier = comboMftskTier_->currentData().toInt();
    activeModem_->setMftskTier(tier);
    if (tier == static_cast<int>(MftskTier::SPORT)) {
        QMessageBox::warning(this, QStringLiteral("高速档提示"),
            QStringLiteral("高速档 (M-FSK 150 符号/s) 吞吐更高，但对噪声/干扰更敏感，\n"
                           "在较差链路上更容易出错。建议仅在信道质量良好时使用。"));
    }

    activeModem_->setPskOrder(comboPskOrder_->currentData().toInt());
    activeModem_->setPskQamRate(comboPskQamRate_->currentData().toInt());
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

void SettingsDialog::onDeviceChanged()
{
    // 首次枚举填充下拉时会触发本槽；此时不应用也不保存（避免覆盖持久值）
    if (!devicesLoaded_) return;
    saveSettings();
    emit devicesChanged();
}

void SettingsDialog::populateDevices()
{
    if (!deviceManager_) return;

    deviceManager_->refreshDevices();

    QString currentIn = comboInputDevice_->currentText();
    QString currentOut = comboOutputDevice_->currentText();

    if (!devicesLoaded_) {
        // 首次枚举：优先恢复上次持久化的设备选择（否则用列表首项）
        QSettings settings(audiot::appConfigFilePath(), QSettings::IniFormat);
        settings.beginGroup(QStringLiteral("audio"));
        const QString savedIn = settings.value(QStringLiteral("inputDevice")).toString();
        const QString savedOut = settings.value(QStringLiteral("outputDevice")).toString();
        settings.endGroup();
        if (!savedIn.isEmpty()) currentIn = savedIn;
        if (!savedOut.isEmpty()) currentOut = savedOut;
    }

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
    // 统一使用与调制设置相同的 INI 配置文件（见 app_config.h），
    // 不再写注册表；分组 [audio]。
    QSettings settings(audiot::appConfigFilePath(), QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("audio"));

    comboSampleRate_->setCurrentIndex(
        comboSampleRate_->findData(settings.value(QStringLiteral("sampleRate"), 44100).toInt()));
    comboBufferSize_->setCurrentIndex(
        comboBufferSize_->findData(settings.value(QStringLiteral("bufferSize"), 1024).toInt()));
    settings.endGroup();
}

void SettingsDialog::saveSettings()
{
    QSettings settings(audiot::appConfigFilePath(), QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("audio"));

    settings.setValue(QStringLiteral("sampleRate"), comboSampleRate_->currentData().toInt());
    settings.setValue(QStringLiteral("bufferSize"), comboBufferSize_->currentData().toInt());
    // 音频设备选择（下拉文本即设备名）
    settings.setValue(QStringLiteral("inputDevice"), comboInputDevice_->currentText());
    settings.setValue(QStringLiteral("outputDevice"), comboOutputDevice_->currentText());
    settings.endGroup();
    settings.sync();
}

PipelineConfig SettingsDialog::getPipelineConfig() const
{
    // 仅提供全局字段（采样率）；调制方式/FEC/自动检测由各收发选项卡的
    // ModemSettingsWidget 提供，调制高级参数也保存在那里（由本面板代理
    // 显示），MainWindow::surfaceConfig() 负责合并。
    PipelineConfig cfg;
    cfg.sampleRate = comboSampleRate_->currentData().toInt();
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
