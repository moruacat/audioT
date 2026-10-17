// radio_panel.cpp - 电台模式面板实现
#include "radio_panel.h"
#include "modem_settings_widget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QTime>
#include <QResizeEvent>
#include <QScrollBar>
#include <QFontMetrics>
#include <QFrame>
#include <QAbstractItemView>
#include <QMessageBox>
#include <QDir>
#include <QUrl>

namespace audiot {

RadioPanel::RadioPanel(QWidget* parent)
    : QWidget(parent)
    , sending_(false)
    , modemSettings_(nullptr)
{
    setupUI();
}

void RadioPanel::loadPanelSettings()
{
    if (modemSettings_) {
        modemSettings_->loadSettings(QStringLiteral("radio"));
    }
}

void RadioPanel::savePanelSettings() const
{
    if (modemSettings_) {
        modemSettings_->saveSettings(QStringLiteral("radio"));
    }
}

namespace {
// 气泡对话框样式参数（类似聊天软件）
const int kBubbleMaxWidth = 460;   // 气泡最大宽度（长文本换行）
const int kBubblePadX = 12;        // 气泡左右内边距（共）
const int kBubblePadY = 8;         // 气泡上下内边距（共）
const int kBubbleRadius = 8;       // 气泡圆角半径（QSS border-radius）
}

void RadioPanel::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // --- 气泡式消息列表 ---
    chatLog_ = new QListWidget(this);
    chatLog_->setSelectionMode(QAbstractItemView::NoSelection);
    chatLog_->setFocusPolicy(Qt::NoFocus);
    chatLog_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    chatLog_->setStyleSheet(QStringLiteral(
        "QListWidget { border: none; }"
        "QListWidget::item { border: none; padding: 3px 0px; }"));
    mainLayout->addWidget(chatLog_, 1);

    // 空列表提示层（首条消息后隐藏）
    hintLabel_ = new QLabel(QStringLiteral(
        "电台消息将显示在此（文本 / 文件 / 系统信息）..."), chatLog_);
    hintLabel_->setAlignment(Qt::AlignCenter);
    hintLabel_->setStyleSheet(QStringLiteral("color:#9a9a9a;"));
    hintLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);

    // --- 输入行 ---
    QHBoxLayout* inputLayout = new QHBoxLayout();
    inputLine_ = new QLineEdit(this);
    inputLine_->setPlaceholderText(QStringLiteral("输入要发送的消息，回车或点发送"));
    btnFile_ = new QPushButton(QStringLiteral("发送文件..."), this);
    btnSend_ = new QPushButton(QStringLiteral("发送"), this);
    inputLayout->addWidget(inputLine_, 1);
    inputLayout->addWidget(btnFile_);
    inputLayout->addWidget(btnSend_);
    mainLayout->addLayout(inputLayout);

    // --- 控制行 ---
    QHBoxLayout* ctlLayout = new QHBoxLayout();
    btnListen_ = new QPushButton(QStringLiteral("开始监听"), this);
    btnStop_ = new QPushButton(QStringLiteral("停止监听"), this);
    btnStop_->setEnabled(false);
    labelStatus_ = new QLabel(QStringLiteral("未监听"), this);
    ctlLayout->addWidget(btnListen_);
    ctlLayout->addWidget(btnStop_);
    ctlLayout->addWidget(labelStatus_);
    ctlLayout->addStretch();
    mainLayout->addLayout(ctlLayout);

    // --- 发射进度条（仅发送中显示）---
    sendProgress_ = new QProgressBar(this);
    sendProgress_->setRange(0, 100);
    sendProgress_->setValue(0);
    sendProgress_->setVisible(false);
    mainLayout->addWidget(sendProgress_);

    // --- Per-surface modem settings ---
    modemSettings_ = new ModemSettingsWidget(this);
    mainLayout->addWidget(modemSettings_);

    // Connections
    connect(inputLine_, &QLineEdit::returnPressed, this, &RadioPanel::onSendClicked);
    connect(btnSend_, &QPushButton::clicked, this, &RadioPanel::onSendClicked);
    connect(btnFile_, &QPushButton::clicked, this, &RadioPanel::onBrowseFile);
    connect(btnListen_, &QPushButton::clicked, this, [this]() {
        emit startListening();
    });
    connect(btnStop_, &QPushButton::clicked, this, [this]() {
        emit stopListening();
    });
}

void RadioPanel::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (hintLabel_) {
        hintLabel_->setGeometry(chatLog_->viewport()->geometry());
    }
}

QString RadioPanel::timestamp()
{
    return QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
}

