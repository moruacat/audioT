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

namespace audiot {

class RxPanel : public QWidget {
    Q_OBJECT
public:
    explicit RxPanel(QWidget* parent = nullptr);

signals:
    // 解析指定 WAV 文件（后台线程进行）；空路径表示麦克风实时模式
    void startReceiving(const QString& wavPath);
    // 中止正在进行的解析/监听
    void stopReceiving();

public slots:
    void onDataReceived(const QString& text);
    void onFileReceived(const QString& filename);
    void onReceiveStatus(const QString& status);
    // percent >= 0 显示确定进度；-1 显示不确定（忙碌）状态
    void onReceiveProgress(int percent);
    void clearReceived();

private slots:
    void onBrowseWav();
    void onStartClicked();
    void onStopClicked();
    void onModeChanged();

private:
    void setupUI();

    // Input source（WAV 文件 / 麦克风实时 双模式）
    QRadioButton* radioWav_;
    QRadioButton* radioMic_;
    QLineEdit* wavFilePath_;
    QPushButton* btnBrowse_;

    // Output
    QTextEdit* receivedText_;
    QLineEdit* savePath_;

    // Control
    QPushButton* btnStart_;
    QPushButton* btnStop_;
    QProgressBar* progress_;
    QLabel* labelStatus_;
};

} // namespace audiot
#endif // AUDIOT_RX_PANEL_H
