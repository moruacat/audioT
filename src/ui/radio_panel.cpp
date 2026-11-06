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
#include <QFileInfo>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLayoutItem>
#include <QMouseEvent>

#include <functional>

namespace audiot {

namespace {
// 人类可读的文件大小（用于发送文件前的确认提示与聊天气泡）
QString formatFileSize(qint64 bytes)
{
    if (bytes < 1024) return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    return QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 2);
}

// 可点击的气泡框架：点击（非子控件链接区域）回调 onClick，
// 用于"点击消息气泡查看文件详情"。
class ClickableFrame : public QFrame {
public:
    std::function<void()> onClick;
protected:
    void mousePressEvent(QMouseEvent* e) override
    {
        if (onClick) onClick();
        QFrame::mousePressEvent(e);
    }
};
} // namespace

RadioPanel::RadioPanel(QWidget* parent)
    : QWidget(parent)
    , sending_(false)
    , sendingIsFile_(false)
    , lastSendPercent_(-1)
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
    // 必须允许换行：否则文本单行宽度可能超过视口，居中对齐看起来会像
    // 左上对齐并且溢出窗口
    hintLabel_->setWordWrap(true);
    hintLabel_->setStyleSheet(QStringLiteral("color:#9a9a9a; background:transparent;"));
    hintLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);
    relayoutHint();

    // --- 输入行 ---
    QHBoxLayout* inputLayout = new QHBoxLayout();
    inputLine_ = new QLineEdit(this);
    inputLine_->setPlaceholderText(QStringLiteral("输入要发送的消息，回车或点发送"));
    btnFile_ = new QPushButton(QStringLiteral("发送文件…"), this);
    btnSend_ = new QPushButton(QStringLiteral("发送"), this);
    btnSend_->setProperty("primary", true);   // 主色按钮（主题 QSS 识别）
    inputLayout->addWidget(inputLine_, 1);
    inputLayout->addWidget(btnFile_);
    inputLayout->addWidget(btnSend_);
    mainLayout->addLayout(inputLayout);

    // --- 控制行 ---
    QHBoxLayout* ctlLayout = new QHBoxLayout();
    btnListen_ = new QPushButton(QStringLiteral("开始监听"), this);
    btnListen_->setProperty("primary", true);  // 主色按钮（主题 QSS 识别）
    btnStop_ = new QPushButton(QStringLiteral("停止监听"), this);
    btnStop_->setProperty("danger", true);     // 停止类按钮用危险色文字
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

void RadioPanel::relayoutHint()
{
    if (!hintLabel_ || !chatLog_) return;
    // 提示层是聊天列表的子控件：坐标用 viewport 在其父坐标系中的矩形，
    // 这样提示正好铺满可视区（含边框偏移），不会超出窗口
    hintLabel_->setGeometry(chatLog_->viewport()->geometry());
    hintLabel_->raise();
}

void RadioPanel::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    relayoutHint();
}

void RadioPanel::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    // 首次切到电台页时 resizeEvent 可能尚未触发，这里补一次，
    // 避免提示层停留在默认几何（表现为左上对齐且溢出）
    relayoutHint();
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
    // 时间戳与气泡旁置、组成一个整体（不再让时间戳单独占一行）；
    // 窄窗口下整块仍可随气泡一起换行显示，可读性优先。
    QHBoxLayout* hLayout = new QHBoxLayout(outer);
    hLayout->setContentsMargins(0, 0, 0, 0);
    hLayout->setSpacing(6);

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
        // 己方：气泡靠右，时间戳紧邻气泡左侧
        hLayout->addStretch();
        hLayout->addWidget(tsLabel, 0, Qt::AlignBottom);
        hLayout->addWidget(bubble);
    } else {
        // 对方：气泡靠左，时间戳紧邻气泡右侧
        hLayout->addWidget(bubble);
        hLayout->addWidget(tsLabel, 0, Qt::AlignBottom);
        hLayout->addStretch();
    }
    return outer;
}

void RadioPanel::appendFileMessage(int kind, const QString& displayName,
                                   const QString& filePath, const QString& note)
{
    hintLabel_->hide();
    QListWidgetItem* item = new QListWidgetItem(chatLog_);
    QWidget* bubble = createFileBubble(kind, displayName, filePath, note, timestamp());
    item->setSizeHint(bubble->sizeHint());
    chatLog_->setItemWidget(item, bubble);
    chatLog_->scrollToBottom();
}

