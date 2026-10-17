// app_config.h - 应用配置文件的统一路径（INI）
#ifndef AUDIOT_APP_CONFIG_H
#define AUDIOT_APP_CONFIG_H

#include <QString>

namespace audiot {

// 应用配置文件（INI，可移植、纯文本可见）的统一路径：
//   Windows: %APPDATA%/AudioT/audioT.ini
//   Linux:   $XDG_CONFIG_HOME/AudioT/audioT.ini（或 ~/.config/AudioT/audioT.ini）
// 音频设备、采样率/缓冲/Mark/Space/波特率等高级参数，以及各收发选项卡的
// 调制设置（modem:<surface> 分组）都写入这同一个文件；缺失或不可读时各处
// 回退到内置默认值。
QString appConfigFilePath();

} // namespace audiot
#endif // AUDIOT_APP_CONFIG_H
