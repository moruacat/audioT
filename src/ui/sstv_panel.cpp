// sstv_panel.cpp - SSTV 选项卡面板实现
//
// 布局刻意保持紧凑（主窗口最小 700x500），并分成"发送图片 / 接收图片"两组，
// 与现有发射/接收面板的组织方式一致。样式全部沿用主题里既有的
// primary/danger 属性与悬浮反馈，不额外写样式表。
#include "sstv_panel.h"

#include <QComboBox>
#include <QCheckBox>
#include <QPixmap>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QProgressBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QDir>
#include <QStandardPaths>
#include <QFileInfo>
#include <QImageWriter>
#include <QDateTime>

#include <cstring>

namespace audiot {

namespace {
const int kPreviewW = 300;
const int kPreviewH = 200;
}

SstvPanel::SstvPanel(QWidget* parent)
    : QWidget(parent)
    , comboMode_(nullptr)
    , labelModeInfo_(nullptr)
    , checkAuto_(nullptr)
    , labelTxInfo_(nullptr)
    , labelTxPreview_(nullptr)
    , btnPickImage_(nullptr)
    , btnTx_(nullptr)
    , btnStopTx_(nullptr)
    , btnExportWav_(nullptr)
    , progressTx_(nullptr)
    , wavPath_(nullptr)
    , btnPickWav_(nullptr)
    , btnDecode_(nullptr)
    , btnMic_(nullptr)
    , btnStopMic_(nullptr)
    , progressRx_(nullptr)
    , labelRxPreview_(nullptr)
    , btnSaveImage_(nullptr)
    , btnClearRx_(nullptr)
    , labelStatus_(nullptr)
{
    setupUI();
    updateModeInfo();
}

void SstvPanel::setupUI()
{
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setSpacing(6);

    // ---- 合规提示：本项目范围仅限个人学习/实验 ----
    QLabel* note = new QLabel(QStringLiteral(
        "仅限个人学习与实验（本地扬声器→麦克风、音频线或 WAV 文件）。"
        "SSTV 属业余无线电制式，未取得执照请勿用于无线电发射。"), this);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color:#9a9a9a; font-size:11px;"));
    root->addWidget(note);

    // ---- 模式选择 ----
    QHBoxLayout* modeRow = new QHBoxLayout();
    QLabel* modeLabel = new QLabel(QStringLiteral("模式:"), this);
    comboMode_ = new QComboBox(this);
    for (int i = 0; i < sstv::modeCount(); ++i) {
        comboMode_->addItem(QString::fromUtf8(sstv::modeSpecByIndex(i).name));
    }
    labelModeInfo_ = new QLabel(this);
    labelModeInfo_->setStyleSheet(QStringLiteral("color:#9a9a9a; font-size:11px;"));
    modeRow->addWidget(modeLabel);
    modeRow->addWidget(comboMode_);
    modeRow->addSpacing(8);
    modeRow->addWidget(labelModeInfo_);
    modeRow->addStretch();
    root->addLayout(modeRow);

    // ================= 发送 =================
    QGroupBox* txGroup = new QGroupBox(QStringLiteral("发送图片"), this);
    QVBoxLayout* txLayout = new QVBoxLayout(txGroup);

    QHBoxLayout* pickRow = new QHBoxLayout();
    btnPickImage_ = new QPushButton(QStringLiteral("选择图片…"), txGroup);
    labelTxInfo_ = new QLabel(QStringLiteral("未选择图片"), txGroup);
    labelTxInfo_->setStyleSheet(QStringLiteral("color:#9a9a9a; font-size:11px;"));
    pickRow->addWidget(btnPickImage_);
    pickRow->addWidget(labelTxInfo_, 1);
    txLayout->addLayout(pickRow);

    labelTxPreview_ = new QLabel(txGroup);
    labelTxPreview_->setMinimumSize(160, 60);
    labelTxPreview_->setMaximumHeight(kPreviewH);
    labelTxPreview_->setAlignment(Qt::AlignCenter);
    labelTxPreview_->setStyleSheet(QStringLiteral(
        "QLabel { background:#111; border:1px solid #3a3a3d; border-radius:4px;"
        " color:#6a6a6a; font-size:11px; }"));
    labelTxPreview_->setText(QStringLiteral("图片预览"));
    txLayout->addWidget(labelTxPreview_);

    QHBoxLayout* txBtnRow = new QHBoxLayout();
    btnTx_ = new QPushButton(QStringLiteral("开始发射"), txGroup);
    btnTx_->setProperty("primary", true);
    btnStopTx_ = new QPushButton(QStringLiteral("停止发射"), txGroup);
    btnStopTx_->setProperty("danger", true);
    btnStopTx_->setEnabled(false);
    btnExportWav_ = new QPushButton(QStringLiteral("导出 WAV"), txGroup);
    txBtnRow->addWidget(btnTx_);
    txBtnRow->addWidget(btnStopTx_);
    txBtnRow->addWidget(btnExportWav_);
    txBtnRow->addStretch();
    txLayout->addLayout(txBtnRow);

    progressTx_ = new QProgressBar(txGroup);
    progressTx_->setRange(0, 100);
    progressTx_->setValue(0);
    txLayout->addWidget(progressTx_);

    root->addWidget(txGroup);

    // ================= 接收 =================
    QGroupBox* rxGroup = new QGroupBox(QStringLiteral("接收图片"), this);
    QVBoxLayout* rxLayout = new QVBoxLayout(rxGroup);

    QHBoxLayout* wavRow = new QHBoxLayout();
    wavPath_ = new QLineEdit(rxGroup);
    wavPath_->setPlaceholderText(QStringLiteral("选择含 SSTV 录音的 WAV 文件…"));
    wavPath_->setReadOnly(true);
    btnPickWav_ = new QPushButton(QStringLiteral("选择文件…"), rxGroup);
    btnDecode_ = new QPushButton(QStringLiteral("解析 WAV"), rxGroup);
    btnDecode_->setProperty("primary", true);
    wavRow->addWidget(wavPath_, 1);
    wavRow->addWidget(btnPickWav_);
    wavRow->addWidget(btnDecode_);
    rxLayout->addLayout(wavRow);

    QHBoxLayout* micRow = new QHBoxLayout();
    checkAuto_ = new QCheckBox(QStringLiteral("自动识别模式（按 VIS）"), rxGroup);
    checkAuto_->setChecked(true);
    checkAuto_->setToolTip(QStringLiteral(
        "勾选时按录音里的 VIS 头部自动判断模式；取消勾选则强制使用上方所选模式。"
        "注意：帧头里不含 FEC/速率等信息，SSTV 的兼容性只取决于模式是否一致。"));
    btnMic_ = new QPushButton(QStringLiteral("开始麦克风录制"), rxGroup);
    btnStopMic_ = new QPushButton(QStringLiteral("停止录制"), rxGroup);
    btnStopMic_->setProperty("danger", true);
    btnStopMic_->setEnabled(false);
    QLabel* micHint = new QLabel(QStringLiteral(
        "整段录制，静音约 1 秒自动结束；最长 120 秒"), rxGroup);
    micHint->setStyleSheet(QStringLiteral("color:#9a9a9a; font-size:11px;"));
    micRow->addWidget(btnMic_);
    micRow->addWidget(btnStopMic_);
    micRow->addWidget(micHint, 1);
    rxLayout->addLayout(micRow);

    progressRx_ = new QProgressBar(rxGroup);
    progressRx_->setRange(0, 100);
    progressRx_->setValue(0);
    rxLayout->addWidget(progressRx_);

    labelRxPreview_ = new QLabel(rxGroup);
    labelRxPreview_->setMinimumSize(160, 60);
    labelRxPreview_->setMaximumHeight(kPreviewH);
    labelRxPreview_->setAlignment(Qt::AlignCenter);
    labelRxPreview_->setStyleSheet(QStringLiteral(
        "QLabel { background:#111; border:1px solid #3a3a3d; border-radius:4px;"
        " color:#6a6a6a; font-size:11px; }"));
    labelRxPreview_->setText(QStringLiteral("解码结果将显示在此"));
    rxLayout->addWidget(labelRxPreview_);

    QHBoxLayout* rxBtnRow = new QHBoxLayout();
    btnSaveImage_ = new QPushButton(QStringLiteral("保存图片"), rxGroup);
    btnSaveImage_->setEnabled(false);
    btnClearRx_ = new QPushButton(QStringLiteral("清空结果"), rxGroup);
    btnClearRx_->setEnabled(false);
    rxBtnRow->addWidget(btnSaveImage_);
    rxBtnRow->addWidget(btnClearRx_);
    rxBtnRow->addStretch();
    rxLayout->addLayout(rxBtnRow);

    root->addWidget(rxGroup);

    // ---- 状态 ----
    labelStatus_ = new QLabel(QStringLiteral("就绪"), this);
    labelStatus_->setWordWrap(true);
    root->addWidget(labelStatus_);
    root->addStretch();

    // ---- 连接 ----
    connect(comboMode_, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, [this](int) { updateModeInfo(); });
    connect(btnPickImage_, &QPushButton::clicked, this, &SstvPanel::onPickImage);
    connect(btnPickWav_, &QPushButton::clicked, this, &SstvPanel::onPickWav);
    connect(btnSaveImage_, &QPushButton::clicked, this, &SstvPanel::onSaveImage);
    connect(btnClearRx_, &QPushButton::clicked, this, &SstvPanel::onClearReceived);
    connect(btnTx_, &QPushButton::clicked, this, [this]() {
        emit transmitRequested();
    });
    connect(btnStopTx_, &QPushButton::clicked, this, [this]() {
        emit stopTransmitRequested();
    });
    connect(btnExportWav_, &QPushButton::clicked, this, [this]() {
        emit exportWavRequested();
    });
    connect(btnDecode_, &QPushButton::clicked, this, [this]() {
        const QString path = wavPath_->text().trimmed();
        if (path.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("提示"),
                                 QStringLiteral("请先选择一个 WAV 文件"));
            return;
        }
        emit decodeWavRequested(path);
    });
    connect(btnMic_, &QPushButton::clicked, this, [this]() {
        emit startMicRequested();
    });
    connect(btnStopMic_, &QPushButton::clicked, this, [this]() {
        emit stopMicRequested();
    });
}