QWidget* RadioPanel::createFileBubble(int kind, const QString& name,
                                      const QString& path, const QString& note,
                                      const QString& ts)
{
    // 文件消息气泡：两个动作分开——
    //   点击文件名 -> 用系统默认程序打开本地文件
    //   点击气泡其余区域 -> 查看文件消息详情
    const bool mine = (kind == MSG_SEND);
    const QString bgColor = mine ? QStringLiteral("#95EC69")
                                 : QStringLiteral("#F0F0F0");
    const bool openable = !path.isEmpty();

    QWidget* outer = new QWidget;
    QHBoxLayout* hLayout = new QHBoxLayout(outer);
    hLayout->setContentsMargins(0, 0, 0, 0);
    hLayout->setSpacing(6);

    QLabel* tsLabel = new QLabel(ts);
    tsLabel->setStyleSheet(QStringLiteral("color:#9a9a9a; font-size:11px;"));

    ClickableFrame* bubble = new ClickableFrame;
    bubble->setStyleSheet(QStringLiteral(
        "QFrame { background:%1; border-radius:%2px; }")
        .arg(bgColor).arg(kBubbleRadius));
    QVBoxLayout* bLayout = new QVBoxLayout(bubble);
    bLayout->setContentsMargins(kBubblePadX / 2, kBubblePadY / 2,
                                kBubblePadX / 2, kBubblePadY / 2);
    bLayout->setSpacing(2);

    // 文件名：可点击文本（未能保存时不提供打开入口，避免点了打不开）
    QLabel* nameLabel = new QLabel(bubble);
    if (openable) {
        nameLabel->setTextFormat(Qt::RichText);
        nameLabel->setText(QStringLiteral("📎 <a href=\"open\">%1</a>")
                               .arg(name.toHtmlEscaped()));
        nameLabel->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
        nameLabel->setCursor(Qt::PointingHandCursor);
        nameLabel->setToolTip(QStringLiteral("点击打开：%1").arg(path));
        connect(nameLabel, &QLabel::linkActivated, this,
                [this, path, name]() { openLocalFile(path, name); });
    } else {
        nameLabel->setText(QStringLiteral("📎 %1").arg(name.toHtmlEscaped()));
        nameLabel->setTextFormat(Qt::PlainText);
        nameLabel->setWordWrap(true);
    }
    bLayout->addWidget(nameLabel);

    // 次级说明行：文件大小 / 保存状态 / 失败原因
    if (!note.isEmpty()) {
        QLabel* noteLabel = new QLabel(note, bubble);
        noteLabel->setWordWrap(true);
        noteLabel->setStyleSheet(QStringLiteral("color:#9a9a9a; font-size:11px;"));
        bLayout->addWidget(noteLabel);
    }

    bubble->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);

    // 点击气泡其余区域：查看文件消息详情
    const QString detailPath = path;
    const QString detailName = name;
    const QString detailNote = note;
    bubble->onClick = [this, detailName, detailPath, detailNote, mine]() {
        QString info = QStringLiteral("文件名：%1\n类型：%2")
                           .arg(detailName,
                                mine ? QStringLiteral("本机发送的文件")
                                     : QStringLiteral("接收到的文件"));
        if (!detailNote.isEmpty()) {
            info += QStringLiteral("\n%1").arg(detailNote);
        }
        if (!detailPath.isEmpty()) {
            info += QStringLiteral("\n本地位置：%1\n（点击气泡内的文件名即可打开）")
                        .arg(detailPath);
        } else {
            info += QStringLiteral("\n该文件未保存到本地，无法打开。");
        }
        QMessageBox::information(this, QStringLiteral("文件消息详情"), info);
    };

    if (mine) {
        hLayout->addStretch();
        hLayout->addWidget(tsLabel, 0, Qt::AlignBottom);
        hLayout->addWidget(bubble);
    } else {
        hLayout->addWidget(bubble);
        hLayout->addWidget(tsLabel, 0, Qt::AlignBottom);
        hLayout->addStretch();
    }
    return outer;
}