void RadioPanel::appendMessage(int kind, const QString& text)
{
    hintLabel_->hide();
    const QString ts = timestamp();

    QListWidgetItem* item = new QListWidgetItem(chatLog_);
    QWidget* bubble = createBubble(kind, text, ts);
    item->setSizeHint(bubble->sizeHint());
    chatLog_->setItemWidget(item, bubble);
    chatLog_->scrollToBottom();
}

QWidget* RadioPanel::createBubble(int kind, const QString& text, const QString& ts)
{
    if (kind == MSG_INFO) {
        // 系统信息：居中灰色小字，无气泡
        QLabel* label = new QLabel(QStringLiteral("[%1] %2").arg(ts, text));
        label->setAlignment(Qt::AlignCenter);
        label->setStyleSheet(QStringLiteral("color:#9a9a9a; font-size:12px;"));
        label->setWordWrap(true);
        return label;
    }

    // 气泡 + 时间戳（己方绿色右对齐 / 对方灰色左对齐）
    const bool mine = (kind == MSG_SEND);
    const QString bgColor = mine ? QStringLiteral("#95EC69")
                                 : QStringLiteral("#F0F0F0");
    const QString textColor = mine ? QStringLiteral("#1a1a1a")
                                   : QStringLiteral("#1a1a1a");

    QWidget* outer = new QWidget;
    QVBoxLayout* vLayout = new QVBoxLayout(outer);
    vLayout->setContentsMargins(0, 0, 0, 0);
    vLayout->setSpacing(2);

    QLabel* tsLabel = new QLabel(ts);
    tsLabel->setStyleSheet(QStringLiteral("color:#9a9a9a; font-size:11px;"));

    QLabel* msgLabel = new QLabel(text);
    msgLabel->setWordWrap(true);
    msgLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    msgLabel->setStyleSheet(QStringLiteral("color:%1;").arg(textColor));

    // 按文本实际宽度计算气泡尺寸（长文本限宽换行）
    QFontMetrics fm(msgLabel->font());
    const int labelW = kBubbleMaxWidth - kBubblePadX;
    const QRect rect = fm.boundingRect(
        QRect(0, 0, labelW, 4000), Qt::TextWordWrap, text);
    const int bubbleW = qMin(rect.width() + kBubblePadX, kBubbleMaxWidth);
    // 用 QLabel::heightForWidth 取换行后的真实高度：boundingRect 只按单行盒高
    // 估算、忽略行距，直接拿去 setFixedSize 会把最后一行底部裁掉（表现为气泡
    // 下半缺一条，缺失高度约等于圆角半径）；再补一点圆角内侧留白。
    int labelH = msgLabel->heightForWidth(bubbleW - kBubblePadX);
    if (labelH <= 0) {
        labelH = qMax(rect.height(), fm.height());
    }
    const int bubbleH = labelH + kBubblePadY + kBubbleRadius;
    msgLabel->setFixedSize(bubbleW - kBubblePadX, labelH);

    QFrame* bubble = new QFrame;
    bubble->setStyleSheet(QStringLiteral(
        "QFrame { background:%1; border-radius:%2px; }")
        .arg(bgColor).arg(kBubbleRadius));
    QHBoxLayout* bLayout = new QHBoxLayout(bubble);
    bLayout->setContentsMargins(kBubblePadX / 2, kBubblePadY / 2,
                                kBubblePadX / 2, kBubblePadY / 2);
    bLayout->addWidget(msgLabel);
    bubble->setFixedSize(bubbleW, bubbleH);

    if (mine) {
        // 己方：整体靠右，时间戳在气泡上方右对齐
        tsLabel->setAlignment(Qt::AlignRight);
        vLayout->addWidget(tsLabel);
        vLayout->addWidget(bubble, 0, Qt::AlignRight);
    } else {
        // 对方：整体靠左
        tsLabel->setAlignment(Qt::AlignLeft);
        vLayout->addWidget(tsLabel);
        vLayout->addWidget(bubble, 0, Qt::AlignLeft);
    }
    return outer;
}

void RadioPanel::appendFileMessage(int kind, const QString& displayName,
                                   const QString& filePath)
{
    hintLabel_->hide();
    QListWidgetItem* item = new QListWidgetItem(chatLog_);
    QWidget* bubble = createFileBubble(kind, displayName, filePath, timestamp());
    item->setSizeHint(bubble->sizeHint());
    chatLog_->setItemWidget(item, bubble);
    chatLog_->scrollToBottom();
}

