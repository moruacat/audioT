// theme.cpp - 全应用深色主题（调色板 + 全局 QSS）
#include "theme.h"

#include <QApplication>
#include <QPalette>
#include <QColor>
#include <QString>
#include <QAbstractButton>
#include <QCheckBox>
#include <QRadioButton>
#include <QDialog>
#include <QVariantAnimation>
#include <QSequentialAnimationGroup>
#include <QEasingCurve>
#include <QPainter>
#include <QEvent>

namespace audiot {

// ============================================================
// 悬浮 / 点击 / 切换的"淡入淡出"反馈
// ============================================================
//
// Qt Widgets 的 QSS **不支持**过渡动画（:hover 只能瞬间切换颜色，观感
// 很"生硬"）。这里用一个铺在控件之上的半透明覆盖层，由
// QVariantAnimation 驱动透明度，实现真正的渐变色过渡：
//   - 覆盖层不吃鼠标事件，不影响按钮原有交互
//   - 动画只在 hover 进入/离开、按下、切换的瞬间短暂运行（150~260ms）
//   - 覆盖层只覆盖单个控件，且不产生全窗口重绘（弱机也安全）
//   - 缓动曲线用 InOutQuad，避免线性动画的"机械感"
class FadeOverlay : public QWidget {
public:
    explicit FadeOverlay(QWidget* target)
        : QWidget(target)
        , target_(target)
        , radius_(6)
        , strength_(0.0)
        , hovered_(false)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);
        hide();

        hoverAnim_ = new QVariantAnimation(this);
        hoverAnim_->setDuration(150);
        hoverAnim_->setEasingCurve(QEasingCurve::InOutQuad);
        connect(hoverAnim_, &QVariantAnimation::valueChanged, this,
                [this](const QVariant& v) { setStrength(v.toDouble()); });

        // 点击/切换反馈：先短促增亮，再平滑淡回（两段顺序动画）
        pulseSeq_ = new QSequentialAnimationGroup(this);
        pulseUp_ = new QVariantAnimation;
        pulseUp_->setDuration(90);
        pulseUp_->setEasingCurve(QEasingCurve::OutQuad);
        pulseDown_ = new QVariantAnimation;
        pulseDown_->setDuration(180);
        pulseDown_->setEasingCurve(QEasingCurve::InOutQuad);
        connect(pulseUp_, &QVariantAnimation::valueChanged, this,
                [this](const QVariant& v) { setStrength(v.toDouble()); });
        connect(pulseDown_, &QVariantAnimation::valueChanged, this,
                [this](const QVariant& v) { setStrength(v.toDouble()); });
        pulseSeq_->addAnimation(pulseUp_);     // 所有权移交给动画组
        pulseSeq_->addAnimation(pulseDown_);
        connect(pulseSeq_, &QSequentialAnimationGroup::finished, this,
                [this]() { setStrength(hovered_ ? 1.0 : 0.0); });
    }

    // 摆放反馈层：矩形与圆角由调用方按控件类型决定
    void setTargetGeometry(const QRect& r, int radius)
    {
        radius_ = radius;
        setGeometry(r);
    }

    // 悬浮进入 / 离开：平滑过渡到 1.0 / 0.0
    void animateHover(bool on)
    {
        hovered_ = on;
        pulseSeq_->stop();
        if (on) show();
        startTwoPoint(on ? 1.0 : 0.0, 150);
    }

    // 按下 / 切换：先快速增亮再淡回原状，形成一次"确认式"的淡入淡出
    void pulse()
    {
        hoverAnim_->stop();
        show();
        pulseUp_->stop();
        pulseUp_->setStartValue(strength_);
        pulseUp_->setEndValue(1.7);
        pulseDown_->setStartValue(1.7);
        pulseDown_->setEndValue(hovered_ ? 1.0 : 0.0);
        pulseSeq_->start();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        if (strength_ <= 0.001) return;
        // strength 0~1 为悬浮色，最高约 1.7 为按下/切换的峰值
        const int alpha = qBound(0, static_cast<int>(strength_ * 34), 60);
        if (alpha <= 0) return;
        QColor tint = target_->property("primary").toBool()
            ? QColor(255, 255, 255, alpha)     // 主色按钮用白色提亮
            : QColor(21, 133, 192, alpha);     // 其余按钮用主色着色
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        // 圆角与目标控件一致（按钮 6px / 复选框 4px / 单选框取半径），
        // 覆盖层只铺在指定矩形上，不会溢出去盖住相邻控件
        const qreal rad = radius_;
        p.setPen(Qt::NoPen);
        p.setBrush(tint);
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), rad, rad);
    }

