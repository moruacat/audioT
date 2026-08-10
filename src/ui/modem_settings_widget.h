// modem_settings_widget.h - Reusable modem/pipeline settings widget
// Embedded in each TX/RX/Radio panel so every transmit/decode surface
// has its own modulation controls (modem, M-FSK tier, auto-detect, FEC).
#ifndef AUDIOT_MODEM_SETTINGS_WIDGET_H
#define AUDIOT_MODEM_SETTINGS_WIDGET_H

#include <QWidget>
#include <QComboBox>
#include <QCheckBox>

#include "core/pipeline/pipeline_config.h"

namespace audiot {

class ModemSettingsWidget : public QWidget {
    Q_OBJECT
public:
    explicit ModemSettingsWidget(QWidget* parent = nullptr);

    // Build a PipelineConfig from the current widget state. Only the
    // modem/FEC fields are set; audio device & advanced params are owned
    // by the main settings panel.
    PipelineConfig getPipelineConfig() const;

    // Persist this widget's values under a per-surface key
    // ("tx" / "rx" / "radio") so each tab keeps its own settings.
    void loadSettings(const QString& surfaceKey);
    void saveSettings(const QString& surfaceKey) const;

    // Apply the values of an existing config into the widgets (used to
    // mirror a loaded configuration).
    void setFromConfig(const PipelineConfig& cfg);

signals:
    // Emitted whenever the user changes any control.
    void changed();

private slots:
    void onMftskTierChanged(int index);

private:
    void setupUI();

    QComboBox* comboModem_;
    QComboBox* comboMftskTier_;
    QCheckBox* checkAutoDetect_;
    QComboBox* comboFEC_;
};

} // namespace audiot
#endif // AUDIOT_MODEM_SETTINGS_WIDGET_H
