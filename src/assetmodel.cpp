#include "assetmodel.h"
#include "metadata.h"
#include "thumbnailer.h"

#include <QFile>
#include <QFileInfo>
#include <algorithm>

AssetModel::AssetModel(QObject *parent) : QAbstractListModel(parent) {}

void AssetModel::setThumbEdge(int e) {
    if (e == m_thumbEdge) return;
    m_thumbEdge = e;
    if (!m_ids.isEmpty())
        emit dataChanged(index(0), index(m_ids.size() - 1), {ThumbRole});
}

int AssetModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_ids.size();
}

QHash<int, QByteArray> AssetModel::roleNames() const {
    return {
        {IdRole, "assetId"}, {FilenameRole, "filename"}, {PathRole, "path"},
        {FormatRole, "format"}, {IsRawRole, "isRaw"}, {RatingRole, "rating"},
        {FlagRole, "flag"}, {LabelRole, "label"}, {CapturedRole, "captured"},
        {CapturedTimeRole, "capturedTime"}, {ThumbRole, "thumb"}, {WidthRole, "width"},
        {HeightRole, "height"}, {SelectedRole, "selected"}, {CurrentRole, "current"},
        {MakeRole, "make"}, {ModelRole, "model"}, {LensRole, "lens"},
        {ExposureRole, "exposure"}, {IsoRole, "iso"}, {SizeRole, "size"},
        {OfflineRole, "offline"}, {EditedRole, "edited"},
        {VariantRole, "variant"}, {VariantNameRole, "variantName"}, {VariantOfRole, "variantOf"},
        {StackRole, "stackId"}, {StackPosRole, "stackPos"}, {StackCountRole, "stackCount"},
        {CopyrightRole, "copyright"}, {CreatorRole, "creator"},
        {EditFlagsRole, "editFlags"}, {GpsRole, "hasGps"}, {SidecarStaleRole, "sidecarStale"}
    };
}

const AssetRecord &AssetModel::record(int id) const {
    auto it = m_cache.find(id);
    if (it == m_cache.end()) {
        // Bound the cache: a full scroll through a big catalog must not
        // pin every record in memory.
        if (m_cache.size() > 4000) m_cache.clear();
        it = m_cache.insert(id, m_catalog ? m_catalog->asset(id) : AssetRecord());
    }
    return it.value();
}

QVariant AssetModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_ids.size()) return QVariant();
    const int id = m_ids[index.row()];
    switch (role) {
    case IdRole: return id;
    case SelectedRole: return m_selection && m_selection->contains(id);
    case CurrentRole: return m_current && *m_current == id;
    default: break;
    }
    const AssetRecord &r = record(id);
    switch (role) {
    case FilenameRole: return r.filename;
    case PathRole: return r.path;
    case FormatRole: return r.format;
    case IsRawRole: return r.isRaw;
    case RatingRole: return r.rating;
    case FlagRole: return r.flag;
    case LabelRole: return r.label;
    case CapturedRole: return Metadata::formatCaptured(r.capturedAt, false);
    case CapturedTimeRole: return Metadata::formatCaptured(r.capturedAt, true);
    case ThumbRole: return Thumbnailer::sourceFor(r.path, r.mtime, m_thumbEdge, r.editRev, r.variant);
    case EditedRole: return r.edited;
    case EditFlagsRole: return r.edited ? r.editFlags : 0;
    case GpsRole: return r.hasGps;
    case SidecarStaleRole: {   // touched outside since we last read or wrote it (a stat, no parse)
        const QFileInfo sc(Metadata::existingSidecar(r.path));
        return sc.exists() && sc.lastModified().toSecsSinceEpoch() != r.sidecarSeen;
    }
    case WidthRole: return r.width;
    case HeightRole: return r.height;
    case MakeRole: return r.make;
    case ModelRole: return r.model;
    case LensRole: return r.lens;
    case ExposureRole: return Metadata::formatExposure(r);
    case IsoRole: return r.iso;
    case SizeRole: return Metadata::formatSize(r.size);
    case OfflineRole: return !QFile::exists(r.path);
    case VariantRole: return r.variant;
    case VariantNameRole: return r.variantName;
    case VariantOfRole: return r.variantOf;
    case StackRole: return r.stackId;
    case StackPosRole: return r.stackPos;
    case StackCountRole: return r.stackId ? r.stackCount : 0;
    case CopyrightRole: return r.copyright;
    case CreatorRole: return r.creator;
    default: return QVariant();
    }
}

void AssetModel::setIds(const QVector<int> &ids) {
    if (ids == m_ids) {
        // Metadata may have changed, but the same rows need no reset (which
        // destroys delegates, scroll state and pending preview requests).
        invalidateAll();
        return;
    }
    if (ids.size() > m_ids.size() && std::equal(m_ids.cbegin(), m_ids.cend(), ids.cbegin())) {
        // The common import case appends in capture/filename order. Retain
        // existing delegates, scroll position and in-flight image requests.
        const int before = m_ids.size();
        beginInsertRows(QModelIndex(), before, ids.size() - 1);
        m_ids = ids;
        for (int row = before; row < m_ids.size(); ++row) m_rows.insert(m_ids[row], row);
        m_cache.clear();
        endInsertRows();
        if (before) emit dataChanged(index(0), index(before - 1));
        emit countChanged();
        return;
    }
    beginResetModel();
    m_ids = ids;
    m_rows.clear();
    m_rows.reserve(m_ids.size());
    for (int row = 0; row < m_ids.size(); ++row) m_rows.insert(m_ids[row], row);
    m_cache.clear();
    endResetModel();
    emit countChanged();
}

void AssetModel::invalidate(const QVector<int> &ids) {
    for (int id : ids) m_cache.remove(id);
    notifyRows(ids);
}

void AssetModel::invalidateAll() {
    m_cache.clear();
    if (!m_ids.isEmpty()) emit dataChanged(index(0), index(m_ids.size() - 1));
}

void AssetModel::selectionChanged(const QVector<int> &ids) {
    notifyRows(ids, {SelectedRole, CurrentRole});
}

void AssetModel::notifyRows(const QVector<int> &ids, const QList<int> &roles) {
    QVector<int> rows;
    rows.reserve(qMin(ids.size(), m_ids.size()));
    for (int id : ids) {
        const int row = rowOf(id);
        if (row >= 0) rows.append(row);
    }
    std::sort(rows.begin(), rows.end());
    for (int i = 0; i < rows.size();) {
        const int first = rows[i];
        int last = first;
        while (++i < rows.size() && rows[i] <= last + 1) last = rows[i];
        emit dataChanged(index(first), index(last), roles);
    }
}
