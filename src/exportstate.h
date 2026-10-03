#pragma once
#include <QString>
#include <QVariantList>

// Atomic, versioned queue journal beside the catalog's engine library.
// ICC bytes belong to the queued job, even if the original profile disappears.
namespace ExportState {
bool load(const QString &path, QVariantList *rows, QString *error);
bool save(const QString &path, const QVariantList &rows, QString *error);
}