QWidget* RadioPanel::createFileBubble(int kind, const QString& name,
                                      const QString& path, const QString& ts)
{
    // 文件消息气泡：文件名是可点击链接，点击用系统默认程序打开（而非纯文本）
    const bool mine = (kind == MSG_SEND);
    const QString bgColor = mine ? QStringLiteral("#95EC69")
                                 : QStringLiteral("#F0F0F0");

    QWidget* outer = new QWidget;
    QVBoxLayout* vLayout = new QVBoxLayout(outer);
    vLayout->setContentsMargins(0, 0, 0, 0);
    vLayout->setSpacing(2);

    QLabel* tsLabel = new QLabel(ts);
    tsLabel->setStyleSheet(QStringLiteral("color:#9a9a9a; font-size:11px;"));

    QLabel* msgLabel = new QLabel;
    msgLabel->setTextFormat(Qt::RichText);
    const QString url = QUrl::fromLocalFile(path).toString();
    msgLabel->setText(QStringLiteral("📎 <a href=\"%1\">%2</a>")
                          .arg(url, name.toHtmlEscaped()));
    msgLabel->setOpenExternalLinks(true);   // 点击交给系统默认程序打开
    msgLabel->setToolTip(path);
    msgLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    msgLabel->setStyleSheet(QStringLiteral("color:#1a1a1a;"));

    QFrame* bubble = new QFrame;
    bubble->setStyleSheet(QStringLiteral(
        "QFrame { background:%1; border-radius:%2px; }")
        .arg(bgColor).arg(kBubbleRadius));
    QHBoxLayout* bLayout = new QHBoxLayout(bubble);
    bLayout->setContentsMargins(kBubblePadX / 2, kBubblePadY / 2,
                                kBubblePadX / 2, kBubblePadY / 2);
    bLayout->addWidget(msgLabel);
    bubble->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);

    if (mine) {
        tsLabel->setAlignment(Qt::AlignRight);
        vLayout->addWidget(tsLabel);
        vLayout->addWidget(bubble, 0, Qt::AlignRight);
    } else {
        tsLabel->setAlignment(Qt::AlignLeft);
        vLayout->addWidget(tsLabel);
        vLayout->addWidget(bubble, 0, Qt::AlignLeft);
    }
    return outer;
}


void RadioPanel::setListening(bool on)
{
    btnListen_->setEnabled(!on);
    btnStop_->setEnabled(on);
    labelStatus_->setText(on ? QStringLiteral("监听中...") : QStringLiteral("未监听"));
}

void RadioPanel::setSending(bool on)
{
    sending_ = on;
    // 发送中禁止调节：输入框、选文件、调制参数全部变灰；发送键变"停止"
    inputLine_->setEnabled(!on);
    btnFile_->setEnabled(!on);
    btnSend_->setText(on ? QStringLiteral("停止") : QStringLiteral("发送"));
    if (modemSettings_) modemSettings_->setEnabled(!on);
    sendProgress_->setVisible(on);

    if (on) {
        sendProgress_->setRange(0, 100);
        sendProgress_->setValue(0);
        labelStatus_->setText(QStringLiteral("发送中..."));
    } else {
        // 恢复监听状态显示（btnStop_ 可用即表示正在监听）
        labelStatus_->setText(btnStop_->isEnabled() ? QStringLiteral("监听中...")
                                                    : QStringLiteral("未监听"));
    }
}

void RadioPanel::setSendProgress(int percent)
{
    sendProgress_->setVisible(true);
    if (percent < 0) {
        // 不确定进度（准备音阶段）：忙碌动画
        sendProgress_->setRange(0, 0);
    } else {
        sendProgress_->setRange(0, 100);
        sendProgress_->setValue(percent);
    }
}

void RadioPanel::onSendClicked()
{
    if (sending_) {
        // 发送中：按钮已变为"停止"；二次确认后中止
        const QMessageBox::StandardButton ret = QMessageBox::question(
            this, QStringLiteral("确认停止"),
            QStringLiteral("当前正在发送，确定要中止吗？\n"
                           "（对方会收到一条“发送已停止”提示）"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (ret == QMessageBox::Yes) {
            emit stopSendRequested();
        }
        return;
    }

    const QString text = inputLine_->text().trimmed();
    if (text.isEmpty()) {
        return;
    }
    emit sendRequested(text);
    inputLine_->clear();
}

void RadioPanel::onBrowseFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择要发送的文件"));
    if (path.isEmpty()) {
        return;
    }
    // 选中即发送（聊天软件风格）
    emit sendRequested(path);
}

} // namespace audiot
