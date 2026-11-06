// settings_dialog.cpp - Persistent settings panel (right-hand side)
#include "settings_dialog.h"
#include "modem_settings_widget.h"
#include "app_config.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QFrame>
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
    setMinimumWidth(250);
    setupUI();
    loadSettings();
}

void SettingsDialog::setupUI()
{
    // 面板外层用滚动区包裹：窗口变矮时面板可滚动，不挤压任务区。
    QVBoxLayout* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    QScrollArea* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    outer->addWidget(scroll);

    QWidget* content = new QWidget(scroll);
    scroll->setWidget(content);
    QVBoxLayout* mainLayout = new QVBoxLayout(content);
    mainLayout->setContentsMargins(2, 2, 2, 2);
    mainLayout->setSpacing(6);

    // ================= 一、音频设置（全局） =================
    QGroupBox* deviceGroup = new QGroupBox(QStringLiteral("音频设置"), this);
    QVBoxLayout* deviceOuter = new QVBoxLayout(deviceGroup);
    deviceOuter->setSpacing(4);

    QFormLayout* deviceLayout = new QFormLayout();
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
    deviceOuter->addLayout(deviceLayout);

    // 输入设备占用状态（默认空闲）：共享输入设备时给出明确提示
    noteDevice_ = new QLabel(QStringLiteral("输入设备状态：空闲"), deviceGroup);
    noteDevice_->setWordWrap(true);
    noteDevice_->setStyleSheet(QStringLiteral("color:#9a9a9a; font-size:11px;"));
    deviceOuter->addWidget(noteDevice_);

    // 全局音频参数：直接展示（不再藏进折叠的"高级设置"）——
    // 采样率与缓冲区是经常影响任务成败的设置。
    QLabel* sepGlobal = new QLabel(QStringLiteral("全局音频参数（发射 / 接收 / 电台共用）"),
                                   deviceGroup);
    sepGlobal->setStyleSheet(QStringLiteral(
        "color:#b0b0b0; font-weight:bold; padding-top:4px;"));
    deviceOuter->addWidget(sepGlobal);

    QFormLayout* globalLayout = new QFormLayout();
    comboSampleRate_ = new QComboBox(deviceGroup);
    comboSampleRate_->addItem(QStringLiteral("22050 Hz"), 22050);
    comboSampleRate_->addItem(QStringLiteral("44100 Hz"), 44100);
    comboSampleRate_->addItem(QStringLiteral("48000 Hz"), 48000);
    comboSampleRate_->setCurrentIndex(1);
    globalLayout->addRow(QStringLiteral("采样率:"), comboSampleRate_);

    comboBufferSize_ = new QComboBox(deviceGroup);
    comboBufferSize_->addItem(QStringLiteral("256 samples"), 256);
    comboBufferSize_->addItem(QStringLiteral("512 samples"), 512);
    comboBufferSize_->addItem(QStringLiteral("1024 samples"), 1024);
    comboBufferSize_->addItem(QStringLiteral("2048 samples"), 2048);
    comboBufferSize_->addItem(QStringLiteral("4096 samples"), 4096);
    comboBufferSize_->setCurrentIndex(2);  // 1024 default
    comboBufferSize_->setToolTip(QStringLiteral(
        "缓冲区越小延迟越低，越大越能吸收抖动；出现爆音/卡顿时调大。"));
    globalLayout->addRow(QStringLiteral("缓冲区:"), comboBufferSize_);
    deviceOuter->addLayout(globalLayout);

    noteGlobal_ = new QLabel(QStringLiteral(
        "以上参数对所有任务生效，修改后影响后续操作；任务进行中不宜修改。"),
        deviceGroup);
    noteGlobal_->setWordWrap(true);
    noteGlobal_->setStyleSheet(QStringLiteral("color:#9a9a9a; font-size:11px;"));
    deviceOuter->addWidget(noteGlobal_);

    // 全局参数改动即时保存
    connect(comboSampleRate_, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &SettingsDialog::saveSettings);
    connect(comboBufferSize_, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &SettingsDialog::saveSettings);

    mainLayout->addWidget(deviceGroup);

    // ================= 二、当前任务参数（跟随选项卡） =================
    // 调制方式、FEC、自动检测在各收发选项卡自带的"调制参数"组里；这里只
    // 摆**跟随当前选项卡**的调制高级参数，并明确标注它们的归属，避免用户
    // 误以为改了全局设置。
    taskGroup_ = new QGroupBox(QStringLiteral("当前任务参数"), this);
    QVBoxLayout* taskOuter = new QVBoxLayout(taskGroup_);
    taskOuter->setSpacing(4);

    noteTask_ = new QLabel(taskGroup_);
    noteTask_->setWordWrap(true);
    noteTask_->setStyleSheet(QStringLiteral("color:#9a9a9a; font-size:11px;"));
    taskOuter->addWidget(noteTask_);

    QFormLayout* advLayout = new QFormLayout();

    // AFSK 专用
    spinMarkFreq_ = new QSpinBox(taskGroup_);
    spinMarkFreq_->setRange(300, 8000);
    spinMarkFreq_->setValue(1200);
    spinMarkFreq_->setSuffix(QStringLiteral(" Hz"));
    advLayout->addRow(QStringLiteral("Mark 频率:"), spinMarkFreq_);

    spinSpaceFreq_ = new QSpinBox(taskGroup_);
    spinSpaceFreq_->setRange(300, 8000);
    spinSpaceFreq_->setValue(2200);
    spinSpaceFreq_->setSuffix(QStringLiteral(" Hz"));
    advLayout->addRow(QStringLiteral("Space 频率:"), spinSpaceFreq_);

    spinBaudRate_ = new QSpinBox(taskGroup_);
    spinBaudRate_->setRange(100, 9600);
    spinBaudRate_->setValue(1200);
    spinBaudRate_->setSuffix(QStringLiteral(" bps"));
    advLayout->addRow(QStringLiteral("波特率:"), spinBaudRate_);

    // MFSK 专用
    comboMftskTier_ = new QComboBox(taskGroup_);
    comboMftskTier_->addItem(QStringLiteral("稳健 (50 符号/s)"), static_cast<int>(MftskTier::ROBUST));
    comboMftskTier_->addItem(QStringLiteral("均衡 (100 符号/s)"), static_cast<int>(MftskTier::BALANCED));
    comboMftskTier_->addItem(QStringLiteral("高速 (150 符号/s)"), static_cast<int>(MftskTier::SPORT));
    advLayout->addRow(QStringLiteral("MFSK 档位:"), comboMftskTier_);

    // PSK 专用
    comboPskOrder_ = new QComboBox(taskGroup_);
    comboPskOrder_->addItem(QStringLiteral("DBPSK (1 bit/符号)"), 1);
    comboPskOrder_->addItem(QStringLiteral("DQPSK (2 bit/符号)"), 2);
    comboPskOrder_->addItem(QStringLiteral("D8PSK (3 bit/符号)"), 3);
    advLayout->addRow(QStringLiteral("PSK 阶数:"), comboPskOrder_);

    // PSK / QAM 共用
    comboPskQamRate_ = new QComboBox(taskGroup_);
    comboPskQamRate_->addItem(QStringLiteral("稳健 (735 符号/s, 载波 1470 Hz)"),
                              static_cast<int>(PskQamRate::ROBUST));
    comboPskQamRate_->addItem(QStringLiteral("均衡 (1050 符号/s, 载波 2100 Hz)"),
                              static_cast<int>(PskQamRate::BALANCED));
    comboPskQamRate_->addItem(QStringLiteral("高速 (1470 符号/s, 载波 2940 Hz)"),
                              static_cast<int>(PskQamRate::SPORT));
    advLayout->addRow(QStringLiteral("PSK/QAM 档位:"), comboPskQamRate_);

    taskLayout_ = advLayout;
    taskOuter->addLayout(advLayout);

    // 调制高级参数改动写回当前选项卡
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

    mainLayout->addWidget(taskGroup_);
    mainLayout->addStretch();

    // 初始：无活动选项卡时隐藏全部调制高级项
    updateAdvancedVisibility();
}