void SstvPanel::updateModeInfo()
{
    const sstv::ModeSpec* spec = selectedMode();
    if (!spec) {
        return;
    }
    const double sec = sstv::frameDurationMs(*spec) / 1000.0;
    labelModeInfo_->setText(QStringLiteral("%1×%2 · 整帧约 %3 秒 · VIS %4")
        .arg(spec->width)
        .arg(spec->height)
        .arg(QString::number(sec, 'f', 1))
        .arg(static_cast<int>(spec->visCode)));
}

const sstv::ModeSpec* SstvPanel::selectedMode() const
{
    if (!comboMode_) {
        return nullptr;
    }
    return &sstv::modeSpecByIndex(comboMode_->currentIndex());
}

const sstv::ModeSpec* SstvPanel::forcedMode() const
{
    if (checkAuto_ && checkAuto_->isChecked()) {
        return nullptr;   // 自动：按录音里的 VIS 判断
    }
    return selectedMode();
}

void SstvPanel::onPickImage()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择要发送的图片"), QString(),
        QStringLiteral("图片 (*.png *.jpg *.jpeg *.bmp *.gif *.webp)"));
    if (path.isEmpty()) {
        return;   // 取消：不改动当前状态（评审要求）
    }
    QImage img(path);
    if (img.isNull()) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("无法读取这张图片: %1").arg(path));
        return;
    }
    txImage_ = img;
    txFileName_ = QFileInfo(path).fileName();
    const QFileInfo fi(path);
    labelTxInfo_->setText(QStringLiteral("%1 · %2×%3 · %4 KB")
        .arg(txFileName_)
        .arg(img.width())
        .arg(img.height())
        .arg(fi.size() / 1024));
    updatePreview();
    labelStatus_->setText(QStringLiteral("已选择图片: %1").arg(txFileName_));
}

