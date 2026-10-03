#pragma once

#include <QString>

// A backup is a directory published only after all database snapshots and
// checksums have succeeded. Restore always creates a new directory.
namespace CatalogBackup {
QString engineLibrary(const QString &catalog);
QString engineConfig(const QString &catalog);
// The engine settings shared by catalogs without their own engine-config:
// <configBase>/omaraw/engine, moving a former <configBase>/engine there.
QString sharedEngineConfig(const QString &configBase);
QString create(const QString &catalog, const QString &destination, QString *error);
QString restore(const QString &manifest, const QString &destination, QString *error);
bool verify(const QString &manifest, QString *error);
bool snapshot(const QString &source, const QString &destination, QString *error);
}