private:
    void setStrength(double v)
    {
        strength_ = v;
        if (strength_ <= 0.001 && !hovered_) {
            hide();
        } else {
            show();
        }
        update();
    }

    void startTwoPoint(double end, int durationMs)
    {
        hoverAnim_->stop();
        hoverAnim_->setDuration(durationMs);
        hoverAnim_->setStartValue(strength_);
        hoverAnim_->setEndValue(end);
        hoverAnim_->start();
        update();
    }

    QWidget* target_;
    QVariantAnimation* hoverAnim_;
    QVariantAnimation* pulseUp_;
    QVariantAnimation* pulseDown_;
    QSequentialAnimationGroup* pulseSeq_;
    int radius_;
    double strength_;
    bool hovered_;
};

namespace {

// 反馈层应当铺在控件的哪一块上：
//   - 普通按钮：整个按钮矩形（按钮本身就是一个完整色块）
//   - 复选框 / 单选框：**只覆盖指示框**，而且尺寸在本地算死，不查样式。
//     原因：QSS 覆盖样式后 QStyle::subElementRect 对指示框的推算并不可靠，
//     勾选前后还可能给出不同结果。一旦算大了，这层半透明色就会盖住自己的
//     文字、甚至蹭到相邻控件的指示框上。
// 指示框的实际尺寸由 theme 的 QSS 固定：复选框 16+1+1=18，单选框 14+1+1=16，
// 位置在控件最左端、垂直居中。按此计算后：
//   宽度 <= 18px，而布局中相邻控件至少相隔一个 spacing(>=6px)，
//   因此这层不可能与任何相邻控件重叠。
QRect feedbackRect(QAbstractButton* btn)
{
    if (!btn) {
        return QRect();
    }
    const bool isCheckBox = qobject_cast<QCheckBox*>(btn) != nullptr;
    const bool isRadio = qobject_cast<QRadioButton*>(btn) != nullptr;
    if (!isCheckBox && !isRadio) {
        return btn->rect();
    }
    const int box = isCheckBox ? 18 : 16;      // 内容 16/14 + 两侧 1px 边框
    const int h = qMin(box, btn->height());
    return QRect(0, (btn->height() - h) / 2, box, h);
}

// 反馈层的圆角：与控件自身的圆角保持一致，避免边缘露出直角
int feedbackRadius(QAbstractButton* btn, const QRect& r)
{
    if (qobject_cast<QRadioButton*>(btn)) {
        return qMin(r.width(), r.height()) / 2;   // 圆形指示框
    }
    if (qobject_cast<QCheckBox*>(btn)) {
        return 4;                                  // 与 QSS 的 4px 圆角一致
    }
    return 6;                                      // 与按钮 QSS 的 6px 圆角一致
}

// 取（必要时创建）按钮上的反馈层，并按当前几何摆放
FadeOverlay* overlayFor(QAbstractButton* btn)
{
    FadeOverlay* ov = nullptr;
    for (QObject* child : btn->children()) {
        if (auto* found = dynamic_cast<FadeOverlay*>(child)) {
            ov = found;
            break;
        }
    }
    if (!ov) {
        ov = new FadeOverlay(btn);
        ov->raise();
    }
    const QRect r = feedbackRect(btn);
    ov->setTargetGeometry(r, feedbackRadius(btn, r));
    return ov;
}

// 只调整已有反馈层的位置（不创建），用于频繁触发的 Resize
void syncOverlayGeometry(QAbstractButton* btn)
{
    for (QObject* child : btn->children()) {
        if (auto* ov = dynamic_cast<FadeOverlay*>(child)) {
            const QRect r = feedbackRect(btn);
            ov->setTargetGeometry(r, feedbackRadius(btn, r));
        }
    }
}

// 应用级事件过滤器：给所有按钮挂上悬浮/点击反馈，不改变按钮本身行为
class ButtonFeedbackFilter : public QObject {
public:
    explicit ButtonFeedbackFilter(QObject* parent) : QObject(parent) {}

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override
    {
        auto* btn = qobject_cast<QAbstractButton*>(obj);
        if (!btn) {
            return false;
        }
        // 只给主窗口里的常驻控件做反馈：对话框（QMessageBox / QFileDialog 等）
        // 与菜单里的按钮是临时创建销毁的，给它们挂覆盖层既无意义，也可能在
        // 系统对话框上留下视觉残留。
        if (qobject_cast<QDialog*>(btn->window())) {
            return false;
        }
        switch (ev->type()) {
            case QEvent::Enter:
                if (btn->isEnabled()) overlayFor(btn)->animateHover(true);
                break;
            case QEvent::Leave:
                overlayFor(btn)->animateHover(false);
                break;
            case QEvent::MouseButtonPress:
                if (btn->isEnabled()) overlayFor(btn)->pulse();
                break;
            case QEvent::MouseButtonRelease:
                // 覆盖"点击按钮"与"切换复选框/单选框"两种反馈
                if (btn->isEnabled()) overlayFor(btn)->pulse();
                break;
            case QEvent::Resize:
                syncOverlayGeometry(btn);
                break;
            case QEvent::EnabledChange:
                if (!btn->isEnabled()) overlayFor(btn)->animateHover(false);
                break;
            default:
                break;
        }
        return false;   // 绝不拦截事件
    }
};

// 主题色板（集中定义，避免散落在各处的硬编码色值）
const char* kAccent      = "#1585C0";   // 主色：选中页签 / 主要按钮 / 焦点边框
const char* kWindow      = "#1e1e20";   // 窗口背景
const char* kSurface     = "#252528";   // 输入框、下拉等控件背景
const char* kSurfaceAlt  = "#2b2b2f";   // 次级面板
const char* kBorder      = "#3f3f46";   // 常规边框
const char* kBorderSoft  = "#3a3a3d";   // 分组框边框
const char* kText        = "#e0e0e0";   // 主文字
const char* kTextDim     = "#9a9a9a";   // 次级文字（在深色底上仍清晰）
const char* kTextDisabled= "#6a6a6a";
const char* kDanger      = "#ff7b72";   // 错误/停止类按钮文字

QPalette buildPalette()
{
    QPalette p;
    const QColor accent(kAccent);
    p.setColor(QPalette::Window, QColor(kWindow));
    p.setColor(QPalette::WindowText, QColor(kText));
    p.setColor(QPalette::Base, QColor(kSurface));
    p.setColor(QPalette::AlternateBase, QColor(kSurfaceAlt));
    p.setColor(QPalette::Text, QColor(kText));
    p.setColor(QPalette::Button, QColor(kSurfaceAlt));
    p.setColor(QPalette::ButtonText, QColor(kText));
    p.setColor(QPalette::BrightText, QColor(kDanger));
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    p.setColor(QPalette::ToolTipBase, QColor(kSurfaceAlt));
    p.setColor(QPalette::ToolTipText, QColor(kText));
    p.setColor(QPalette::Link, QColor("#3fa9e0"));
    p.setColor(QPalette::LinkVisited, QColor("#3fa9e0"));
    p.setColor(QPalette::PlaceholderText, QColor("#7a7a7a"));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(kTextDisabled));
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(kTextDisabled));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(kTextDisabled));
    p.setColor(QPalette::Disabled, QPalette::Highlight, QColor("#2c4a5c"));
    p.setColor(QPalette::Disabled, QPalette::HighlightedText, QColor("#8a8a8a"));
    return p;
}

