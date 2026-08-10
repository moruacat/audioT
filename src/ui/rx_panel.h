// rx_panel.h - Receive panel widget (WAV 文件直接解析)
#ifndef AUDIOT_RX_PANEL_H
#define AUDIOT_RX_PANEL_H

#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QLabel>
#include <QGroupBox>
#include <QProgressBar>
#include <QRadioButton>

class QStackedWidget;
class QImage;

namespace audiot {

class SSTVViewer;
class ModemSettingsWidget;

class RxPanel : public QWidget {
    Q_OBJECT
public:
    explicit RxPanel(QWidget* parent = nullptr);

    // Per-surface modem/pipeline settings (modulation, M-FSK tier,
    // auto-detect, FEC). Each panel owns its own configuration.
    ModemSettingsWidget* modemSettings() const { return modemSettings_; }
    void loadPanelSettings();
    void savePanelSettings() const;

signals:
    // 解析指定 WAV 文件（后台线程进行）；空路径表示麦克风实时模式
    void startReceiving(const QString& wavPath);
    // 中止正在进行的解析/监听
    void stopReceiving();
    // SSTV 图片解析：空路径表示麦克风实时监听，非空为 WAV 文件
    void startSstv(const QString& wavPath);
    void stopSstv();

public slots:
    void onDataReceived(const QString& text);
    void onFileReceived(const QString& filename);
    void onReceiveStatus(const QString& status);
    // percent >= 0 显示确定进度；-1 显示不确定（忙碌）状态
    void onReceiveProgress(int percent);
    void clearReceived();
    // SSTV 解码图片上屏（拷贝显示）
    void showSstvImage(const QImage& image);
    void clearSstv();

private slots:
    void onBrowseWav();
    void onStartClicked();
    void onStopClicked();
    void onModeChanged();

private:
    void setupUI();

    // Input source（WAV 文件 / 麦克风实时 / SSTV 图片 三模式）
    QRadioButton* radioWav_;
    QRadioButton* radioMic_;
    QRadioButton* radioSstv_;
    QLineEdit* wavFilePath_;
    QPushButton* btnBrowse_;

    // Per-surface modem settings widget
    ModemSettingsWidget* modemSettings_;

    // Output（文本页 / SSTV 图片页 切换显示）
    QStackedWidget* recvStack_;
    QTextEdit* receivedText_;
    QLineEdit* savePath_;
    SSTVViewer* sstvViewer_;
    QPushButton* btnSaveSstv_;

    // Control
    QPushButton* btnStart_;
    QPushButton* btnStop_;
    QProgressBar* progress_;
    QLabel* labelStatus_;
};

} // namespace audiot
#endif // AUDIOT_RX_PANEL_H
