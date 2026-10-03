#include "catalog.h"
#include "autotag.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QSet>
#include <QCoreApplication>
#include <cmath>

QVariantList Catalog::autoTags(int id) const {
    const auto rec = asset(id);
    if (!rec.id) return {};
    QSqlQuery q(m_db);
    q.prepare("SELECT tag,confidence,decision FROM asset_auto_tags WHERE asset_id=?");
    q.addBindValue(rec.variantOf > 0 ? rec.variantOf : id);
    QHash<QString,QVariantMap> rows;
    if (q.exec()) while (q.next()) {
        const double score=q.value(1).toDouble(); const int decision=q.value(2).toInt();
        rows.insert(q.value(0).toString(), {{"confidence",score},{"decision",decision},
                    {"active",decision == 1 || (decision == 0 && score >= AutoTags::threshold)}});
    }
    QVariantList out;
    for (const auto &entry : AutoTags::options()) {
        const auto option=entry.toMap(); const QString key=option.value("tag").toString();
        auto row=rows.value(key, {{"confidence",0.0},{"decision",0},{"active",false}});
        row.insert("tag",key); row.insert("label",AutoTags::label(key));
        row.insert("group",option.value("group"));
        out << row;
    }
    return out;
}
QVariantList Catalog::autoTagCollections() const {
    QHash<QString,bool> choices;
    QSqlQuery q(m_db);
    if(isOpen() && q.exec("SELECT tag,enabled FROM auto_tag_collection_choices")) while(q.next()) choices.insert(q.value(0).toString(),q.value(1).toBool());
    QVariantList out;
    for(const auto &group:AutoTags::groups()) {
        const auto g=group.toMap(); const QString key=g.value("key").toString()=="people" ? QString("people") : "group:"+g.value("key").toString();
        out << QVariantMap{{"key",key},{"label",g.value("label")},{"group",true},{"enabled",choices.value(key)}};
    }
    for(const auto &option:AutoTags::options()) {
        const auto o=option.toMap(); const QString key=o.value("tag").toString(); if(key=="people") continue;
        out << QVariantMap{{"key",key},{"label",o.value("label")},{"group",false},{"enabled",choices.value(key)}};
    }
    return out;
}
bool Catalog::setAutoTagCollectionEnabled(const QString &key,bool enabled) {
    if(!AutoTags::collectionKeys().contains(key)) return false;
    if(!exec("SAVEPOINT auto_tag_choice")) return false;
    QSqlQuery q(m_db); q.prepare("INSERT INTO auto_tag_collection_choices VALUES(?,?) ON CONFLICT(tag) DO UPDATE SET enabled=excluded.enabled");
    q.addBindValue(key); q.addBindValue(enabled ? 1 : 0); bool ok=q.exec();
    if(ok && enabled) {
        // An explicit enable can recreate an album the user deleted earlier.
        q.prepare("DELETE FROM auto_tag_albums WHERE tag=? AND album_id IS NULL"); q.addBindValue(key); ok=q.exec();
    }
    if(!ok) { m_error=q.lastError().text(); exec("ROLLBACK TO auto_tag_choice"); }
    const bool released=exec("RELEASE auto_tag_choice"); return ok && released;
}
QString Catalog::autoTagScanStatus(int id) const {
    const auto rec=asset(id); if (!rec.id) return {};
    QSqlQuery q(m_db); q.prepare("SELECT state,error,model FROM auto_tag_jobs WHERE asset_id=?");
    q.addBindValue(rec.variantOf > 0 ? rec.variantOf : id);
    if (!q.exec() || !q.next()) return QCoreApplication::translate("AutoTags","Not scanned");
    if (q.value(0)=="queued") return QCoreApplication::translate("AutoTags","Waiting to scan");
    if (q.value(0)=="error") return q.value(1).toString();
    if (q.value(2).toString()!=AutoTags::modelVersion()) return QCoreApplication::translate("AutoTags","Scan again to detect the additional subjects");
    return QCoreApplication::translate("AutoTags","Scanned locally");
}
bool Catalog::setAutoTagDecision(int id, const QString &tag, int decision) {
    if (!AutoTags::keys().contains(tag) || decision < -1 || decision > 1) return false;
    const auto rec=asset(id); if (!rec.id) return false;
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO asset_auto_tags(asset_id,tag,decision) VALUES(?,?,?) ON CONFLICT(asset_id,tag) DO UPDATE SET decision=excluded.decision");
    q.addBindValue(rec.variantOf > 0 ? rec.variantOf : id); q.addBindValue(tag); q.addBindValue(decision);
    if (!q.exec()) { m_error=q.lastError().text(); return false; } return true;
}
bool Catalog::queueAutoTags(const QVector<int> &ids, bool force) {
    if (!exec("SAVEPOINT queue_auto_tags")) return false;
    bool ok=true; QSet<int> seen;
    QSqlQuery q(m_db);
    q.prepare(force
        ? "INSERT INTO auto_tag_jobs(asset_id,state) VALUES(?,'queued') ON CONFLICT(asset_id) DO UPDATE SET state='queued',error='',generation=generation+1"
        : "INSERT OR IGNORE INTO auto_tag_jobs(asset_id,state) VALUES(?,'queued')");
    for (int id : ids) {
        const auto rec=asset(id); if (!rec.id) continue;
        const int owner=rec.variantOf > 0 ? rec.variantOf : id;
        if (seen.contains(owner)) continue; seen.insert(owner);
        q.bindValue(0,owner);
        if (!q.exec()) { m_error=q.lastError().text(); ok=false; break; }
    }
    if (!ok) exec("ROLLBACK TO queue_auto_tags");
    const bool released=exec("RELEASE queue_auto_tags"); return ok && released;
}
QVariantMap Catalog::nextAutoTagJob() const {
    QSqlQuery q(m_db);
    if (!q.exec("SELECT j.asset_id,j.generation,a.path FROM auto_tag_jobs j JOIN assets a ON a.id=j.asset_id WHERE j.state='queued' ORDER BY j.asset_id LIMIT 1") || !q.next()) return {};
    return {{"id",q.value(0)},{"generation",q.value(1)},{"path",q.value(2)}};
}
int Catalog::pendingAutoTags() const {
    if (!isOpen()) return 0;
    QSqlQuery q(m_db); return q.exec("SELECT COUNT(*) FROM auto_tag_jobs WHERE state='queued'") && q.next() ? q.value(0).toInt() : 0;
}
int Catalog::failedAutoTagsForImport(int importId) const {
    if (!isOpen() || importId <= 0) return 0;
    QSqlQuery q(m_db); q.prepare("SELECT COUNT(*) FROM auto_tag_jobs j JOIN assets a ON a.id=j.asset_id WHERE j.state='error' AND a.import_id=?");
    q.addBindValue(importId); return q.exec() && q.next() ? q.value(0).toInt() : 0;
}
bool Catalog::resetAutoTagDecisions(int id) {
    const auto rec=asset(id); if (!rec.id) return false;
    QSqlQuery q(m_db); q.prepare("UPDATE asset_auto_tags SET decision=0 WHERE asset_id=?");
    q.addBindValue(rec.variantOf > 0 ? rec.variantOf : id);
    if (!q.exec()) { m_error=q.lastError().text(); return false; } return true;
}
bool Catalog::finishAutoTagJob(int id, int generation, const QVariantMap &scores, const QString &error) {
    if (error.isEmpty()) {
        if (scores.size()!=AutoTags::keys().size()) return false;
        for (const QString &key : AutoTags::keys()) {
            bool valid=false; const double value=scores.value(key).toDouble(&valid);
            if (!valid || !std::isfinite(value) || value < 0 || value > 1) return false;
        }
    }
    if (!exec("SAVEPOINT finish_auto_tag")) return false;
    QSqlQuery check(m_db); check.prepare("SELECT 1 FROM auto_tag_jobs WHERE asset_id=? AND generation=? AND state='queued'");
    check.addBindValue(id); check.addBindValue(generation);
    if (!check.exec()) { m_error=check.lastError().text(); exec("ROLLBACK TO finish_auto_tag"); exec("RELEASE finish_auto_tag"); return false; }
    if (!check.next()) { exec("RELEASE finish_auto_tag"); return true; } // Deleted, replaced, or explicitly rescanned meanwhile.
    check.finish();
    bool ok=true;
    if (error.isEmpty()) for (const QString &key : AutoTags::keys()) {
        QSqlQuery q(m_db);
        // Store positive detections and update prior detections/corrections.
        // Eighty zero-score rows per photograph would only enlarge catalogues.
        q.prepare("INSERT INTO asset_auto_tags(asset_id,tag,confidence,model) SELECT ?,?,?,? WHERE ?>=? OR EXISTS(SELECT 1 FROM asset_auto_tags WHERE asset_id=? AND tag=?) ON CONFLICT(asset_id,tag) DO UPDATE SET confidence=excluded.confidence,model=excluded.model");
        q.addBindValue(id); q.addBindValue(key); q.addBindValue(scores.value(key)); q.addBindValue(AutoTags::modelVersion());
        q.addBindValue(scores.value(key)); q.addBindValue(AutoTags::threshold); q.addBindValue(id); q.addBindValue(key);
        if (!q.exec()) { m_error=q.lastError().text(); ok=false; break; }
    }
    if (ok) {
        QSqlQuery q(m_db); q.prepare("UPDATE auto_tag_jobs SET state=?,model=?,error=? WHERE asset_id=? AND generation=?");
        q.addBindValue(error.isEmpty() ? "done" : "error"); q.addBindValue(AutoTags::modelVersion());
        q.addBindValue(error.isEmpty() ? QStringLiteral("") : error.left(500)); q.addBindValue(id); q.addBindValue(generation);
        ok=q.exec(); if (!ok) m_error=q.lastError().text();
    }
    if (!ok) exec("ROLLBACK TO finish_auto_tag");
    const bool released=exec("RELEASE finish_auto_tag"); return ok && released;
}
bool Catalog::ensureAutoTagAlbum(const QString &tag, const QString &name, const QString &rule, const QString &json) {
    if (!AutoTags::collectionKeys().contains(tag)) return false;
    QSqlQuery check(m_db); check.prepare("SELECT 1 FROM auto_tag_albums WHERE tag=?"); check.addBindValue(tag);
    if (!check.exec()) { m_error=check.lastError().text(); return false; }
    if (check.next()) return true; // Includes a collection deliberately deleted by the user.
    check.finish();
    if (!exec("SAVEPOINT auto_tag_album")) return false;
    const int album=createAlbum(name,rule,json);
    QSqlQuery q(m_db); q.prepare("INSERT INTO auto_tag_albums(tag,album_id) VALUES(?,?)");
    q.addBindValue(tag); q.addBindValue(album);
    const bool ok=album>0 && q.exec();
    if (!ok) { m_error=q.lastError().text(); exec("ROLLBACK TO auto_tag_album"); }
    const bool released=exec("RELEASE auto_tag_album"); return ok && released;
}