void SstvPanel::updatePreview()
{
    if (txImage_.isNull()) {
        labelTxPreview_->setPixmap(QPixmap());
        labelTxPreview_->setText(QStringLiteral("图片预览"));
        return;
    }
    labelTxPreview_->setText(QString());
    labelTxPreview_->setPixmap(QPixmap::fromImage(txImage_.scaled(
        labelTxPreview_->width(), labelTxPreview_->height(),
        Qt::KeepAspectRatio, Qt::SmoothTransformation)));
}

void SstvPanel::onPickWav()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择 SSTV 录音"), QString(),
        QStringLiteral("WAV 音频 (*.wav)"));
    if (!path.isEmpty()) {
        wavPath_->setText(path);
    }
}

bool SstvPanel::takeFrame(std::vector<uint8_t>& rgb, const sstv::ModeSpec*& spec,
                          QString& err)
{
    if (txImage_.isNull()) {
        err = QStringLiteral("请先选择一张要发送的图片");
        return false;
    }
    spec = selectedMode();
    if (!spec) {
        err = QStringLiteral("未选择 SSTV 模式");
        return false;
    }

    // 缩放到模式尺寸。用 KeepAspectRatioByExpanding + 居中裁切，
    // 而不是直接拉伸——拉伸会让圆和文字变形，SSTV 的分辨率本来就不高，
    // 变形会比裁掉一点边缘更难看。
    QImage scaled = txImage_.scaled(spec->width, spec->height,
                                    Qt::KeepAspectRatioByExpanding,
                                    Qt::SmoothTransformation);
    const int ox = (scaled.width() - spec->width) / 2;
    const int oy = (scaled.height() - spec->height) / 2;
    QImage cropped = scaled.copy(ox, oy, spec->width, spec->height);
    QImage conv = cropped.convertToFormat(QImage::Format_RGB888);

    rgb.resize(static_cast<size_t>(spec->width) * spec->height * 3);
    for (int y = 0; y < spec->height; ++y) {
        std::memcpy(rgb.data() + static_cast<size_t>(y) * spec->width * 3,
                    conv.constScanLine(y),
                    static_cast<size_t>(spec->width) * 3);
    }
    return true;
}

