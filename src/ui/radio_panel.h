// radio_panel.h - 电台模式面板（双向通信，类似聊天软件）
#ifndef AUDIOT_RADIO_PANEL_H
#define AUDIOT_RADIO_PANEL_H

#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QListWidget>
#include <QLabel>

class QResizeEvent;

namespace audiot {

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

signals:
    void startListening();
    void stopListening();
    // textOrFile：输入框文本（文本消息）或文件路径（文件消息）
    void sendRequested(const QString& textOrFile);

public slots:
    void appendMessage(int kind, const QString& text);
    void setListening(bool on);

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

    QListWidget* chatLog_;
    QLabel* hintLabel_;                 // 空列表提示（首条消息后隐藏）
    QLineEdit* inputLine_;
    QPushButton* btnFile_;
    QPushButton* btnSend_;
    QPushButton* btnListen_;
    QPushButton* btnStop_;
    QLabel* labelStatus_;
};

} // namespace audiot
#endif // AUDIOT_RADIO_PANEL_H
