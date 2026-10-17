// tx_panel.h - Transmit panel widget
#ifndef AUDIOT_TX_PANEL_H
#define AUDIOT_TX_PANEL_H

#include <QWidget>
#include <QRadioButton>
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QProgressBar>
#include <QLabel>
#include <QGroupBox>

namespace audiot {

class ModemSettingsWidget;

class TxPanel : public QWidget {
    Q_OBJECT
public:
    explicit TxPanel(QWidget* parent = nullptr);

    // Per-surface modem/pipeline settings (modulation, M-FSK tier,
    // auto-detect, FEC). Each panel owns its own configuration.
    ModemSettingsWidget* modemSettings() const { return modemSettings_; }
    void loadPanelSettings();
    void savePanelSettings() const;

signals:
    void startTransmission(const QString& textOrFile);
    void stopTransmission();
    void exportWavRequested(const QString& textOrFile, const QString& path);

public slots:
    void onTransmissionProgress(int percent);
    void onTransmissionStatus(const QString& status);

private slots:
    void onModeChanged();
    void onBrowseInput();
    void onBrowseOutput();
    void onExportClicked();
    void onStartClicked();
    void onStopClicked();

private:
    void setupUI();

    // Input mode
    QRadioButton* radioText_;
    QRadioButton* radioFile_;
    QRadioButton* radioImage_;   // 图片/视频模式（数字帧传输）
    QTextEdit* textInput_;
    QLineEdit* filePathEdit_;
    QPushButton* btnBrowse_;

    // Configuration (per-surface modem settings widget)
    ModemSettingsWidget* modemSettings_;

    // Output
    QLineEdit* outputWavPath_;
    QPushButton* btnBrowseOut_;
    QPushButton* btnExportWav_;

    // Control
    QPushButton* btnStart_;
    QPushButton* btnStop_;
    QLabel* labelWarning_;       // 红色警告：发射时巨大噪音
    QProgressBar* progress_;
    QLabel* labelStatus_;
};

} // namespace audiot
#endif // AUDIOT_TX_PANEL_H
