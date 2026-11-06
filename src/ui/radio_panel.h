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
    // 文件消息气泡：文件名是可点击文本（点击用系统默认程序打开）；
    // 气泡其余区域可点击查看详情。
    //   displayName - 显示的文件名
    //   filePath    - 本地文件路径；为空表示**未能保存**，此时不提供打开入口
    //   note        - 次级说明行（文件大小 / 保存状态 / 失败原因）
    void appendFileMessage(int kind, const QString& displayName,
                           const QString& filePath, const QString& note);
    void setListening(bool on);
    // 发送状态：发送中禁用输入与调制选项、发送键变"停止发送"、显示进度条
    void setSending(bool on);
    // 只更新发送阶段文字（如"编码中..."），不动其它发送状态
    void setSendPhase(const QString& text);
    // 发射进度：0-100；负值表示不确定（准备音阶段）
    void setSendProgress(int percent);

private slots:
    void onSendClicked();
    void onBrowseFile();

protected:
    // 保持提示文字层与列表视口对齐（空列表提示用）
    void resizeEvent(QResizeEvent* event) override;
    // 首次切到电台页时也要摆正提示层（resizeEvent 未必先于首次绘制）
    void showEvent(QShowEvent* event) override;

private:
    void setupUI();
    // 把空列表提示层铺满聊天区视口（居中显示、不超出窗口）
    void relayoutHint();
    static QString timestamp();
    // 气泡对话框样式：己方绿色右对齐、对方灰色左对齐、系统居中灰字；
    // 时间戳与气泡旁置组成一体
    QWidget* createBubble(int kind, const QString& text, const QString& ts);
    // 文件气泡：可点击的文件名（系统默认程序打开）+ 次级说明行（大小/状态），
    // 点击气泡其余区域查看详情
    QWidget* createFileBubble(int kind, const QString& name, const QString& path,
                              const QString& note, const QString& ts);
    // 用系统默认程序打开文件；失败时给出明确提示（无默认打开程序等）
    void openLocalFile(const QString& path, const QString& displayName);

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
    // 本次发送是否为文件：决定"停止发送"的策略（文本立即停，
    // 文件需确认并显示已发送/剩余量）
    bool sendingIsFile_;
    int lastSendPercent_;               // 最近一次发射进度（0-100，-1 为不确定）
    ModemSettingsWidget* modemSettings_;

    // 当前调制方式 + FEC 的文字摘要，用于发送文件前的确认提示
    QString modemSummary() const;
};

} // namespace audiot
#endif // AUDIOT_RADIO_PANEL_H
