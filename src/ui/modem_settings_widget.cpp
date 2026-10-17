// modem_settings_widget.cpp - Reusable modem/pipeline settings widget
#include "modem_settings_widget.h"
#include "app_config.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QSettings>

namespace audiot {

namespace {
// Qt 5.15 MinGW 的 QFormLayout 没有 setRowVisible()，这里通过同时
// 隐藏字段控件与其行标签实现"整行显隐"。
void setFormRowVisible(QFormLayout* layout, QWidget* field, bool visible)
{
    if (!layout || !field) return;
    field->setVisible(visible);
    if (QWidget* label = layout->labelForField(field)) {
        label->setVisible(visible);
    }
}
} // namespace

ModemSettingsWidget::ModemSettingsWidget(QWidget* parent)
    : QWidget(parent)
    , layout_(nullptr)
    , autoDetectRow_(-1)
    , comboModem_(nullptr)
    , checkAutoDetect_(nullptr)
    , comboFEC_(nullptr)
    , mftskTier_(static_cast<int>(MftskTier::BALANCED))
    , pskOrder_(2)
    , pskQamRate_(static_cast<int>(PskQamRate::BALANCED))
    , markFreq_(1200)
    , spaceFreq_(2200)
    , baudRate_(1200)
{
    setupUI();
}

void ModemSettingsWidget::setupUI()
{
    QGroupBox* modemGroup = new QGroupBox(QStringLiteral("调制参数"), this);
    layout_ = new QFormLayout(modemGroup);
    layout_->setContentsMargins(6, 6, 6, 6);

    comboModem_ = new QComboBox(modemGroup);
    comboModem_->addItem(QStringLiteral("AFSK"), static_cast<int>(ModemType::AFSK));
    comboModem_->addItem(QStringLiteral("MFSK (4-FSK)"), static_cast<int>(ModemType::MFSK));
    comboModem_->addItem(QStringLiteral("PSK (差分)"), static_cast<int>(ModemType::PSK));
    comboModem_->addItem(QStringLiteral("16-QAM"), static_cast<int>(ModemType::QAM));
    layout_->addRow(QStringLiteral("调制方式:"), comboModem_);

    checkAutoDetect_ = new QCheckBox(
        QStringLiteral("自动检测调制方式 (AFSK / M-FSK / PSK / QAM)"), modemGroup);
    autoDetectRow_ = layout_->rowCount();
    layout_->addRow(QString(), checkAutoDetect_);

    comboFEC_ = new QComboBox(modemGroup);
    comboFEC_->addItem(QStringLiteral("None"), 0);
    comboFEC_->addItem(QStringLiteral("Hamming"), 1);
    comboFEC_->addItem(QStringLiteral("Reed-Solomon"), 2);
    comboFEC_->setCurrentIndex(1);  // Hamming default
    layout_->addRow(QStringLiteral("FEC 类型:"), comboFEC_);

    connect(comboModem_, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &ModemSettingsWidget::onModemChanged);
    connect(comboFEC_, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, [this](int) { emit changed(); persist(); });
    connect(checkAutoDetect_, &QCheckBox::toggled,
            this, [this](bool) { emit changed(); persist(); });

    QVBoxLayout* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(modemGroup);
}

void ModemSettingsWidget::onModemChanged(int index)
{
    Q_UNUSED(index);
    // 通知设置面板更新"高级设置"中应按当前调制方式显示哪些项
    emit modemTypeChanged(modemTypeValue());
    emit changed();
    persist();   // 立即落盘（savePanelSettings 无调用点，不能依赖它）
}

int ModemSettingsWidget::modemTypeValue() const
{
    return comboModem_->currentData().toInt();
}

void ModemSettingsWidget::setAutoDetectVisible(bool visible)
{
    if (!layout_ || autoDetectRow_ < 0) return;
    setFormRowVisible(layout_, checkAutoDetect_, visible);
}

void ModemSettingsWidget::persist()
{
    if (surfaceKey_.isEmpty()) return;
    saveSettings(surfaceKey_);
}

void ModemSettingsWidget::setMftskTier(int v)   { mftskTier_ = v;   emit changed(); persist(); }
void ModemSettingsWidget::setPskOrder(int v)    { pskOrder_ = v;    emit changed(); persist(); }
void ModemSettingsWidget::setPskQamRate(int v)  { pskQamRate_ = v;  emit changed(); persist(); }
void ModemSettingsWidget::setMarkFreq(int v)    { markFreq_ = v;    emit changed(); persist(); }
void ModemSettingsWidget::setSpaceFreq(int v)   { spaceFreq_ = v;   emit changed(); persist(); }
void ModemSettingsWidget::setBaudRate(int v)    { baudRate_ = v;    emit changed(); persist(); }

PipelineConfig ModemSettingsWidget::getPipelineConfig() const
{
    PipelineConfig cfg;
    cfg.modemType = static_cast<ModemType>(comboModem_->currentData().toInt());
    cfg.fecType = static_cast<FecType>(comboFEC_->currentData().toInt());
    cfg.autoDetectModem = checkAutoDetect_->isChecked();
    cfg.mftskTier = static_cast<MftskTier>(mftskTier_);
    cfg.pskOrder = pskOrder_;
    cfg.pskQamRate = static_cast<PskQamRate>(pskQamRate_);
    cfg.markFreq = static_cast<float>(markFreq_);
    cfg.spaceFreq = static_cast<float>(spaceFreq_);
    cfg.baudRate = baudRate_;
    return cfg;
}

void ModemSettingsWidget::setFromConfig(const PipelineConfig& cfg)
{
    comboModem_->setCurrentIndex(
        comboModem_->findData(static_cast<int>(cfg.modemType)));
    comboFEC_->setCurrentIndex(
        comboFEC_->findData(static_cast<int>(cfg.fecType)));
    checkAutoDetect_->setChecked(cfg.autoDetectModem);
    mftskTier_ = static_cast<int>(cfg.mftskTier);
    pskOrder_ = cfg.pskOrder;
    pskQamRate_ = static_cast<int>(cfg.pskQamRate);
    markFreq_ = static_cast<int>(cfg.markFreq);
    spaceFreq_ = static_cast<int>(cfg.spaceFreq);
    baudRate_ = cfg.baudRate;
}

// ---- Configuration file (explicit INI, per-surface keys) ----
//
// 使用统一的配置文件（见 app_config.h）：
//   %APPDATA%/AudioT/audioT.ini   (Linux: XDG config dir)
// Sections: [modem:<surfaceKey>] with keys modemType / fecType /
// autoDetectModem / mftskTier / pskOrder / pskQamRate / markFreq /
// spaceFreq / baudRate。文件缺失或不可读时回退到内置默认值。

void ModemSettingsWidget::loadSettings(const QString& surfaceKey)
{
    surfaceKey_ = surfaceKey;
    QSettings settings(audiot::appConfigFilePath(), QSettings::IniFormat);
    const QString section = QStringLiteral("modem:") + surfaceKey;
    settings.beginGroup(section);
    const int modemType = settings.value(QStringLiteral("modemType"),
                                         static_cast<int>(ModemType::AFSK)).toInt();
    const int fecType = settings.value(QStringLiteral("fecType"),
                                       static_cast<int>(FecType::HAMMING)).toInt();
    mftskTier_ = settings.value(QStringLiteral("mftskTier"),
                                static_cast<int>(MftskTier::BALANCED)).toInt();
    pskOrder_ = settings.value(QStringLiteral("pskOrder"), 2).toInt();
    pskQamRate_ = settings.value(QStringLiteral("pskQamRate"),
                                 static_cast<int>(PskQamRate::BALANCED)).toInt();
    markFreq_ = settings.value(QStringLiteral("markFreq"), 1200).toInt();
    spaceFreq_ = settings.value(QStringLiteral("spaceFreq"), 2200).toInt();
    baudRate_ = settings.value(QStringLiteral("baudRate"), 1200).toInt();
    const bool autoDetect = settings.value(QStringLiteral("autoDetectModem"), true).toBool();
    settings.endGroup();

    // 配置损坏时回退到默认（保持控件初值）
    if (comboModem_->findData(modemType) < 0 || comboFEC_->findData(fecType) < 0) {
        return;
    }
    comboModem_->setCurrentIndex(comboModem_->findData(modemType));
    comboFEC_->setCurrentIndex(comboFEC_->findData(fecType));
    checkAutoDetect_->setChecked(autoDetect);
}

void ModemSettingsWidget::saveSettings(const QString& surfaceKey) const
{
    QSettings settings(audiot::appConfigFilePath(), QSettings::IniFormat);
    const QString section = QStringLiteral("modem:") + surfaceKey;
    settings.beginGroup(section);
    settings.setValue(QStringLiteral("modemType"), comboModem_->currentData().toInt());
    settings.setValue(QStringLiteral("fecType"), comboFEC_->currentData().toInt());
    settings.setValue(QStringLiteral("autoDetectModem"), checkAutoDetect_->isChecked());
    settings.setValue(QStringLiteral("mftskTier"), mftskTier_);
    settings.setValue(QStringLiteral("pskOrder"), pskOrder_);
    settings.setValue(QStringLiteral("pskQamRate"), pskQamRate_);
    settings.setValue(QStringLiteral("markFreq"), markFreq_);
    settings.setValue(QStringLiteral("spaceFreq"), spaceFreq_);
    settings.setValue(QStringLiteral("baudRate"), baudRate_);
    settings.endGroup();
    settings.sync();
}

} // namespace audiot