void SettingsDialog::setActiveModem(ModemSettingsWidget* modem, const QString& taskName)
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
    // 标题与说明明确标注参数归属：避免用户误以为改了全局设置
    if (taskGroup_) {
        taskGroup_->setTitle(taskName.isEmpty()
            ? QStringLiteral("当前任务参数")
            : QStringLiteral("当前任务参数（%1）").arg(taskName));
    }
    if (noteTask_) {
        noteTask_->setText(taskName.isEmpty()
            ? QStringLiteral("仅适用于当前任务。切换选项卡后显示对应任务的参数。")
            : QStringLiteral("仅适用于“%1”任务；切换选项卡后显示对应任务的参数，"
                             "与其他任务互不影响。").arg(taskName));
    }
    refreshFromActiveModem();
    updateAdvancedVisibility();
}

void SettingsDialog::setInputDeviceBusy(const QString& byTask)
{
    if (!noteDevice_) return;
    if (byTask.isEmpty()) {
        noteDevice_->setText(QStringLiteral("输入设备状态：空闲"));
        noteDevice_->setStyleSheet(QStringLiteral("color:#9a9a9a; font-size:11px;"));
    } else {
        noteDevice_->setText(QStringLiteral("输入设备状态：正被“%1”任务占用").arg(byTask));
        noteDevice_->setStyleSheet(QStringLiteral("color:#ff7b72; font-size:11px;"));
    }
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
    QFormLayout* advLayout = taskLayout_;
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