void RadioPanel::openLocalFile(const QString& path, const QString& displayName)
{
    if (path.isEmpty()) return;
    if (!QFileInfo::exists(path)) {
        QMessageBox::warning(this, QStringLiteral("无法打开文件"),
            QStringLiteral("文件不存在或已被移动：\n%1").arg(path));
        return;
    }
    // 用系统默认程序打开；系统没有可用的默认打开程序时明确提示，
    // 而不是静默失败
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
        QMessageBox::warning(this, QStringLiteral("无法打开文件"),
            QStringLiteral("系统没有可用的默认程序来打开“%1”。\n"
                           "可点击气泡查看文件位置后手动打开。").arg(displayName));
    }
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
    // 发送中禁止调节：输入框、选文件、调制参数全部变灰；发送键变"停止发送"
    inputLine_->setEnabled(!on);
    btnFile_->setEnabled(!on);
    btnSend_->setText(on ? QStringLiteral("停止发送") : QStringLiteral("发送"));
    if (modemSettings_) modemSettings_->setEnabled(!on);
    sendProgress_->setVisible(on);

    if (on) {
        sendProgress_->setRange(0, 100);
        sendProgress_->setValue(0);
        lastSendPercent_ = -1;
        labelStatus_->setText(QStringLiteral("发送中..."));
    } else {
        lastSendPercent_ = -1;
        // 注意：不在这里清 sendingIsFile_——它由 onBrowseFile/onSendClicked
        // 在发起发送时设置；若在此清掉，"发送中被打断后重新发送文件"会被
        // 误判为文本发送（停止时不再二次确认）
        // 恢复监听状态显示（btnStop_ 可用即表示正在监听）
        labelStatus_->setText(btnStop_->isEnabled() ? QStringLiteral("监听中...")
                                                    : QStringLiteral("未监听"));
    }
}

void RadioPanel::setSendPhase(const QString& text)
{
    labelStatus_->setText(text);
}

void RadioPanel::setSendProgress(int percent)
{
    lastSendPercent_ = percent;
    sendProgress_->setVisible(true);
    if (percent < 0) {
        // 不确定进度（准备音阶段）：忙碌动画
        sendProgress_->setRange(0, 0);
    } else {
        sendProgress_->setRange(0, 100);
        sendProgress_->setValue(percent);
    }
}

QString RadioPanel::modemSummary() const
{
    const PipelineConfig cfg = modemSettings_ ? modemSettings_->getPipelineConfig()
                                              : PipelineConfig();
    QString modem;
    switch (cfg.modemType) {
        case ModemType::AFSK: modem = QStringLiteral("AFSK"); break;
        case ModemType::MFSK: modem = QStringLiteral("MFSK (4-FSK)"); break;
        case ModemType::PSK:  modem = QStringLiteral("PSK (差分)"); break;
        case ModemType::QAM:  modem = QStringLiteral("16-QAM"); break;
        default:              modem = QStringLiteral("未知"); break;
    }
    QString fec;
    switch (cfg.fecType) {
        case FecType::NONE:          fec = QStringLiteral("None"); break;
        case FecType::HAMMING:       fec = QStringLiteral("Hamming"); break;
        case FecType::REED_SOLOMON:  fec = QStringLiteral("Reed-Solomon"); break;
        default:                     fec = QStringLiteral("未知"); break;
    }
    return QStringLiteral("%1 / FEC %2").arg(modem, fec);
}

void RadioPanel::onSendClicked()
{
    if (sending_) {
        // 停止发送的策略按风险区分：
        //  - 文本：立即中止，不做无谓确认
        //  - 文件：弹确认并给出已发送/剩余量（中断损失较大）
        if (sendingIsFile_) {
            const int sent = lastSendPercent_ < 0 ? 0 : lastSendPercent_;
            const QMessageBox::StandardButton ret = QMessageBox::question(
                this, QStringLiteral("确认停止发送"),
                QStringLiteral("正在发送文件：已发送约 %1%，剩余约 %2%。\n"
                               "确定要中止吗？（会尝试向对方发送“发送已停止”通知）")
                    .arg(sent).arg(100 - sent),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (ret != QMessageBox::Yes) {
                return;
            }
        }
        emit stopSendRequested();
        return;
    }

    const QString text = inputLine_->text().trimmed();
    if (text.isEmpty()) {
        return;
    }
    sendingIsFile_ = false;   // 文本消息：停止时不弹确认
    emit sendRequested(text);
    inputLine_->clear();
}

void RadioPanel::onBrowseFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择要发送的文件"));
    if (path.isEmpty()) {
        return;   // 用户取消：不改变输入框与发送状态
    }

    // 发送文件前先确认：文件名 / 大小 / 当前调制方式与 FEC，
    // 避免误选文件后立即开始传输
    const QFileInfo info(path);
    const QMessageBox::StandardButton ret = QMessageBox::question(
        this, QStringLiteral("发送文件确认"),
        QStringLiteral("文件名：%1\n大小：%2\n当前调制：%3\n\n确认通过音频链路发送该文件？")
            .arg(info.fileName(), formatFileSize(info.size()), modemSummary()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (ret != QMessageBox::Yes) {
        return;
    }

    sendingIsFile_ = true;   // 文件发送：停止时需确认并显示进度
    emit sendRequested(path);
}

} // namespace audiot
