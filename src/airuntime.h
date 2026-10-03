#pragma once
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace AiRuntime {
inline QString home(const char *environment, const QString &kind, const QString &legacy) {
    const QString override = qEnvironmentVariable(environment);
    if (!override.isEmpty()) return override;
    const QString bundled = QDir::cleanPath(QCoreApplication::applicationDirPath() + "/../lib/omaraw/" + kind);
    if (QFileInfo::exists(bundled + "/bundled")) return bundled;
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/omaraw/" + legacy;
}
inline bool bundled(const QString &home) { return QFileInfo::exists(home + "/bundled"); }
}
