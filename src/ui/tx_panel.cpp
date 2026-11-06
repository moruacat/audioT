// tx_panel.cpp - Transmit panel widget implementation
#include "tx_panel.h"
#include "modem_settings_widget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QButtonGroup>
#include <QFileDialog>
#include <QMessageBox>

namespace audiot {

TxPanel::TxPanel(QWidget* parent)
    : QWidget(parent)
    , modemSettings_(nullptr)
{
    setupUI();
}

void TxPanel::loadPanelSettings()
{
    if (modemSettings_) {
        modemSettings_->loadSettings(QStringLiteral("tx"));
    }
}

void TxPanel::savePanelSettings() const
{
    if (modemSettings_) {
        modemSettings_->saveSettings(QStringLiteral("tx"));
    }
}

void TxPanel::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // --- Input source group ---
    QGroupBox* inputGroup = new QGroupBox(QStringLiteral("输入源"), this);
    QVBoxLayout* inputLayout = new QVBoxLayout(inputGroup);

    QHBoxLayout* modeLayout = new QHBoxLayout();
    radioText_ = new QRadioButton(QStringLiteral("文本输入"), this);
    radioFile_ = new QRadioButton(QStringLiteral("文件输入"), this);
    radioImage_ = new QRadioButton(QStringLiteral("图片/视频"), this);
    radioText_->setChecked(true);

    QButtonGroup* modeGroup = new QButtonGroup(this);
    modeGroup->addButton(radioText_);
    modeGroup->addButton(radioFile_);
    modeGroup->addButton(radioImage_);
    modeLayout->addWidget(radioText_);
    modeLayout->addWidget(radioFile_);
    modeLayout->addWidget(radioImage_);
    modeLayout->addStretch();
    inputLayout->addLayout(modeLayout);

    // Text input
    textInput_ = new QTextEdit(this);
    textInput_->setPlaceholderText(QStringLiteral("请输入要发送的文本..."));
    textInput_->setMaximumHeight(120);
    inputLayout->addWidget(textInput_);

    // File input row (hidden by default)
    QHBoxLayout* fileLayout = new QHBoxLayout();
    filePathEdit_ = new QLineEdit(this);
    filePathEdit_->setPlaceholderText(QStringLiteral("选择输入文件..."));
    filePathEdit_->setReadOnly(true);
    btnBrowse_ = new QPushButton(QStringLiteral("选择文件…"), this);
    fileLayout->addWidget(filePathEdit_);
    fileLayout->addWidget(btnBrowse_);
    inputLayout->addLayout(fileLayout);

    // Hide file row initially
    filePathEdit_->setVisible(false);
    btnBrowse_->setVisible(false);

    mainLayout->addWidget(inputGroup);

    // --- Configuration (per-surface modem settings) ---
    modemSettings_ = new ModemSettingsWidget(this);
    // 发射端不需要"自动检测调制方式"（那是接收端能力）
    modemSettings_->setAutoDetectVisible(false);
    mainLayout->addWidget(modemSettings_);

    // --- Output group ---
    QGroupBox* outputGroup = new QGroupBox(QStringLiteral("输出 WAV 文件"), this);
    QHBoxLayout* outputLayout = new QHBoxLayout(outputGroup);
    outputWavPath_ = new QLineEdit(this);
    outputWavPath_->setPlaceholderText(QStringLiteral("输出 WAV 文件路径..."));
    btnBrowseOut_ = new QPushButton(QStringLiteral("选择保存位置…"), this);
    btnExportWav_ = new QPushButton(QStringLiteral("导出 WAV"), this);
    outputLayout->addWidget(outputWavPath_);
    outputLayout->addWidget(btnBrowseOut_);
    outputLayout->addWidget(btnExportWav_);
    mainLayout->addWidget(outputGroup);

    // --- Control ---
    QHBoxLayout* controlLayout = new QHBoxLayout();
    btnStart_ = new QPushButton(QStringLiteral("开始发射"), this);
    btnStart_->setProperty("primary", true);   // 主色按钮（主题 QSS 识别）
    btnStop_ = new QPushButton(QStringLiteral("停止发射"), this);
    btnStop_->setProperty("danger", true);     // 停止类按钮用危险色文字
    btnStop_->setEnabled(false);
    labelHint_ = new QLabel(this);
    setHint(QStringLiteral("发射时输出设备将播放调制音频，请先确认输出设备与音量"), false);
    controlLayout->addWidget(btnStart_);
    controlLayout->addWidget(btnStop_);
    controlLayout->addWidget(labelHint_);
    controlLayout->addStretch();
    mainLayout->addLayout(controlLayout);

    // Progress and status
    progress_ = new QProgressBar(this);
    progress_->setRange(0, 100);
    progress_->setValue(0);
    mainLayout->addWidget(progress_);

    labelStatus_ = new QLabel(QStringLiteral("就绪"), this);
    mainLayout->addWidget(labelStatus_);

    mainLayout->addStretch();

    // Connections
    connect(radioText_, &QRadioButton::toggled, this, &TxPanel::onModeChanged);
    connect(btnBrowse_, &QPushButton::clicked, this, &TxPanel::onBrowseInput);
    connect(btnBrowseOut_, &QPushButton::clicked, this, &TxPanel::onBrowseOutput);
    connect(btnExportWav_, &QPushButton::clicked, this, &TxPanel::onExportClicked);
    connect(btnStart_, &QPushButton::clicked, this, &TxPanel::onStartClicked);
    connect(btnStop_, &QPushButton::clicked, this, &TxPanel::onStopClicked);
}

