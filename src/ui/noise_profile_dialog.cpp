// noise_profile_dialog.cpp - Noise profile import dialog implementation
#include "noise_profile_dialog.h"

#include "wav_reader.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QDialogButtonBox>
#include <QGroupBox>

#include <cmath>

namespace audiot {

NoiseProfileDialog::NoiseProfileDialog(QWidget* parent)
    : QDialog(parent)
    , wavPath_(nullptr)
    , btnBrowse_(nullptr)
    , btnImport_(nullptr)
    , btnSave_(nullptr)
    , infoView_(nullptr)
{
    setWindowTitle(QStringLiteral("噪声轮廓"));
    setMinimumWidth(460);
    setupUI();
    refreshInfo();
}

void NoiseProfileDialog::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // File selection row
    QHBoxLayout* fileRow = new QHBoxLayout();
    wavPath_ = new QLineEdit(this);
    wavPath_->setPlaceholderText(QStringLiteral("选择纯噪声 WAV 录音..."));
    btnBrowse_ = new QPushButton(QStringLiteral("浏览..."), this);
    btnImport_ = new QPushButton(QStringLiteral("导入"), this);
    fileRow->addWidget(wavPath_, 1);
    fileRow->addWidget(btnBrowse_);
    fileRow->addWidget(btnImport_);
    mainLayout->addLayout(fileRow);

    // Profile info box
    QGroupBox* infoBox = new QGroupBox(QStringLiteral("轮廓信息"), this);
    QVBoxLayout* infoLayout = new QVBoxLayout(infoBox);
    infoView_ = new QPlainTextEdit(this);
    infoView_->setReadOnly(true);
    infoView_->setMinimumHeight(140);
    infoLayout->addWidget(infoView_);
    mainLayout->addWidget(infoBox);

    // Bottom buttons
    QHBoxLayout* bottomRow = new QHBoxLayout();
    btnSave_ = new QPushButton(QStringLiteral("保存轮廓..."), this);
    btnSave_->setEnabled(false);
    QDialogButtonBox* buttons = new QDialogButtonBox(
        QDialogButtonBox::Close, this);
    bottomRow->addWidget(btnSave_);
    bottomRow->addStretch(1);
    bottomRow->addWidget(buttons);
    mainLayout->addLayout(bottomRow);

    connect(btnBrowse_, &QPushButton::clicked,
            this, &NoiseProfileDialog::onBrowseWav);
    connect(btnImport_, &QPushButton::clicked,
            this, &NoiseProfileDialog::onImport);
    connect(btnSave_, &QPushButton::clicked,
            this, &NoiseProfileDialog::onSaveProfile);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::clicked, this, &QDialog::accept);
}

const NoiseProfile& NoiseProfileDialog::profile() const
{
    return profile_;
}

void NoiseProfileDialog::onBrowseWav()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择噪声录音"), QString(),
        QStringLiteral("WAV 文件 (*.wav)"));
    if (!path.isEmpty()) {
        wavPath_->setText(path);
        onImport();
    }
}

void NoiseProfileDialog::onImport()
{
    const QString path = wavPath_->text().trimmed();
    if (path.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("噪声轮廓"),
                             QStringLiteral("请先选择一个 WAV 文件。"));
        return;
    }

    WavReader reader;
    if (!reader.open(path.toStdString())) {
        QMessageBox::critical(this, QStringLiteral("噪声轮廓"),
                              QStringLiteral("无法打开 WAV 文件。"));
        return;
    }

    const AudioFormat fmt = reader.format();
    const std::vector<float> samples = reader.readAll();
    reader.close();

    if (!profile_.createFromAudio(samples, fmt.sampleRate)) {
        QMessageBox::critical(this, QStringLiteral("噪声轮廓"),
                              QStringLiteral("WAV 文件无有效音频数据。"));
        return;
    }

    btnSave_->setEnabled(true);
    refreshInfo();
}

void NoiseProfileDialog::onSaveProfile()
{
    if (!profile_.isValid()) return;

    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("保存噪声轮廓"), QStringLiteral("noise.anp"),
        QStringLiteral("AudioT 噪声轮廓 (*.anp)"));
    if (path.isEmpty()) return;

    if (!profile_.saveToFile(path.toStdString())) {
        QMessageBox::critical(this, QStringLiteral("噪声轮廓"),
                              QStringLiteral("保存失败。"));
    }
}

void NoiseProfileDialog::refreshInfo()
{
    if (!profile_.isValid()) {
        infoView_->setPlainText(QStringLiteral("尚未加载噪声轮廓。"));
        return;
    }

    const double rmsDb = profile_.rmsLevel() > 0.0f
        ? 20.0 * std::log10(static_cast<double>(profile_.rmsLevel()))
        : -120.0;
    const double peakDb = profile_.peakLevel() > 0.0f
        ? 20.0 * std::log10(static_cast<double>(profile_.peakLevel()))
        : -120.0;

    QString text;
    text += QStringLiteral("采样率:     %1 Hz\n")
                .arg(profile_.sampleRate());
    text += QStringLiteral("样本数:     %1\n")
                .arg(static_cast<qulonglong>(profile_.sampleCount()));
    text += QStringLiteral("时长:       %1 秒\n")
                .arg(profile_.duration(), 0, 'f', 2);
    text += QStringLiteral("RMS 电平:   %1 (%2 dB)\n")
                .arg(profile_.rmsLevel(), 0, 'f', 4)
                .arg(rmsDb, 0, 'f', 1);
    text += QStringLiteral("峰值电平:   %1 (%2 dB)\n")
                .arg(profile_.peakLevel(), 0, 'f', 4)
                .arg(peakDb, 0, 'f', 1);
    text += QStringLiteral("频谱质心:   %1 Hz\n")
                .arg(profile_.spectralCentroid(), 0, 'f', 1);
    infoView_->setPlainText(text);
}

} // namespace audiot
