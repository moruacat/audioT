// rx_panel.cpp - Receive panel widget implementation
#include "rx_panel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QFile>
#include <QTextStream>

namespace audiot {

RxPanel::RxPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
}

void RxPanel::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // --- Input source group ---
    QGroupBox* sourceGroup = new QGroupBox(QStringLiteral("输入源"), this);
    QVBoxLayout* sourceLayout = new QVBoxLayout(sourceGroup);

    // WAV file row
    QHBoxLayout* wavLayout = new QHBoxLayout();
    wavFilePath_ = new QLineEdit(this);
    wavFilePath_->setPlaceholderText(QStringLiteral("选择 WAV 文件..."));
    wavFilePath_->setReadOnly(true);
    btnBrowse_ = new QPushButton(QStringLiteral("浏览..."), this);
    wavLayout->addWidget(wavFilePath_);
    wavLayout->addWidget(btnBrowse_);
    sourceLayout->addLayout(wavLayout);

    mainLayout->addWidget(sourceGroup);

    // --- Received data group ---
    QGroupBox* recvGroup = new QGroupBox(QStringLiteral("接收数据"), this);
    QVBoxLayout* recvLayout = new QVBoxLayout(recvGroup);

    receivedText_ = new QTextEdit(this);
    receivedText_->setReadOnly(true);
    receivedText_->setPlaceholderText(QStringLiteral("接收到的数据将显示在此..."));
    recvLayout->addWidget(receivedText_);

    // Save row
    QHBoxLayout* saveLayout = new QHBoxLayout();
    savePath_ = new QLineEdit(this);
    savePath_->setPlaceholderText(QStringLiteral("保存解码结果到文件..."));
    QPushButton* btnSave = new QPushButton(QStringLiteral("保存"), this);
    saveLayout->addWidget(savePath_);
    saveLayout->addWidget(btnSave);
    recvLayout->addLayout(saveLayout);

    mainLayout->addWidget(recvGroup);

    // --- Control ---
    QHBoxLayout* controlLayout = new QHBoxLayout();
    btnStart_ = new QPushButton(QStringLiteral("开始解析"), this);
    btnStop_ = new QPushButton(QStringLiteral("停止"), this);
    btnStop_->setEnabled(false);
    controlLayout->addWidget(btnStart_);
    controlLayout->addWidget(btnStop_);
    controlLayout->addStretch();
    mainLayout->addLayout(controlLayout);

    progress_ = new QProgressBar(this);
    progress_->setRange(0, 100);
    progress_->setValue(0);
    mainLayout->addWidget(progress_);

    labelStatus_ = new QLabel(QStringLiteral("就绪"), this);
    mainLayout->addWidget(labelStatus_);

    mainLayout->addStretch();

    // Connections
    connect(btnBrowse_, &QPushButton::clicked, this, &RxPanel::onBrowseWav);
    connect(btnStart_, &QPushButton::clicked, this, &RxPanel::onStartClicked);
    connect(btnStop_, &QPushButton::clicked, this, &RxPanel::onStopClicked);
    connect(btnSave, &QPushButton::clicked, this, [this]() {
        QString path = savePath_->text().trimmed();
        if (path.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请输入保存路径"));
            return;
        }
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("无法打开文件: %1").arg(path));
            return;
        }
        QTextStream out(&file);
        out << receivedText_->toPlainText();
        file.close();
        labelStatus_->setText(QStringLiteral("已保存到: %1").arg(path));
    });
}

void RxPanel::onBrowseWav()
{
    QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择 WAV 文件"),
                                                 QString(), QStringLiteral("WAV 文件 (*.wav)"));
    if (!path.isEmpty()) {
        wavFilePath_->setText(path);
    }
}

void RxPanel::onStartClicked()
{
    const QString path = wavFilePath_->text().trimmed();
    if (path.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请选择 WAV 文件"));
        return;
    }
    // 解析在后台线程进行（MainWindow 负责），完成后经
    // onReceiveStatus 复位按钮，这里统一在启动时禁用控件
    emit startReceiving(path);
    labelStatus_->setText(QStringLiteral("解析中..."));

    btnStart_->setEnabled(false);
    btnStop_->setEnabled(true);
}

void RxPanel::clearReceived()
{
    receivedText_->clear();
}

void RxPanel::onStopClicked()
{
    emit stopReceiving();
    btnStart_->setEnabled(true);
    btnStop_->setEnabled(false);
    labelStatus_->setText(QStringLiteral("已停止"));
}

void RxPanel::onDataReceived(const QString& text)
{
    receivedText_->append(text);
}

void RxPanel::onFileReceived(const QString& filename)
{
    labelStatus_->setText(QStringLiteral("收到文件: %1").arg(filename));
}

void RxPanel::onReceiveProgress(int percent)
{
    if (percent < 0) {
        // 不确定进度：忙碌动画（麦克风实时接收）
        progress_->setRange(0, 0);
        return;
    }
    progress_->setRange(0, 100);
    progress_->setValue(percent);
}

void RxPanel::onReceiveStatus(const QString& status)
{
    labelStatus_->setText(status);
    if (status.contains(QStringLiteral("完成")) || status.contains(QStringLiteral("停止"))
        || status.contains(QStringLiteral("错误"))) {
        btnStart_->setEnabled(true);
        btnStop_->setEnabled(false);
    }
}

} // namespace audiot