void TxPanel::setHint(const QString& text, bool error)
{
    // 提示文字随发射阶段变化；只有真正出错时才用红色，
    // 避免常驻红字被用户忽略。
    labelHint_->setText(text);
    labelHint_->setStyleSheet(error
        ? QStringLiteral("color:#ff7b72; font-weight:bold;")
        : QStringLiteral("color:#9a9a9a;"));
}

void TxPanel::onModeChanged()
{
    const bool textMode = radioText_->isChecked();
    // 切换输入源只切换显示哪一块输入区，已输入文本/已选文件都原样保留，
    // 不会静默清空用户内容。
    textInput_->setVisible(textMode);
    filePathEdit_->setVisible(!textMode);
    btnBrowse_->setVisible(!textMode);
}

void TxPanel::onBrowseInput()
{
    // 图片/视频模式：过滤图片与视频文件；文本/文件模式：所有文件。
    // 实际的载荷类别由发送时按扩展名判定（见 MainWindow::encodeTxInput）。
    const QString filter = radioImage_->isChecked()
        ? QStringLiteral("图片/视频 (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.mp4 *.avi *.mkv *.mov)")
        : QStringLiteral("所有文件 (*)");
    QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择输入文件"),
                                                 QString(), filter);
    if (!path.isEmpty()) {
        filePathEdit_->setText(path);
    }
}

void TxPanel::onBrowseOutput()
{
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("保存 WAV 文件"),
                                                 QString(), QStringLiteral("WAV 文件 (*.wav)"));
    if (!path.isEmpty()) {
        outputWavPath_->setText(path);
    }
}

void TxPanel::onExportClicked()
{
    // 直接取当前输入（文本或文件），导出时现场编码，无需先发射
    QString input;
    if (radioText_->isChecked()) {
        input = textInput_->toPlainText().trimmed();
        if (input.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请输入文本内容"));
            return;
        }
    } else {
        input = filePathEdit_->text().trimmed();
        if (input.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请选择输入文件"));
            return;
        }
    }

    QString path = outputWavPath_->text().trimmed();
    if (path.isEmpty()) {
        path = QFileDialog::getSaveFileName(this, QStringLiteral("导出 WAV 文件"),
                                             QString(), QStringLiteral("WAV 文件 (*.wav)"));
        if (path.isEmpty()) {
            return;
        }
        outputWavPath_->setText(path);
    }
    emit exportWavRequested(input, path);
}

void TxPanel::onStartClicked()
{
    QString payload;
    if (radioText_->isChecked()) {
        payload = textInput_->toPlainText().trimmed();
        if (payload.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请输入文本内容"));
            return;
        }
    } else {
        payload = filePathEdit_->text().trimmed();
        if (payload.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请选择输入文件"));
            return;
        }
    }

    // 先进入"发射中"状态再通知主窗口：编码现在在后台线程进行，
    // 主窗口随后会把状态改写为"编码中..."（若先 emit 会被这里覆盖）
    btnStart_->setEnabled(false);
    btnStop_->setEnabled(true);
    radioText_->setEnabled(false);
    radioFile_->setEnabled(false);
    radioImage_->setEnabled(false);
    progress_->setValue(0);
    labelStatus_->setText(QStringLiteral("发射中..."));

    emit startTransmission(payload);
}

void TxPanel::onStopClicked()
{
    emit stopTransmission();
    btnStart_->setEnabled(true);
    btnStop_->setEnabled(false);
    labelStatus_->setText(QStringLiteral("已停止发射"));
    setHint(QStringLiteral("发射时输出设备将播放调制音频，请先确认输出设备与音量"), false);
}

void TxPanel::onTransmissionProgress(int percent)
{
    progress_->setValue(percent);
}

void TxPanel::onTransmissionStatus(const QString& status)
{
    labelStatus_->setText(status);
    // 提示文字随阶段切换：准备音 / 发射中 / 出错（红色）/ 结束复位
    if (status.contains(QStringLiteral("失败")) || status.contains(QStringLiteral("错误"))
        || status.contains(QStringLiteral("无法"))) {
        setHint(status, true);
    } else if (status.contains(QStringLiteral("准备音"))) {
        setHint(QStringLiteral("正在播放准备音，请确认输出设备与音量"), false);
    } else if (status.contains(QStringLiteral("发射中"))) {
        setHint(QStringLiteral("正在播放编码音频，请勿将音量调至过高"), false);
    } else if (status.contains(QStringLiteral("完成")) ||
               status.contains(QStringLiteral("停止"))) {
        setHint(QStringLiteral("发射时输出设备将播放调制音频，请先确认输出设备与音量"), false);
    }

    if (status.contains(QStringLiteral("完成")) || status.contains(QStringLiteral("失败"))) {
        btnStart_->setEnabled(true);
        btnStop_->setEnabled(false);
        radioText_->setEnabled(true);
        radioFile_->setEnabled(true);
        radioImage_->setEnabled(true);
    }
}

} // namespace audiot
