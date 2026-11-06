// theme.h - 全应用深色主题
//
// 深色模式为本应用的**正式主题**（主色 #1585C0）。实现方式为
// QPalette + 全局 QSS：
//   - 不使用阴影、渐变与半透明，避免弱机/32 位系统上的额外合成开销
//   - 不引入任何过渡动画，切换/刷新不会造成全窗口闪烁
//   - 波形监视器保持自己的黑底绿线配色，不受本主题影响（局部重绘）
#ifndef AUDIOT_THEME_H
#define AUDIOT_THEME_H

class QApplication;

namespace audiot {

// 应用深色主题：设置调色板与全局样式表。需在创建 QApplication 之后、
// 显示主窗口之前调用一次。
void applyDarkTheme(QApplication& app);

} // namespace audiot
#endif // AUDIOT_THEME_H
