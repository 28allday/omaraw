// The browser's list of assets: a vector of ids plus a small record cache,
// plus an id-to-row index for batch changes. The grid only materialises
// records it draws. Selection lives in Backend; the model just reports it.
#pragma once

#include "catalog.h"
#include <QAbstractListModel>
#include <QHash>
#include <QSet>
#include <QVector>

class AssetModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
    enum Roles {
        IdRole = Qt::UserRole + 1, FilenameRole, PathRole, FormatRole, IsRawRole,
        RatingRole, FlagRole, LabelRole, CapturedRole, CapturedTimeRole, ThumbRole,
        WidthRole, HeightRole, SelectedRole, CurrentRole, MakeRole, ModelRole,
        LensRole, ExposureRole, IsoRole, SizeRole, OfflineRole, EditedRole,
        VariantRole, VariantNameRole, VariantOfRole, StackRole, StackPosRole, StackCountRole, CopyrightRole, CreatorRole,
        EditFlagsRole, GpsRole, SidecarStaleRole
    };
    Q_ENUM(Roles)

    explicit AssetModel(QObject *parent = nullptr);
    void setCatalog(Catalog *c) { m_catalog = c; }
    void setSelection(const QSet<int> *s, const int *current) { m_selection = s; m_current = current; }
    void setThumbEdge(int e);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Replace the id list (a new source, filter or sort).
    void setIds(const QVector<int> &ids);
    const QVector<int> &ids() const { return m_ids; }
    Q_INVOKABLE int idAt(int row) const { return row >= 0 && row < m_ids.size() ? m_ids[row] : 0; }
    Q_INVOKABLE int rowOf(int id) const { return m_rows.value(id, -1); }
    // The stack a row belongs to (0 = none): lets a strip outline a run of neighbours.
    Q_INVOKABLE int stackIdAt(int row) const { const int id = idAt(row); return id ? record(id).stackId : 0; }

    // After a rating/flag/label change: drop cached records, repaint rows.
    void invalidate(const QVector<int> &ids);
    void invalidateAll();
    void selectionChanged(const QVector<int> &ids);

signals:
    void countChanged();

private:
    const AssetRecord &record(int id) const;
    void notifyRows(const QVector<int> &ids, const QList<int> &roles = {});

    Catalog *m_catalog = nullptr;
    const QSet<int> *m_selection = nullptr;
    const int *m_current = nullptr;
    QVector<int> m_ids;
    QHash<int, int> m_rows;
    mutable QHash<int, AssetRecord> m_cache;
    int m_thumbEdge = 512;
};