QString buildStyleSheet()
{
    static const char* kTemplate = R"(
/* ---------- 基础 ---------- */
QWidget { color: @TEXT@; }
QMainWindow, QDialog { background: @WINDOW@; }
QScrollArea { background: transparent; border: none; }
QScrollArea > QWidget > QWidget { background: transparent; }

/* ---------- 分组框（卡片）：圆角 8px，无阴影 ---------- */
/* margin-top 必须明显大于 padding-top：标题绘制在 margin 区里，
   否则标题下沿会盖住分组内第一行文字（表现为"文字被遮挡"）。 */
QGroupBox {
    border: 1px solid @BORDER@;
    border-radius: 8px;
    margin-top: 20px;
    padding-top: 4px;
    background: @SURFACE@;
}
QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    left: 10px;
    padding: 0 4px;
    color: @TITLE@;
}

/* ---------- 页签：当前页用主色下边框强调 ---------- */
QTabWidget::pane {
    border: 1px solid @BORDER@;
    border-radius: 8px;
    background: @SURFACE@;
}
QTabBar::tab {
    background: transparent;
    color: @DIM@;
    padding: 6px 16px;
    margin-right: 2px;
    border: none;
    border-bottom: 2px solid transparent;
}
QTabBar::tab:hover { color: @TEXT@; }
QTabBar::tab:selected {
    color: #ffffff;
    background: @SURFACE_ALT@;
    border-bottom: 2px solid @ACCENT@;
}

