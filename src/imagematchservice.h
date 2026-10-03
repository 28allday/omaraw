// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "imagematch.h"
#include <QObject>
#include <QMutex>
#include <QThreadPool>
#include <QTemporaryDir>
#include <memory>
class EngineService;

class ImageMatchService : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool hasReference READ hasReference NOTIFY changed)
    Q_PROPERTY(bool hasPreview READ hasPreview NOTIFY changed)
    Q_PROPERTY(bool previewApplied READ previewApplied NOTIFY changed)
    Q_PROPERTY(bool canUndoBatch READ canUndoBatch NOTIFY changed)
    Q_PROPERTY(QString referenceName READ referenceName NOTIFY changed)
    Q_PROPERTY(QString referenceSource READ referenceSource NOTIFY changed)
    Q_PROPERTY(QString before READ before NOTIFY changed)
    Q_PROPERTY(QString after READ after NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(QVariantMap options READ options WRITE setOptions NOTIFY changed)
    Q_PROPERTY(QVariantMap report READ report NOTIFY changed)
    Q_PROPERTY(QVariantList batchResults READ batchResults NOTIFY changed)
    Q_PROPERTY(QVariantMap previewArea READ previewArea NOTIFY changed)
    Q_PROPERTY(bool detail READ detail NOTIFY changed)
public:
    explicit ImageMatchService(EngineService *engine);
    ~ImageMatchService() override;
    bool busy() const { return m_busy; }
    bool hasReference() const { return !m_reference.overview.isNull(); }
    bool hasPreview() const { return m_previewReady; }
    bool previewApplied() const { return m_previewApplied; }
    bool canUndoBatch() const { return !m_batchUndo.isEmpty() && !m_busy; }
    QString referenceName() const { return m_referenceName; }
    QString referenceSource() const;
    QString before() const;
    QString after() const;
    QString status() const { return m_status; }
    double progress() const { return m_progress; }
    QVariantMap options() const { return m_options.toMap(); }
    void setOptions(const QVariantMap &options);
    QVariantMap report() const { return m_report; }
    QVariantList batchResults() const { return m_batchResults; }
    QVariantMap previewArea() const { return m_area; }
    bool detail() const { return m_detail; }
    QImage image(const QString &which) const;
    Q_INVOKABLE void setReference(const QString &fileOrUrl);
    Q_INVOKABLE void useCurrent();
    Q_INVOKABLE void clearReference();
    Q_INVOKABLE void preview();
    Q_INVOKABLE void apply();
    Q_INVOKABLE void applySelection(const QVariantList &items);
    Q_INVOKABLE void undoBatch();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void reset();
    Q_INVOKABLE void showFit();
    Q_INVOKABLE void detailAt(double x=.5,double y=.5);
    Q_INVOKABLE void editDraft(const QString &field,double value);
    Q_INVOKABLE void editApplied(const QString &field,double value);
    Q_INVOKABLE bool savePreview(const QString &path, const QString &which = QStringLiteral("after"));
signals:
    void changed();
    void operationFailed(const QString &error);
    void finished(int matched,int failed,bool cancelled);
private:
    void response(int ticket,const QString &action,const QVariantMap &result,const QString &error);
    void request(const QString &action,const QVariantMap &data={});
    void analyseTarget(const QVariantMap &result);
    void next();
    void fail(const QString &error);
    void complete();
    void publishPreview(const QVariantMap &result);
    void invalidate();
    bool ready();
    QString source(const QString &which) const;
    EngineService *m_engine;
    QThreadPool m_pool;
    std::shared_ptr<std::atomic<int>> m_generation=std::make_shared<std::atomic<int>>(0);
    std::shared_ptr<QTemporaryDir> m_referenceFiles;
    ImageMatch::Source m_reference,m_target;
    ImageMatch::Options m_options,m_runOptions;
    ImageMatch::Result m_result;
    QVariantMap m_targetItem,m_referenceItem,m_sourceData,m_report,m_area;
    QVariantList m_items,m_batchUndo,m_batchResults;
    QString m_referenceName,m_referencePath,m_status,m_action;
    int m_ticket=0,m_serial=0,m_done=0,m_failed=0,m_next=0,m_detailPending=0;
    double m_progress=0,m_x=.5,m_y=.5;
    bool m_busy=false,m_previewReady=false,m_previewApplied=false,m_detail=false,m_batch=false,m_undoing=false,m_cancelled=false,m_manualGrain=false;
    mutable QMutex m_mutex;
    QImage m_referenceView,m_beforeView,m_afterView,m_referenceDetail,m_beforeDetail,m_afterDetail;
};
