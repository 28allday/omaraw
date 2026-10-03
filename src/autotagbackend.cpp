#include "backend.h"
#include <QDateTime>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

void Backend::setupAutoTags() {
    m_autoTagEnabled=QSettings().value("autotag/enabled",QSettings().value("import/options").toMap().value("autoTag",false)).toBool();
    m_autoTagPaused=QSettings().value("autotag/paused",false).toBool();
    m_autoTagTimer.setSingleShot(true);
    m_autoTagRefresh.setSingleShot(true); m_autoTagRefresh.setInterval(400);
    connect(&m_autoTagTimer,&QTimer::timeout,this,&Backend::pumpAutoTags);
    connect(&m_autoTagRefresh,&QTimer::timeout,this,[this] {
        if(!ensureAutoTagAlbums()) setStatus(tr("Could not update automatic collections: %1").arg(m_catalog.lastError()));
        refresh(); emit catalogChanged(); emit autoTagsChanged();
    });
    connect(&m_autoTagWorker,&AutoTagWorker::completed,this,[this](const QVariantMap &scores,const QString &error,bool fatal) {
        if (m_autoTagJob.isEmpty()) return;
        const auto job=m_autoTagJob; m_autoTagJob.clear();
        const int id=job.value("id").toInt(); const auto asset=m_catalog.asset(id);
        QString failure=error;
        // A moved/deleted photo must not receive a stale worker result.
        if (asset.id && asset.path==job.value("path").toString()) {
            const QFileInfo file(asset.path);
            if (failure.isEmpty() && (file.size()!=job.value("size").toLongLong()
                || file.lastModified().toMSecsSinceEpoch()!=job.value("mtime").toLongLong()))
                failure=tr("Photo changed during the scan. Scan it again.");
            if (!m_catalog.finishAutoTagJob(id,job.value("generation").toInt(),scores,failure)) {
                fatal=true; m_autoTagStatus=tr("Could not save automatic tags: %1").arg(m_catalog.lastError());
                failure=m_autoTagStatus;
            } else if (!failure.isEmpty()) m_autoTagStatus=tr("Auto-tag scan failed: %1").arg(failure);
        }
        if (fatal) {
            m_autoTagPaused=true; QSettings().setValue("autotag/paused",true);
            if (!error.isEmpty()) m_autoTagStatus=error;
        }
        if (fatal || !failure.isEmpty())
            emit autoTagFailed(m_autoTagStatus.isEmpty() ? error : m_autoTagStatus);
        if(!m_catalog.pendingAutoTags() && !ensureAutoTagAlbums()) setStatus(tr("Could not update automatic collections: %1").arg(m_catalog.lastError()));
        if (!m_autoTagRefresh.isActive()) m_autoTagRefresh.start();
        emit autoTagsChanged(); m_autoTagTimer.start(0);
    });
}
void Backend::stopAutoTags() {
    m_autoTagTimer.stop(); m_autoTagRefresh.stop(); m_autoTagJob.clear();
    m_autoTagWorker.stop(); m_autoTagStatus.clear();
}
bool Backend::ensureAutoTagAlbums() {
    for (const auto &entry : m_catalog.autoTagCollections()) {
        const auto choice=entry.toMap(); if(!choice.value("enabled").toBool()) continue;
        const QString key=choice.value("key").toString();
        const QVariantMap rule{{"match","all"},{"autoTag",key}};
        if(m_catalog.count(smartRuleSql(rule))==0) continue;
        const QString json=QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(rule)).toJson(QJsonDocument::Compact));
        if (!m_catalog.ensureAutoTagAlbum(key,tr("%1 (automatic)").arg(AutoTags::label(key)),smartRuleSql(rule),json)) return false;
    }
    return true;
}
void Backend::pumpAutoTags() {
    if (!m_catalog.isOpen() || !m_autoTagEnabled || m_autoTagWorker.busy()) return;
    const int pending=m_catalog.pendingAutoTags();
    if (!pending) { if (m_autoTagStatus.isEmpty() || m_autoTagStatus.startsWith(tr("Scanning"))) m_autoTagStatus=tr("Automatic tags up to date"); emit autoTagsChanged(); return; }
    if (m_autoTagPaused) { emit autoTagsChanged(); return; }
    // Copies, moves and metadata commits get priority over preview reads.
    if (m_importer.running()) { m_autoTagTimer.start(1000); return; }
    m_autoTagJob=m_catalog.nextAutoTagJob(); if (m_autoTagJob.isEmpty()) return;
    const QFileInfo file(m_autoTagJob.value("path").toString());
    m_autoTagJob.insert("size",file.size()); m_autoTagJob.insert("mtime",file.lastModified().toMSecsSinceEpoch());
    m_autoTagStatus=tr("Scanning %1 · %2 remaining").arg(file.fileName()).arg(pending);
    m_autoTagWorker.scan(file.absoluteFilePath()); emit autoTagsChanged();
}
void Backend::setAutoTagEnabled(bool enabled) {
    if(enabled==m_autoTagEnabled) return;
    m_autoTagEnabled=enabled; QSettings().setValue("autotag/enabled",enabled);
    if(!enabled) {
        stopAutoTags();
        // Publish completed tags even if their coalesced refresh was pending.
        if(m_catalog.isOpen()) {
            if(!ensureAutoTagAlbums()) setStatus(tr("Could not update automatic collections: %1").arg(m_catalog.lastError()));
            refresh(); emit catalogChanged();
        }
    }
    else { setAutoTagPaused(false); m_autoTagTimer.start(0); }
    emit autoTagsChanged();
}
bool Backend::setAutoTagCollectionEnabled(const QString &key,bool enabled) {
    if(!m_catalog.setAutoTagCollectionEnabled(key,enabled) || !ensureAutoTagAlbums()) {
        setStatus(tr("Could not update automatic collections: %1").arg(m_catalog.lastError())); return false;
    }
    if(!enabled && m_sourceKind=="smart") {
        bool visible=false;
        for(const auto &album:m_catalog.albums(true)) if(album.toMap().value("id").toInt()==m_sourceId) { visible=true; break; }
        if(!visible) setSource("all");
    }
    refresh(); emit catalogChanged(); emit autoTagsChanged(); emit autoTagSettingsChanged(); return true;
}
void Backend::setAutoTagPaused(bool paused) {
    if (paused==m_autoTagPaused) return;
    m_autoTagPaused=paused; QSettings().setValue("autotag/paused",paused);
    if (paused) {
        m_autoTagWorker.stop(); m_autoTagJob.clear(); m_autoTagTimer.stop();
        m_autoTagStatus=tr("Automatic tagging paused");
    }
    else m_autoTagTimer.start(0);
    emit autoTagsChanged();
}
bool Backend::scanSelectionAutoTags() {
    if(!m_autoTagEnabled) { setStatus(tr("Turn on automatic tagging before scanning.")); return false; }
    const auto ids=targets(0); if (ids.isEmpty()) return false;
    if (!ensureAutoTagAlbums() || !m_catalog.queueAutoTags(ids,true)) { setStatus(tr("Could not queue automatic tags: %1").arg(m_catalog.lastError())); return false; }
    setAutoTagPaused(false); m_autoTagTimer.start(0); emit autoTagsChanged(); return true;
}
bool Backend::setCurrentAutoTag(const QString &tag, bool present) {
    if (!m_currentId || !AutoTags::keys().contains(tag)) return false;
    if (!m_catalog.setAutoTagDecision(m_currentId,tag,present ? 1 : -1) || !ensureAutoTagAlbums()) { setStatus(tr("Could not save automatic tag: %1").arg(m_catalog.lastError())); return false; }
    refresh(); emit autoTagsChanged(); emit catalogChanged(); return true;
}
bool Backend::resetCurrentAutoTagCorrections() {
    if (!m_currentId) return false;
    if (!m_catalog.resetAutoTagDecisions(m_currentId)) { setStatus(tr("Could not reset tag corrections: %1").arg(m_catalog.lastError())); return false; }
    if (!ensureAutoTagAlbums()) { setStatus(tr("Could not update automatic collections: %1").arg(m_catalog.lastError())); return false; }
    refresh(); emit autoTagsChanged(); emit catalogChanged(); return true;
}
