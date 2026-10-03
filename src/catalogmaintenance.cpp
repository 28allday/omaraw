#include "catalogmaintenance.h"
#include "catalogbackup.h"
#include <QClipboard>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLocale>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace {
struct Result { bool ok = true; int entries = 0, edited = 0; QStringList lines; };
Result checkDatabase(const QString &path, const QString &label, bool catalog) {
    Result result;
    result.lines << label << path;
    const QString connection = "catalog-check-" + QUuid::createUuid().toString(QUuid::Id128);
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connection);
        db.setDatabaseName(path); db.setConnectOptions("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=5000");
        if (!db.open()) { result.ok = false; result.lines << CatalogMaintenance::tr("Could not open: %1").arg(db.lastError().text()); }
        else {
            QSqlQuery query(db);
            QStringList problems;
            // Keep the catalogue's structural and relationship checks on the
            // same committed snapshot, even if an import finishes meanwhile.
            if (!db.transaction()) problems << db.lastError().text();
            if (!query.exec("PRAGMA integrity_check")) problems << query.lastError().text();
            else {
                bool sawResult = false;
                while (query.next()) { sawResult = true; if (query.value(0).toString() != "ok") problems << query.value(0).toString(); }
                if (!sawResult) problems << CatalogMaintenance::tr("SQLite returned no integrity result.");
                if (query.lastError().isValid()) problems << query.lastError().text();
            }
            if (catalog) {
                if (!query.exec("PRAGMA foreign_key_check")) problems << query.lastError().text();
                else {
                    while (query.next()) problems << CatalogMaintenance::tr("%1 row %2 refers to a missing %3 row").arg(query.value(0).toString(), query.value(1).toString(), query.value(2).toString());
                    if (query.lastError().isValid()) problems << query.lastError().text();
                }
                if (query.exec("SELECT COUNT(*),COALESCE(SUM(edited<>0),0) FROM assets") && query.next()) {
                    result.entries = query.value(0).toInt(); result.edited = query.value(1).toInt();
                } else problems << query.lastError().text();
            }
            query.finish(); db.rollback();
            result.ok = problems.isEmpty();
            result.lines << (result.ok ? CatalogMaintenance::tr("Passed — no database errors found.") : CatalogMaintenance::tr("Needs attention:\n%1").arg(problems.join('\n')));
            result.lines << CatalogMaintenance::tr("Database file: %1").arg(QLocale().formattedDataSize(QFileInfo(path).size()));
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(connection);
    return result;
}
}

CatalogMaintenance::CatalogMaintenance(QObject *parent) : QObject(parent) { m_pool.setMaxThreadCount(1); }
CatalogMaintenance::~CatalogMaintenance() { m_pool.waitForDone(); }
void CatalogMaintenance::setCatalog(const QString &path) {
    if (path == m_catalog) return;
    m_catalog = path; m_summary.clear(); m_report.clear(); m_passed = false; emit changed();
}
void CatalogMaintenance::check(const QString &catalog, const QString &engineConfig) {
    if (m_busy || catalog.isEmpty()) return;
    setCatalog(catalog); m_busy = true; m_passed = false; m_summary = tr("Checking catalog and Develop databases…"); m_report.clear(); emit changed();
    m_pool.start([this, catalog, engineConfig] {
        QElapsedTimer elapsed; elapsed.start();
        Result result = checkDatabase(catalog, tr("Photo catalog — structure and references"), true);
        QStringList report{tr("OmaRAW catalog integrity report"), tr("Checked: %1").arg(QDateTime::currentDateTime().toString(Qt::ISODate)),
                           tr("Photos and versions: %1").arg(result.entries), QString(), result.lines.join('\n')};
        const QList<QPair<QString,QString>> databases{{CatalogBackup::engineLibrary(catalog), tr("Develop history — database structure")},
                                                    {engineConfig + "/data.db", tr("Develop settings — database structure")}};
        for (const auto &entry : databases) {
            if (QFileInfo::exists(entry.first)) {
                const auto checked = checkDatabase(entry.first, entry.second, false);
                result.ok &= checked.ok; report << QString() << checked.lines.join('\n');
            } else {
                const bool required = result.edited > 0;
                result.ok &= !required;
                report << QString() << entry.second << entry.first
                       << (required ? tr("Missing — this catalog contains edited photos. Restore the missing Develop data from a complete backup.")
                                    : tr("Not present — no edited photos recorded in this catalog."));
            }
        }
        report << QString() << tr("Completed in %1 seconds.").arg(elapsed.elapsed()/1000.0, 0, 'f', 2)
               << tr("This checks database structure and catalog references. It does not check original photo files, external profiles or the visual correctness of edits. No repairs were made.");
        if (!result.ok) report << tr("Keep the current catalog. Restore a verified backup into a separate folder, or copy this report for diagnosis.");
        const QString text = report.join('\n');
        QMetaObject::invokeMethod(this, [this, catalog, ok=result.ok, text] {
            m_busy = false;
            if (m_catalog == catalog) { m_passed = ok; m_report = text; m_summary = ok ? tr("Check passed — no database errors found") : tr("Check completed — attention needed"); }
            emit changed();
        }, Qt::QueuedConnection);
    });
}
void CatalogMaintenance::copyReport() const { if (!m_report.isEmpty()) QGuiApplication::clipboard()->setText(m_report); }
