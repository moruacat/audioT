// sstv_panel.h - SSTV 选项卡面板（图片 -> 音频 / 音频 -> 图片）
#ifndef AUDIOT_SSTV_PANEL_H
#define AUDIOT_SSTV_PANEL_H

#include <QWidget>
#include <QImage>

#include <cstdint>
#include <vector>

#include "core/sstv/sstv_protocol.h"

class QComboBox;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QProgressBar;

namespace audiot {

class SstvPanel : public QWidget {
    Q_OBJECT
public:
    explicit SstvPanel(QWidget* parent = nullptr);

    // 供主窗口取当前待发送帧：把已选图片缩放到模式尺寸并转成 RGB888。
    // 失败时返回 false 并写入 err（中文，可直接显示）。
    bool takeFrame(std::vector<uint8_t>& rgb, const sstv::ModeSpec*& spec, QString& err);

    // 当前选择的模式（下拉框）
    const sstv::ModeSpec* selectedMode() const;
    // 接收时的模式约束：勾选"自动识别"时返回 nullptr（按 VIS 自动判断）
    const sstv::ModeSpec* forcedMode() const;

    void setTxActive(bool active);
    void setTxProgress(int percent);
    void setTxStatus(const QString& text);

    void setRxActive(bool active);
    void setRxProgress(int percent);
    void setRxStatus(const QString& text);

    void showDecodedImage(const QImage& img, const QString& errorOrWarning);

    // 接收结果保存目录：与电台模式一致，优先系统下载目录
    static QString receivedImageDir();

signals:
    void transmitRequested();
    void stopTransmitRequested();
    void exportWavRequested();
    void decodeWavRequested(const QString& path);
    void startMicRequested();
    void stopMicRequested();

private:
    void setupUI();
    void onPickImage();
    void onPickWav();
    void onSaveImage();
    void onClearReceived();
    void updateModeInfo();
    void updatePreview();

    QComboBox* comboMode_;
    QLabel* labelModeInfo_;
    QCheckBox* checkAuto_;

    // 发送
    QImage txImage_;
    QString txFileName_;
    QLabel* labelTxInfo_;
    QLabel* labelTxPreview_;
    QPushButton* btnPickImage_;
    QPushButton* btnTx_;
    QPushButton* btnStopTx_;
    QPushButton* btnExportWav_;
    QProgressBar* progressTx_;

    // 接收
    QLineEdit* wavPath_;
    QPushButton* btnPickWav_;
    QPushButton* btnDecode_;
    QPushButton* btnMic_;
    QPushButton* btnStopMic_;
    QProgressBar* progressRx_;
    QLabel* labelRxPreview_;
    QPushButton* btnSaveImage_;
    QPushButton* btnClearRx_;
    QImage rxImage_;

    QLabel* labelStatus_;
};

} // namespace audiot
#endif // AUDIOT_SSTV_PANEL_H
