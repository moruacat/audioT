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

namespace audiot {

RadioPanel::RadioPanel(QWidget* parent)
    : QWidget(parent)
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
    const int bubbleH = qMax(rect.height(), fm.height()) + kBubblePadY;
    msgLabel->setFixedSize(bubbleW - kBubblePadX, bubbleH - kBubblePadY);

    QFrame* bubble = new QFrame;
    bubble->setStyleSheet(QStringLiteral(
        "QFrame { background:%1; border-radius:8px; }").arg(bgColor));
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


void RadioPanel::setListening(bool on)
{
    btnListen_->setEnabled(!on);
    btnStop_->setEnabled(on);
    labelStatus_->setText(on ? QStringLiteral("监听中...") : QStringLiteral("未监听"));
}

void RadioPanel::onSendClicked()
{
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
