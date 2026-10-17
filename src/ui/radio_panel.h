// radio_panel.h - 电台模式面板（双向通信，类似聊天软件）
#ifndef AUDIOT_RADIO_PANEL_H
#define AUDIOT_RADIO_PANEL_H

#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QListWidget>
#include <QLabel>
#include <QProgressBar>

class QResizeEvent;

namespace audiot {

class ModemSettingsWidget;

// 电台模式：一台电脑即一个电台站。
// - 发送：输入文本或选择文件，经扬声器（输出设备）发射
// - 接收：麦克风（输入设备）持续监听，自动切段解码后显示在此
// 收发设备分离、同时进行（互相监测的两个电台站）。
class RadioPanel : public QWidget {
    Q_OBJECT
public:
    enum MsgKind {
        MSG_SEND = 0,   // 自己发送的消息
        MSG_RECV = 1,   // 解码收到的消息
        MSG_INFO = 2    // 系统信息（监听状态/发送失败/信号未解出等）
    };

    explicit RadioPanel(QWidget* parent = nullptr);

    // Per-surface modem/pipeline settings (modulation, M-FSK tier,
    // auto-detect, FEC). Each panel owns its own configuration.
    ModemSettingsWidget* modemSettings() const { return modemSettings_; }
    void loadPanelSettings();
    void savePanelSettings() const;

signals:
    void startListening();
    void stopListening();
    // textOrFile：输入框文本（文本消息）或文件路径（文件消息）
    void sendRequested(const QString& textOrFile);
    // 发送中点击"停止"（面板内已二次确认）——主窗口据此中止发射
    void stopSendRequested();

public slots:
    void appendMessage(int kind, const QString& text);
    // 文件消息：显示为可点击的文件名（点击用系统默认程序打开）
    void appendFileMessage(int kind, const QString& displayName, const QString& filePath);
    void setListening(bool on);
    // 发送状态：发送中禁用输入与调制选项、发送键变"停止"、显示进度条
    void setSending(bool on);
    // 发射进度：0-100；负值表示不确定（准备音阶段）
    void setSendProgress(int percent);

private slots:
    void onSendClicked();
    void onBrowseFile();

protected:
    // 保持提示文字层与列表视口对齐（空列表提示用）
    void resizeEvent(QResizeEvent* event) override;

private:
    void setupUI();
    static QString timestamp();
    // 气泡对话框样式：己方绿色右对齐、对方灰色左对齐、系统居中灰字
    QWidget* createBubble(int kind, const QString& text, const QString& ts);
    // 文件气泡：可点击的文件名链接（系统默认程序打开）
    QWidget* createFileBubble(int kind, const QString& name, const QString& path,
                              const QString& ts);

    QListWidget* chatLog_;
    QLabel* hintLabel_;                 // 空列表提示（首条消息后隐藏）
    QLineEdit* inputLine_;
    QPushButton* btnFile_;
    QPushButton* btnSend_;
    QPushButton* btnListen_;
    QPushButton* btnStop_;
    QLabel* labelStatus_;
    QProgressBar* sendProgress_;        // 发射进度（仅发送中显示）
    bool sending_;                      // 是否正在发送
    ModemSettingsWidget* modemSettings_;
};

} // namespace audiot
#endif // AUDIOT_RADIO_PANEL_H
