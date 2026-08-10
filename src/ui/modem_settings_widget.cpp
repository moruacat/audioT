// modem_settings_widget.cpp - Reusable modem/pipeline settings widget
#include "modem_settings_widget.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QMessageBox>

namespace audiot {

ModemSettingsWidget::ModemSettingsWidget(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
}

void ModemSettingsWidget::setupUI()
{
    QGroupBox* modemGroup = new QGroupBox(QStringLiteral("调制参数"), this);
    QFormLayout* layout = new QFormLayout(modemGroup);
    layout->setContentsMargins(6, 6, 6, 6);

    comboModem_ = new QComboBox(modemGroup);
    comboModem_->addItem(QStringLiteral("AFSK"), static_cast<int>(ModemType::AFSK));
    comboModem_->addItem(QStringLiteral("MFSK (4-FSK)"), static_cast<int>(ModemType::MFSK));
    layout->addRow(QStringLiteral("调制方式:"), comboModem_);

    comboMftskTier_ = new QComboBox(modemGroup);
    comboMftskTier_->addItem(QStringLiteral("稳健 (50/s)"), static_cast<int>(MftskTier::ROBUST));
    comboMftskTier_->addItem(QStringLiteral("均衡 (100/s)"), static_cast<int>(MftskTier::BALANCED));
    comboMftskTier_->addItem(QStringLiteral("高速 (150/s)"), static_cast<int>(MftskTier::SPORT));
    layout->addRow(QStringLiteral("MFSK 档位:"), comboMftskTier_);

    checkAutoDetect_ = new QCheckBox(QStringLiteral("自动检测调制方式 (AFSK / M-FSK)"), modemGroup);
    layout->addRow(QString(), checkAutoDetect_);

    comboFEC_ = new QComboBox(modemGroup);
    comboFEC_->addItem(QStringLiteral("None"), 0);
    comboFEC_->addItem(QStringLiteral("Hamming"), 1);
    comboFEC_->addItem(QStringLiteral("Reed-Solomon"), 2);
    comboFEC_->setCurrentIndex(1);  // Hamming default
    layout->addRow(QStringLiteral("FEC 类型:"), comboFEC_);

    // 高速档提示
    connect(comboMftskTier_, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &ModemSettingsWidget::onMftskTierChanged);
    // 任何变更都通知外部
    connect(comboModem_, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &ModemSettingsWidget::changed);
    connect(comboFEC_, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &ModemSettingsWidget::changed);
    connect(checkAutoDetect_, &QCheckBox::toggled, this, &ModemSettingsWidget::changed);

    QVBoxLayout* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(modemGroup);
}

void ModemSettingsWidget::onMftskTierChanged(int index)
{
    if (index >= 0 &&
        comboMftskTier_->itemData(index).toInt() == static_cast<int>(MftskTier::SPORT)) {
        QMessageBox::warning(this, QStringLiteral("高速档提示"),
            QStringLiteral("高速档 (M-FSK 150 符号/s) 吞吐更高，但对噪声/干扰更敏感，\n"
                           "在较差链路上更容易出错。建议仅在信道质量良好时使用。"));
    }
    emit changed();
}

PipelineConfig ModemSettingsWidget::getPipelineConfig() const
{
    PipelineConfig cfg;
    cfg.modemType = static_cast<ModemType>(comboModem_->currentData().toInt());
    cfg.fecType = static_cast<FecType>(comboFEC_->currentData().toInt());
    cfg.mftskTier = static_cast<MftskTier>(comboMftskTier_->currentData().toInt());
    cfg.autoDetectModem = checkAutoDetect_->isChecked();
    return cfg;
}

void ModemSettingsWidget::setFromConfig(const PipelineConfig& cfg)
{
    comboModem_->setCurrentIndex(
        comboModem_->findData(static_cast<int>(cfg.modemType)));
    comboFEC_->setCurrentIndex(
        comboFEC_->findData(static_cast<int>(cfg.fecType)));
    comboMftskTier_->setCurrentIndex(
        comboMftskTier_->findData(static_cast<int>(cfg.mftskTier)));
    checkAutoDetect_->setChecked(cfg.autoDetectModem);
}

// ---- Configuration file (explicit INI, per-surface keys) ----
//
// Values are stored in a plain INI file (not the registry) so the whole
// configuration is one visible, portable file:
//   %APPDATA%/AudioT/audioT.ini   (or XDG config dir on Linux)
// Sections: [modem:<surfaceKey>] with keys modemType / fecType /
// mftskTier / autoDetectModem. If the file is missing or unreadable the
// widget falls back to built-in defaults (fresh initialization).

static QString configFilePath()
{
    const QString dir = QStandardPaths::writableLocation(
        QStandardPaths::AppConfigLocation);
    if (dir.isEmpty()) {
        return QStringLiteral("audioT.ini");
    }
    QDir().mkpath(dir);
    return dir + QStringLiteral("/audioT.ini");
}

void ModemSettingsWidget::loadSettings(const QString& surfaceKey)
{
    QSettings settings(configFilePath(), QSettings::IniFormat);
    // loadSettings is called with a fresh widget (defaults already set);
    // any missing/unreadable value simply keeps the default. That is the
    // "config file failed -> initialize" behaviour.
    const QString section = QStringLiteral("modem:") + surfaceKey;
    settings.beginGroup(section);
    const int modemType = settings.value(QStringLiteral("modemType"),
                                         static_cast<int>(ModemType::AFSK)).toInt();
    const int fecType = settings.value(QStringLiteral("fecType"),
                                       static_cast<int>(FecType::HAMMING)).toInt();
    const int tier = settings.value(QStringLiteral("mftskTier"),
                                    static_cast<int>(MftskTier::BALANCED)).toInt();
    const bool autoDetect = settings.value(QStringLiteral("autoDetectModem"), true).toBool();
    settings.endGroup();

    // Sanity-guard against a corrupt config: fall back to defaults.
    if (comboModem_->findData(modemType) < 0 ||
        comboFEC_->findData(fecType) < 0 ||
        comboMftskTier_->findData(tier) < 0) {
        return;  // keep widget defaults
    }

    comboModem_->setCurrentIndex(comboModem_->findData(modemType));
    comboFEC_->setCurrentIndex(comboFEC_->findData(fecType));
    comboMftskTier_->setCurrentIndex(comboMftskTier_->findData(tier));
    checkAutoDetect_->setChecked(autoDetect);
}

void ModemSettingsWidget::saveSettings(const QString& surfaceKey) const
{
    QSettings settings(configFilePath(), QSettings::IniFormat);
    const QString section = QStringLiteral("modem:") + surfaceKey;
    settings.beginGroup(section);
    settings.setValue(QStringLiteral("modemType"), comboModem_->currentData().toInt());
    settings.setValue(QStringLiteral("fecType"), comboFEC_->currentData().toInt());
    settings.setValue(QStringLiteral("mftskTier"), comboMftskTier_->currentData().toInt());
    settings.setValue(QStringLiteral("autoDetectModem"), checkAutoDetect_->isChecked());
    settings.endGroup();
    settings.sync();
}

} // namespace audiot