/* ---------- 按钮：圆角 6px ---------- */
QPushButton {
    background: @SURFACE_ALT@;
    color: @TEXT@;
    border: 1px solid @BORDER@;
    border-radius: 6px;
    padding: 4px 12px;
    min-height: 20px;
}
QPushButton:hover { background: #37373b; }
QPushButton:pressed { background: #202023; }
QPushButton:disabled {
    background: #262628;
    color: @DISABLED@;
    border-color: #333336;
}
/* 主要操作按钮（开始发射 / 解析 WAV / 开始监听 / 发送） */
QPushButton[primary="true"] {
    background: @ACCENT@;
    color: #ffffff;
    border-color: @ACCENT@;
    font-weight: bold;
}
QPushButton[primary="true"]:hover { background: #1a94d4; }
QPushButton[primary="true"]:pressed { background: #1271a6; }
QPushButton[primary="true"]:disabled {
    background: #2c4a5c;
    color: #8a8a8a;
    border-color: #2c4a5c;
}
/* 停止/危险类按钮 */
QPushButton[danger="true"] { color: @DANGER@; border-color: #5a3f3c; }
QPushButton[danger="true"]:hover { background: #3a2724; }
QPushButton[danger="true"]:disabled { color: @DISABLED@; border-color: #333336; }

/* ---------- 文本输入 / 下拉 / 数值：圆角 5px，焦点用主色 ---------- */
QLineEdit, QTextEdit, QPlainTextEdit, QSpinBox, QComboBox {
    background: @SURFACE@;
    color: @TEXT@;
    border: 1px solid @BORDER@;
    border-radius: 5px;
    padding: 3px 6px;
    selection-background-color: @ACCENT@;
    selection-color: #ffffff;
}
QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus,
QSpinBox:focus, QComboBox:focus { border: 1px solid @ACCENT@; }
QLineEdit:disabled, QTextEdit:disabled, QPlainTextEdit:disabled,
QSpinBox:disabled, QComboBox:disabled {
    background: #232326;
    color: @DISABLED@;
}
QComboBox QAbstractItemView {
    background: @SURFACE_ALT@;
    color: @TEXT@;
    border: 1px solid @BORDER@;
    selection-background-color: @ACCENT@;
    selection-color: #ffffff;
    outline: none;
}

/* ---------- 复选框：圆角方框 + 清晰勾选 ---------- */
/* 关键约束（踩过的坑）：
   1) 每个状态都必须带**同尺寸**的 image。之前只有 checked 带 image、
      unchecked 不带，Qt 计算 indicator 尺寸时依据不同（image 尺寸 vs
      width/height），勾选后指示框会宽一点点，整个控件随之右移，导致
      选中态与未选中态对不齐。现在两个状态都用 16x16 的图（未选中用
      全透明 blank.svg），尺寸规则完全一致。
   2) 勾选/未选中用完全相同的 border/background 规则，保证 sizeHint 不变。
   3) image-position:center 让图形固定居中，避免看起来"偏一点"。 */
QCheckBox, QRadioButton { spacing: 6px; }
QCheckBox { min-height: 22px; }
QRadioButton { min-height: 20px; }
QCheckBox::indicator, QCheckBox::indicator:unchecked {
    width: 16px; height: 16px;
    border: 1px solid #5a5a5e;
    border-radius: 4px;
    background: @SURFACE@;
    image: url(:/icons/blank.svg);
    image-position: center;
}
QCheckBox::indicator:hover {
    width: 16px; height: 16px;
    border: 1px solid @ACCENT@;
    border-radius: 4px;
    background: @SURFACE@;
    image: url(:/icons/blank.svg);
    image-position: center;
}
QCheckBox::indicator:checked {
    width: 16px; height: 16px;
    border: 1px solid @ACCENT@;
    border-radius: 4px;
    background: @ACCENT@;
    image: url(:/icons/check.svg);
    image-position: center;
}
QCheckBox::indicator:disabled {
    width: 16px; height: 16px;
    border: 1px solid #3a3a3d;
    border-radius: 4px;
    background: #232326;
    image: url(:/icons/blank.svg);
    image-position: center;
}
QRadioButton::indicator, QRadioButton::indicator:unchecked {
    width: 14px; height: 14px;
    border: 1px solid #5a5a5e;
    border-radius: 8px;
    background: @SURFACE@;
    image: url(:/icons/blank.svg);
    image-position: center;
}
/* 选中态必须与未选中态使用**同一个边框宽度**：QSS 里 width/height 是
   内容尺寸，border 会加在外面。之前这里用 4px 边框来画内圈，导致选中时
   指示框从 16px 变成 22px，整行随之位移、并把相邻标签挤到被裁剪。 */
QRadioButton::indicator:checked {
    width: 14px; height: 14px;
    border: 1px solid @ACCENT@;
    border-radius: 8px;
    background: @ACCENT@;
    image: url(:/icons/blank.svg);
    image-position: center;
}
QRadioButton::indicator:hover {
    width: 14px; height: 14px;
    border: 1px solid @ACCENT@;
    border-radius: 8px;
    background: @SURFACE@;
    image: url(:/icons/blank.svg);
    image-position: center;
}
QRadioButton::indicator:checked:hover {
    width: 14px; height: 14px;
    border: 1px solid @ACCENT@;
    border-radius: 8px;
    background: @ACCENT@;
    image: url(:/icons/blank.svg);
    image-position: center;
}

/* ---------- 进度条 ---------- */
QProgressBar {
    background: @SURFACE@;
    border: 1px solid @BORDER@;
    border-radius: 4px;
    text-align: center;
    color: @TEXT@;
}
QProgressBar::chunk { background: @ACCENT@; border-radius: 3px; }

/* ---------- 列表（电台聊天区） ---------- */
QListWidget { background: @SURFACE_ALT@; border: 1px solid @BORDER@; border-radius: 8px; }
QListWidget::item { border: none; }

/* ---------- 滚动条：细、无箭头按钮 ---------- */
QScrollBar:vertical { background: @WINDOW@; width: 10px; margin: 0; }
QScrollBar::handle:vertical {
    background: #45454a; border-radius: 5px; min-height: 24px;
}
QScrollBar::handle:vertical:hover { background: #5a5a5e; }
QScrollBar:horizontal { background: @WINDOW@; height: 10px; margin: 0; }
QScrollBar::handle:horizontal {
    background: #45454a; border-radius: 5px; min-width: 24px;
}
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

/* ---------- 菜单 / 状态栏 / 提示 ---------- */
QMenuBar { background: @WINDOW@; color: @TEXT@; }
QMenuBar::item:selected { background: @ACCENT@; color: #ffffff; }
QMenu { background: @SURFACE_ALT@; color: @TEXT@; border: 1px solid @BORDER@; }
QMenu::item:selected { background: @ACCENT@; color: #ffffff; }
QMenu::separator { height: 1px; background: @BORDER@; margin: 4px 8px; }
QStatusBar { background: @WINDOW@; color: @DIM@; }
QToolTip {
    background: @SURFACE_ALT@; color: @TEXT@;
    border: 1px solid @BORDER@; padding: 3px 6px;
}

/* ---------- 标签页内层容器不额外上色 ---------- */
QStackedWidget { background: transparent; }
)";

    QString s = QString::fromUtf8(kTemplate);
    s.replace(QStringLiteral("@WINDOW@"), QLatin1String(kWindow));
    s.replace(QStringLiteral("@SURFACE@"), QLatin1String(kSurface));
    s.replace(QStringLiteral("@SURFACE_ALT@"), QLatin1String(kSurfaceAlt));
    s.replace(QStringLiteral("@BORDER@"), QLatin1String(kBorder));
    s.replace(QStringLiteral("@TEXT@"), QLatin1String(kText));
    s.replace(QStringLiteral("@DIM@"), QLatin1String(kTextDim));
    s.replace(QStringLiteral("@DISABLED@"), QLatin1String(kTextDisabled));
    s.replace(QStringLiteral("@DANGER@"), QLatin1String(kDanger));
    s.replace(QStringLiteral("@TITLE@"), QLatin1String("#cfcfcf"));
    s.replace(QStringLiteral("@ACCENT@"), QLatin1String(kAccent));
    return s;
}

} // namespace

void applyDarkTheme(QApplication& app)
{
    app.setStyle(QStringLiteral("Fusion"));   // 统一各平台基线外观，QSS 才可控
    app.setPalette(buildPalette());
    app.setStyleSheet(buildStyleSheet());
    // 为所有按钮挂上悬浮/点击/切换的渐变反馈（事件过滤器不改变按钮行为）
    app.installEventFilter(new ButtonFeedbackFilter(&app));
}

} // namespace audiot
