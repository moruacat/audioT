// app_config.cpp - 应用配置文件的统一路径实现
#include "app_config.h"

#include <QDir>
#include <QStandardPaths>

namespace audiot {

QString appConfigFilePath()
{
    const QString dir = QStandardPaths::writableLocation(
        QStandardPaths::AppConfigLocation);
    if (dir.isEmpty()) {
        return QStringLiteral("audioT.ini");
    }
    QDir().mkpath(dir);
    return dir + QStringLiteral("/audioT.ini");
}

} // namespace audiot