void SstvPanel::setTxActive(bool active)
{
    btnTx_->setEnabled(!active);
    btnStopTx_->setEnabled(active);
    btnPickImage_->setEnabled(!active);
    btnExportWav_->setEnabled(!active);
    comboMode_->setEnabled(!active);
    if (!active) {
        progressTx_->setRange(0, 100);
    }
}

void SstvPanel::setTxProgress(int percent)
{
    if (percent < 0) {
        progressTx_->setRange(0, 0);   // 忙碌
        return;
    }
    progressTx_->setRange(0, 100);
    progressTx_->setValue(percent);
}

void SstvPanel::setTxStatus(const QString& text)
{
    labelStatus_->setText(text);
}

void SstvPanel::setRxActive(bool active)
{
    btnDecode_->setEnabled(!active);
    btnMic_->setEnabled(!active);
    btnStopMic_->setEnabled(active);
    btnPickWav_->setEnabled(!active);
    comboMode_->setEnabled(!active);
    if (!active) {
        progressRx_->setRange(0, 100);
    }
}

void SstvPanel::setRxProgress(int percent)
{
    if (percent < 0) {
        progressRx_->setRange(0, 0);
        return;
    }
    progressRx_->setRange(0, 100);
    progressRx_->setValue(percent);
}

void SstvPanel::setRxStatus(const QString& text)
{
    labelStatus_->setText(text);
}

void SstvPanel::showDecodedImage(const QImage& img, const QString& errorOrWarning)
{
    if (img.isNull()) {
        if (!errorOrWarning.isEmpty()) {
            labelStatus_->setText(errorOrWarning);
        }
        return;
    }
    rxImage_ = img;
    labelRxPreview_->setText(QString());
    labelRxPreview_->setPixmap(QPixmap::fromImage(img.scaled(
        labelRxPreview_->width(), labelRxPreview_->height(),
        Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    btnSaveImage_->setEnabled(true);
    btnClearRx_->setEnabled(true);
    labelStatus_->setText(errorOrWarning.isEmpty()
        ? QStringLiteral("解码完成（%1×%2）").arg(img.width()).arg(img.height())
        : errorOrWarning);
}

void SstvPanel::onSaveImage()
{
    if (rxImage_.isNull()) {
        return;
    }
    const QString dirPath = receivedImageDir();
    QDir().mkpath(dirPath);
    QString suggested = dirPath + QStringLiteral("/SSTV_") +
                        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")) +
                        QStringLiteral(".png");
    QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("保存接收到的图片"), suggested,
        QStringLiteral("PNG 图片 (*.png);;JPEG 图片 (*.jpg)"));
    if (path.isEmpty()) {
        return;
    }
    if (!path.contains(QLatin1Char('.'))) {
        path += QStringLiteral(".png");
    }
    if (!rxImage_.save(path)) {
        QMessageBox::warning(this, QStringLiteral("保存失败"),
                             QStringLiteral("无法写入文件: %1").arg(path));
        return;
    }
    labelStatus_->setText(QStringLiteral("已保存到: %1").arg(path));
}

void SstvPanel::onClearReceived()
{
    // 只清显示，不动磁盘上已经保存的文件
    rxImage_ = QImage();
    labelRxPreview_->setPixmap(QPixmap());
    labelRxPreview_->setText(QStringLiteral("解码结果将显示在此"));
    btnSaveImage_->setEnabled(false);
    btnClearRx_->setEnabled(false);
    labelStatus_->setText(QStringLiteral("已清空结果"));
}

QString SstvPanel::receivedImageDir()
{
    const QString downloads = QStandardPaths::writableLocation(
        QStandardPaths::DownloadLocation);
    if (!downloads.isEmpty() && QDir(downloads).exists()) {
        return downloads;
    }
    return QDir::homePath() + QStringLiteral("/AudioT");
}

} // namespace audiot
