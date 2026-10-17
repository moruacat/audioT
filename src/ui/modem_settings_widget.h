// modem_settings_widget.h - Reusable modem/pipeline settings widget
// 每个收发选项卡（发射 / 接收 / 电台）各持有一份：只包含"调制方式 /
// 自动检测 / FEC"三项收发相关选项。
//
// 调制相关的高级参数（Mark/Space/波特率、MFSK 档位、PSK 阶数、
// PSK/QAM 档位）**不再在每个选项卡里重复摆放**，而是由右侧常驻设置面板
// 的"高级设置"依据当前选项卡的调制方式统一显示与编辑（见
// SettingsDialog::setActiveModem）。本类只保存这些数值并负责 per-surface
// 持久化，避免三个选项卡各摆一组一模一样的控件。
#ifndef AUDIOT_MODEM_SETTINGS_WIDGET_H
#define AUDIOT_MODEM_SETTINGS_WIDGET_H

#include <QWidget>
#include <QComboBox>
#include <QCheckBox>

#include "core/pipeline/pipeline_config.h"

class QFormLayout;

namespace audiot {

class ModemSettingsWidget : public QWidget {
    Q_OBJECT
public:
    explicit ModemSettingsWidget(QWidget* parent = nullptr);

    // Build a PipelineConfig from the current widget state（含全部调制相关
    // 字段，高级参数取自本类保存的数值）
    PipelineConfig getPipelineConfig() const;

    // Persist this widget's values under a per-surface key
    // ("tx" / "rx" / "radio") so each tab keeps its own settings.
    void loadSettings(const QString& surfaceKey);
    void saveSettings(const QString& surfaceKey) const;

    // Apply the values of an existing config into the widget
    void setFromConfig(const PipelineConfig& cfg);

    // 发射端不需要"自动检测调制方式"（那是接收端能力），
    // 由 TxPanel 调用以隐藏该选项。
    void setAutoDetectVisible(bool visible);

    // 当前调制方式（ModemType 枚举值），供设置面板决定高级项显隐
    int modemTypeValue() const;

    // 高级参数读写（供右侧设置面板代理显示/编辑）
    int mftskTier() const { return mftskTier_; }
    int pskOrder() const { return pskOrder_; }
    int pskQamRate() const { return pskQamRate_; }
    int markFreq() const { return markFreq_; }
    int spaceFreq() const { return spaceFreq_; }
    int baudRate() const { return baudRate_; }

    void setMftskTier(int v);
    void setPskOrder(int v);
    void setPskQamRate(int v);
    void setMarkFreq(int v);
    void setSpaceFreq(int v);
    void setBaudRate(int v);

signals:
    // Emitted whenever the user changes any control.
    void changed();
    // 调制方式变化（设置面板据此显隐对应的"高级设置"项）
    void modemTypeChanged(int modemType);

private slots:
    void onModemChanged(int index);

private:
    void setupUI();
    // 立即把当前值写回配置文件（高级参数由设置面板代理编辑，
    // 改动要即时落盘，不能等到退出）
    void persist();

    QFormLayout* layout_;
    int autoDetectRow_;     // QFormLayout 行号（自动检测）

    QComboBox* comboModem_;
    QCheckBox* checkAutoDetect_;
    QComboBox* comboFEC_;

    QString surfaceKey_;    // 最近一次 load/save 用的分组键（"tx"/"rx"/"radio"）

    // 高级参数（本类不显示其控件，见文件头注释）
    int mftskTier_;
    int pskOrder_;
    int pskQamRate_;
    int markFreq_;
    int spaceFreq_;
    int baudRate_;
};

} // namespace audiot
#endif // AUDIOT_MODEM_SETTINGS_WIDGET_H
