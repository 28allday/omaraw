#include "headless.h"

#include "backend.h"
#include "assetmodel.h"
#include "engineservice.h"
#include "aiservice.h"
#include "denoiseservice.h"
#include "imagematchservice.h"
#include "headlesscapture.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QMetaMethod>
#include <QScopeGuard>
#include <QSettings>
#include <QThread>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QTimer>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <unistd.h>

namespace Headless {
namespace {

// The engine and its libraries print progress straight to stdout, which would
// sit in the middle of the answer and stop anything parsing it. Standard
// output is put aside on the way in and everything written meanwhile goes to
// stderr with the rest of the chatter; the answer is written to what was put
// aside. So stdout carries one JSON object and nothing else.
int g_answer = -1;

int say(const QJsonObject &result, int code) {
    const QByteArray json = QJsonDocument(result).toJson(QJsonDocument::Indented);
    std::fflush(stdout);
    if (g_answer >= 0) {
        qint64 written = 0;
        while (written < json.size()) {
            const ssize_t n = ::write(g_answer, json.constData() + written, size_t(json.size() - written));
            if (n <= 0) break;
            written += n;
        }
    } else {
        QTextStream out(stdout);
        out << QString::fromUtf8(json);
        out.flush();
    }
    return code;
}

QJsonObject problem(const QString &message) {
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

// Waits for `signal`, or gives up. Returns false on the timeout so a caller
// can say so rather than hang a machine that nobody is watching.
template <typename Sender, typename Signal>
bool await(Sender *sender, Signal signal, int timeoutMs) {
    QEventLoop loop;
    bool fired = false;
    QMetaObject::Connection c = QObject::connect(sender, signal, &loop, [&] { fired = true; loop.quit(); });
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    loop.exec();
    QObject::disconnect(c);
    return fired;
}

// The browser's filters, so `list` and `export` select what the Library would
// show under the same settings rather than inventing a second set of rules.
QString applyFilters(const QCommandLineParser &parser, Backend &backend) {
    backend.setSource(QStringLiteral("all"));
    if (parser.isSet(QStringLiteral("rating"))) {
        bool ok = false;
        const int rating = parser.value(QStringLiteral("rating")).toInt(&ok);
        if (!ok || rating < 0 || rating > 5) return QStringLiteral("--rating takes 0 to 5");
        backend.setFilterRating(rating);
    }
    if (parser.isSet(QStringLiteral("flag"))) {
        const QString flag = parser.value(QStringLiteral("flag"));
        if (flag != QLatin1String("pick") && flag != QLatin1String("reject") && flag != QLatin1String("none"))
            return QStringLiteral("--flag takes pick, reject or none");
        // "none" is the Library's "unflagged": neither picked nor rejected.
        backend.setFilterFlag(flag == QLatin1String("none") ? QStringLiteral("unflagged") : flag);
    }
    if (parser.isSet(QStringLiteral("label"))) backend.setFilterLabel(parser.value(QStringLiteral("label")));
    if (parser.isSet(QStringLiteral("text"))) backend.setFilterText(parser.value(QStringLiteral("text")));
    return QString();
}

QJsonObject photoJson(const QVariantMap &info) {
    QJsonObject o;
    for (const char *key : {"id", "filename", "path", "format", "capturedIso", "dimensions", "rating", "flag", "label", "autoTags", "autoTagScan"})
        if (info.contains(QLatin1String(key))) o.insert(QLatin1String(key), QJsonValue::fromVariant(info.value(QLatin1String(key))));
    return o;
}

int doList(const QCommandLineParser &parser, Backend &backend) {
    const QString bad = applyFilters(parser, backend);
    if (!bad.isEmpty()) return say(problem(bad), 2);
    QJsonArray photos;
    const QVector<int> ids = backend.model()->ids();
    for (int id : ids) photos.append(photoJson(backend.info(id)));
    return say(QJsonObject{{QStringLiteral("ok"), true},
                           {QStringLiteral("verb"), QStringLiteral("list")},
                           {QStringLiteral("catalog"), backend.catalogPath()},
                           {QStringLiteral("count"), photos.size()},
                           {QStringLiteral("photos"), photos}}, 0);
}

bool settle(Backend &backend, EngineService &engine, int timeoutMs);

int doImport(const QCommandLineParser &parser, Backend &backend, EngineService &engine, const QStringList &rest) {
    if (rest.isEmpty()) return say(problem(QStringLiteral("import needs a folder")), 2);
    const QFileInfo folder(rest.first());
    if (!folder.isDir()) return say(problem(QStringLiteral("%1 is not a folder").arg(rest.first())), 2);

    // The window's own choices (skip duplicates, metadata…) apply, but where
    // files go is only ever what this command says: a destination or second
    // copy left from the window must not receive a command-line import.
    QVariantMap options = backend.importOptions();
    options[QStringLiteral("autoTag")] = parser.isSet(QStringLiteral("auto-tag"));
    options.remove(QStringLiteral("destination"));
    options.remove(QStringLiteral("backup"));
    options.remove(QStringLiteral("eject"));
    options[QStringLiteral("mode")] = QStringLiteral("add");
    // Nor does a command-line import change what the window offers next time.
    const QVariant savedOptions = QSettings().value(QStringLiteral("import/options")), savedSmart = QSettings().value(QStringLiteral("import/smartPreviews"));
    const bool savedTagEnabled=backend.autoTagEnabled(), savedTagPaused=backend.autoTagPaused();
    const QVariant savedEnabledSetting=QSettings().value("autotag/enabled"), savedPausedSetting=QSettings().value("autotag/paused");
    const auto restoreSettings = qScopeGuard([&] {
        backend.setAutoTagEnabled(savedTagEnabled); backend.setAutoTagPaused(savedTagPaused);
        if(savedEnabledSetting.isValid()) QSettings().setValue("autotag/enabled",savedEnabledSetting); else QSettings().remove("autotag/enabled");
        if(savedPausedSetting.isValid()) QSettings().setValue("autotag/paused",savedPausedSetting); else QSettings().remove("autotag/paused");
        if (savedOptions.isValid()) QSettings().setValue(QStringLiteral("import/options"), savedOptions); else QSettings().remove(QStringLiteral("import/options"));
        if (savedSmart.isValid()) QSettings().setValue(QStringLiteral("import/smartPreviews"), savedSmart); else QSettings().remove(QStringLiteral("import/smartPreviews"));
    });
    if (parser.isSet(QStringLiteral("mode"))) {
        const QString mode = parser.value(QStringLiteral("mode"));
        if (mode != QLatin1String("add") && mode != QLatin1String("copy")
            && mode != QLatin1String("move") && mode != QLatin1String("verify"))
            return say(problem(QStringLiteral("--mode takes add, copy, move or verify")), 2);
        options[QStringLiteral("mode")] = mode;
    }
    if (parser.isSet(QStringLiteral("dest"))) options[QStringLiteral("destination")] = QFileInfo(parser.value(QStringLiteral("dest"))).absoluteFilePath();
    const QString mode = options.value(QStringLiteral("mode")).toString();
    if (mode != QLatin1String("add") && options.value(QStringLiteral("destination")).toString().isEmpty())
        return say(problem(QStringLiteral("--mode %1 needs --dest").arg(mode)), 2);
    // A destination that is not there yet is made, as a new job's folder usually is.
    if (mode != QLatin1String("add") && !QDir().mkpath(options.value(QStringLiteral("destination")).toString()))
        return say(problem(QStringLiteral("cannot create %1").arg(options.value(QStringLiteral("destination")).toString())), 1);

    int imported = 0, skipped = 0;
    // Disconnected on the way out: the lambda holds this function's locals.
    const QMetaObject::Connection counted = QObject::connect(&backend, &Backend::importFinished, &backend, [&](int i, int s) { imported = i; skipped = s; });
    const auto disconnect = qScopeGuard([&] { QObject::disconnect(counted); });
    backend.importFolderWith(folder.absoluteFilePath(), options);
    // Refused before it started (a destination it cannot write, say): the
    // reason is in the status line, and no finish will ever come.
    if (!backend.importing() && !imported && !skipped) return say(problem(backend.statusMessage()), 1);
    // A shoot off a card can take a while; a whole day of it should not.
    if (!await(&backend, &Backend::importFinished, 6 * 60 * 60 * 1000)) {
        backend.cancelImport();
        return say(problem(QStringLiteral("the import did not finish")), 1);
    }
    if (parser.isSet(QStringLiteral("auto-tag"))) {
        const bool paused=backend.autoTagPaused();
        const auto restorePause=qScopeGuard([&] { backend.setAutoTagPaused(paused); });
        backend.setAutoTagPaused(false);
        QElapsedTimer tagged; tagged.start();
        while ((backend.autoTagPending() || backend.autoTagBusy()) && !backend.autoTagPaused() && tagged.elapsed()<6*60*60*1000)
            await(&backend,&Backend::autoTagsChanged,1000);
        if (backend.autoTagPending()) return say(problem(QStringLiteral("Photos imported; auto-tagging did not finish: %1").arg(backend.autoTagStatus())),1);
    }
    // Edits that came in the photos' sidecars are being applied: they land
    // before the answer does.
    if (engine.available() && !settle(backend, engine, 10 * 60 * 1000)) return say(problem(QStringLiteral("the sidecars' edits did not finish")), 1);
    // A copy that failed or did not verify, a second copy that failed, or
    // a catalog error is a failed import for a script, whatever came in.
    const QVariantMap result = backend.lastImportResult();
    const QString error = result.value(QStringLiteral("error")).toString();
    const int failedFiles = result.value(QStringLiteral("failed")).toInt(), backupFailed = result.value(QStringLiteral("backupFailed")).toInt();
    const int tagFailed = parser.isSet(QStringLiteral("auto-tag")) ? result.value(QStringLiteral("autoTagFailed")).toInt() : 0;
    const bool ok = error.isEmpty() && !failedFiles && !backupFailed && !tagFailed;
    QJsonObject out{{QStringLiteral("ok"), ok},
                    {QStringLiteral("verb"), QStringLiteral("import")},
                    {QStringLiteral("catalog"), backend.catalogPath()},
                    {QStringLiteral("folder"), folder.absoluteFilePath()},
                    {QStringLiteral("mode"), mode},
                    {QStringLiteral("imported"), imported},
                    {QStringLiteral("skipped"), skipped},
                    {QStringLiteral("duplicates"), result.value(QStringLiteral("duplicates")).toInt()},
                    {QStringLiteral("failed"), failedFiles},
                    {QStringLiteral("backupFailed"), backupFailed},
                    {QStringLiteral("autoTagFailed"), tagFailed},
                    {QStringLiteral("message"), backend.statusMessage()}};
    if (!error.isEmpty()) out.insert(QStringLiteral("error"), error);
    else if (tagFailed) out.insert(QStringLiteral("error"), QStringLiteral("Photos imported; %1 auto-tag scans failed").arg(tagFailed));
    else if (!ok) out.insert(QStringLiteral("error"), QStringLiteral("%1 files did not copy or verify, %2 second copies failed").arg(failedFiles).arg(backupFailed));
    return say(out, ok ? 0 : 1);
}

int doExport(const QCommandLineParser &parser, Backend &backend, EngineService &engine, const QStringList &rest) {
    if (rest.isEmpty()) return say(problem(QStringLiteral("export needs an output folder")), 2);
    const QString folder = QFileInfo(rest.first()).absoluteFilePath();
    if (!QDir().mkpath(folder)) return say(problem(QStringLiteral("cannot create %1").arg(folder)), 1);
    if (!engine.available()) return say(problem(QStringLiteral("the develop engine is not available, so nothing can be rendered")), 1);

    // The queue is saved between sessions. Exporting somebody's unfinished
    // work because it happened to be pending would be a surprise, so say so
    // and stop rather than quietly picking it up.
    if (!engine.exportQueue().isEmpty())
        return say(problem(QStringLiteral("%1 rows are already in the export queue; clear it in the application first")
                               .arg(engine.exportQueue().size())), 1);
    // A saved queue that could not be read, or cannot be written, pauses
    // every export: nothing would ever finish.
    if (!engine.exportQueueError().isEmpty())
        return say(problem(QStringLiteral("the export queue cannot be used: %1").arg(engine.exportQueueError())), 1);

    const QString bad = applyFilters(parser, backend);
    if (!bad.isEmpty()) return say(problem(bad), 2);
    const QVariantList items = backend.exportItems(true);
    if (items.isEmpty()) return say(problem(QStringLiteral("no photos match")), 1);

    bool ok = false;
    const int quality = parser.isSet(QStringLiteral("quality")) ? parser.value(QStringLiteral("quality")).toInt(&ok) : 90;
    if (parser.isSet(QStringLiteral("quality")) && (!ok || quality < 1 || quality > 100))
        return say(problem(QStringLiteral("--quality takes 1 to 100")), 2);
    const int maxEdge = parser.isSet(QStringLiteral("max-edge")) ? parser.value(QStringLiteral("max-edge")).toInt(&ok) : 0;
    if (parser.isSet(QStringLiteral("max-edge")) && (!ok || maxEdge < 1)) return say(problem(QStringLiteral("--max-edge takes a size in pixels")), 2);
    const QString format = parser.isSet(QStringLiteral("format")) ? parser.value(QStringLiteral("format")) : QStringLiteral("jpeg");
    // Only what this engine can write; a queued row it cannot would fail
    // late, and a journal holding it could not be read back.
    QStringList formats; bool deep = false;
    // The engine reports what it can write a moment after it starts.
    QElapsedTimer waited; waited.start();
    while (engine.exportFormats().isEmpty() && waited.elapsed() < 60000) await(&engine, &EngineService::exportQueueChanged, 1000);
    for (const QVariant &f : engine.exportFormats()) {
        const QVariantMap m = f.toMap();
        formats << m.value(QStringLiteral("id")).toString();
        if (m.value(QStringLiteral("id")).toString() == format) deep = m.value(QStringLiteral("bpp")).toBool();
    }
    if (!formats.contains(format)) return say(problem(QStringLiteral("--format takes one of: %1").arg(formats.join(QStringLiteral(", ")))), 2);
    int bits = 8;
    if (parser.isSet(QStringLiteral("bits"))) {
        bits = parser.value(QStringLiteral("bits")).toInt(&ok);
        if (!ok || (bits != 8 && bits != 16)) return say(problem(QStringLiteral("--bits takes 8 or 16")), 2);
        if (bits == 16 && !deep) return say(problem(QStringLiteral("%1 is 8 bits only; 16 bits needs tiff, png or psd").arg(format)), 2);
    }
    // The same metadata as the window's export: camera details and keywords,
    // but not where the photo was taken or its Develop history, unless asked.
    const int meta = EngineService::metaFlagsFor(true, parser.isSet(QStringLiteral("location")), true, parser.isSet(QStringLiteral("history")));
    const QString suffix = parser.isSet(QStringLiteral("suffix")) ? parser.value(QStringLiteral("suffix")) : QString();
    // 0 asks the engine for the photograph's own size; the browser's own
    // default long edge is what the interface would use.
    QVariantMap options;
    options[QStringLiteral("mode")] = maxEdge > 0 ? QStringLiteral("long") : QStringLiteral("none");
    if (maxEdge > 0) options[QStringLiteral("value")] = maxEdge;

    // Listening before queueing: a batch whose every row fails at once
    // finishes inside the call.
    bool finished = false;
    const auto finishedConn = QObject::connect(&engine, &EngineService::exportBatchFinished, &engine, [&finished] { finished = true; });
    const auto disconnectFinished = qScopeGuard([&] { QObject::disconnect(finishedConn); });
    engine.exportBatchItems(items, folder, maxEdge > 0 ? maxEdge : 0, quality, suffix, format, bits, meta, {}, options);
    // Until it finishes, or stops for good: a queue that cannot be journalled
    // pauses and would otherwise be waited on for the whole twelve hours.
    QElapsedTimer running; running.start();
    while (!finished) {
        if (!engine.exporting() && (engine.exportPaused() || engine.exportQueue().isEmpty()))
            return say(problem(engine.exportQueueError().isEmpty() ? engine.status() : engine.exportQueueError()), 1);
        if (running.elapsed() > 12LL * 60 * 60 * 1000) {
            engine.cancelExport();
            return say(problem(QStringLiteral("the export did not finish")), 1);
        }
        await(&engine, &EngineService::exportQueueChanged, 1000);
    }

    QJsonArray rows;
    int done = 0, failed = 0;
    for (const QVariant &v : engine.exportQueue()) {
        const QVariantMap row = v.toMap();
        const QString status = row.value(QStringLiteral("status")).toString();
        if (status == QLatin1String("done")) ++done; else ++failed;
        rows.append(QJsonObject{{QStringLiteral("source"), row.value(QStringLiteral("file")).toString()},
                                {QStringLiteral("out"), row.value(QStringLiteral("out")).toString()},
                                {QStringLiteral("status"), status},
                                {QStringLiteral("error"), row.value(QStringLiteral("error")).toString()}});
    }
    engine.clearExportQueue();
    return say(QJsonObject{{QStringLiteral("ok"), failed == 0},
                           {QStringLiteral("verb"), QStringLiteral("export")},
                           {QStringLiteral("catalog"), backend.catalogPath()},
                           {QStringLiteral("folder"), folder},
                           {QStringLiteral("format"), format},
                           {QStringLiteral("done"), done},
                           {QStringLiteral("failed"), failed},
                           {QStringLiteral("files"), rows}}, failed == 0 ? 0 : 1);
}


// ── ops / inspect / apply: Backend ("catalog."), EngineService ("engine.")
// and its AiService ("ai.") by name. The interface uses these same objects, so
// what a script can do here is what a person can do there, and a new control
// arrives in both at once. ─────────────────────────────────────────────────

struct Target { const char *prefix; QObject *object; };
QList<Target> targetsOf(Backend &backend, EngineService &engine, HeadlessCapture &capture) {
    return {{"catalog", &backend}, {"engine", &engine}, {"ai", engine.ai()}, {"denoise", engine.denoise()}, {"match", engine.imageMatch()}, {"capture", &capture}};
}

// Refused, whatever the ops file says. Culling — ratings, flags and colour
// labels — is a human job: the command line reads those marks and never
// makes them. The rest only mean something with a window on the screen, or
// would pull the photo out from under the run ("photo" opens one instead).
QString refusal(const QString &op) {
    static const QStringList culling{
        QStringLiteral("catalog.setRating"), QStringLiteral("catalog.setFlag"), QStringLiteral("catalog.setLabel"),
        QStringLiteral("catalog.toggleFlag"), QStringLiteral("catalog.toggleLabel"),
        // Reads a sidecar's rating and label back into the catalog.
        QStringLiteral("catalog.applySidecar")};
    static const QStringList windowOnly{
        QStringLiteral("catalog.revealBackups"), QStringLiteral("catalog.revealDocumentation"), QStringLiteral("catalog.revealInFileManager"),
        QStringLiteral("catalog.revealLicences"), QStringLiteral("catalog.revealPath"), QStringLiteral("catalog.openCatalogWindow"),
        QStringLiteral("catalog.openInMap"), QStringLiteral("catalog.switchCatalog"), QStringLiteral("catalog.registerShortcuts"),
        QStringLiteral("catalog.setShortcut"), QStringLiteral("catalog.resetShortcut"), QStringLiteral("catalog.resetShortcuts"),
        QStringLiteral("engine.setViewerActive"), QStringLiteral("engine.setDetailView"), QStringLiteral("engine.setOriginalDetailView"),
        QStringLiteral("engine.setPrimaryGradeEditing"), QStringLiteral("engine.loadCurveHistogram"), QStringLiteral("engine.setViewSize")};
    if (culling.contains(op)) return QStringLiteral("%1 is refused: ratings, flags and colour labels are read here, never made — culling stays in the Library").arg(op);
    if (windowOnly.contains(op)) return QStringLiteral("%1 only means something with a window open").arg(op);
    if (op == QLatin1String("engine.load")) return QStringLiteral("engine.load is not used directly: give \"photo\" on the operation (or --photo) to open one");
    return QString();
}

bool usable(const QMetaMethod &m) {
    if (m.access() != QMetaMethod::Public) return false;
    if (m.methodType() != QMetaMethod::Method && m.methodType() != QMetaMethod::Slot) return false;
    for (int i = 0; i < m.parameterCount(); ++i) {
        const QMetaType t(m.parameterType(i));
        if (!t.isValid() || (t.flags() & QMetaType::PointerToQObject)) return false;
    }
    return true;
}

QList<QMetaMethod> methodsOf(QObject *o) {
    QList<QMetaMethod> out;
    const QMetaObject *mo = o->metaObject();
    for (int i = QObject::staticMetaObject.methodCount(); i < mo->methodCount(); ++i)
        if (usable(mo->method(i))) out << mo->method(i);
    return out;
}

QJsonObject describe(const Target &t, const QMetaMethod &m) {
    QJsonArray args;
    const QList<QByteArray> names = m.parameterNames();
    for (int i = 0; i < m.parameterCount(); ++i)
        args.append(QJsonObject{{QStringLiteral("name"), QString::fromLatin1(names.value(i))},
                                {QStringLiteral("type"), QString::fromLatin1(m.parameterTypeName(i))}});
    return QJsonObject{{QStringLiteral("op"), QStringLiteral("%1.%2").arg(QLatin1String(t.prefix), QString::fromLatin1(m.name()))},
                       {QStringLiteral("args"), args},
                       {QStringLiteral("returns"), QString::fromLatin1(m.typeName())}};
}

// Settled: nothing importing, rendering, exporting or writing. Asked a few
// times in a row, because a queued step can land just after a quiet moment.
// The engine's worker is also drained each time round: some jobs (a single
// export, for one) are handed to it without raising a busy flag, and the
// worker runs one job at a time, in order, so a job that has come back means
// everything queued before it is done.
bool settle(Backend &backend, EngineService &engine, int timeoutMs = 10 * 60 * 1000) {
    QElapsedTimer clock; clock.start();
    int calm = 0;
    while (clock.elapsed() < timeoutMs) {
        if (engine.available()) engine.drainWorker();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        const bool idle = !backend.importing() && !static_cast<AiService *>(engine.ai())->busy()
            && !static_cast<DenoiseService *>(engine.denoise())->busy()
            && !static_cast<ImageMatchService *>(engine.imageMatch())->busy()
            && (!backend.autoTagEnabled() || backend.autoTagPaused() || (!backend.autoTagBusy() && !backend.autoTagPending()))
            && (!engine.available() || ((engine.imageId() < 0 || engine.maintenanceReady()) && !engine.exporting()));
        calm = idle ? calm + 1 : 0;
        if (calm >= 5) return true;
        QThread::msleep(10);
    }
    return false;
}

// Every photo, whatever the Library was last showing.
void showEverything(Backend &backend) {
    backend.setSource(QStringLiteral("all"));
    backend.setFilterRating(0); backend.setFilterFlag(QString()); backend.setFilterLabel(QString()); backend.setFilterText(QString());
    backend.setFilterExtra({});
    backend.setCollapseStacks(false);
}

int photoId(Backend &backend, const QJsonValue &ref, QString *error) {
    if (ref.isDouble()) {
        const int id = ref.toInt();
        if (backend.info(id).isEmpty()) { *error = QStringLiteral("there is no photo %1 in this catalog").arg(id); return 0; }
        return id;
    }
    const QString wanted = QFileInfo(ref.toString()).absoluteFilePath();
    showEverything(backend);
    for (int id : backend.model()->ids()) {
        const QVariantMap info = backend.info(id);
        if (info.value(QStringLiteral("variant")).toInt() == 0 && QFileInfo(info.value(QStringLiteral("path")).toString()).absoluteFilePath() == wanted) return id;
    }
    *error = QStringLiteral("%1 is not in this catalog").arg(ref.toString());
    return 0;
}

// Opens a photo in the engine the way Develop does, and waits for it.
bool openPhoto(Backend &backend, EngineService &engine, int id, QString *error) {
    if (!engine.available()) { *error = QStringLiteral("the develop engine is not available"); return false; }
    const QVariantMap info = backend.info(id);
    const QString path = info.value(QStringLiteral("path")).toString();
    const int variant = info.value(QStringLiteral("variant")).toInt();
    backend.select(id);
    if (engine.imagePath() == path && engine.imageVariant() == variant && engine.imageId() >= 0) return settle(backend, engine);
    engine.setViewSize(2048, 2048);
    engine.load(path, variant);
    QElapsedTimer clock; clock.start();
    while (clock.elapsed() < 180000 && !(engine.imagePath() == path && engine.imageId() >= 0 && engine.maintenanceReady())) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(10);
        // A file that cannot be opened leaves the engine idle with no image:
        // say so now rather than at the end of the wait.
        if (clock.elapsed() > 3000 && engine.imageId() < 0 && !engine.busy()) break;
    }
    if (engine.imageId() < 0 || engine.imagePath() != path) { *error = QStringLiteral("photo %1 did not open: %2").arg(id).arg(engine.status()); return false; }
    return settle(backend, engine);
}

QJsonValue json(const QVariant &v) {
    const QJsonValue j = QJsonValue::fromVariant(v);
    if (!j.isNull() || !v.isValid()) return j;
    return v.canConvert<QString>() ? QJsonValue(v.toString()) : QJsonValue();
}

// The develop controls as the panel shows them. `all` false keeps the ones
// moved away from their default on an adjustment that is switched on.
QJsonArray paramsJson(const QVariantList &params, bool all) {
    QJsonArray out;
    for (const QVariant &v : params) {
        const QVariantMap p = v.toMap();
        const bool changed = p.value(QStringLiteral("enabled")).toBool() && p.contains(QStringLiteral("def"))
                             && qAbs(p.value(QStringLiteral("value")).toDouble() - p.value(QStringLiteral("def")).toDouble()) > 1e-6;
        if (!all && !changed) continue;
        QJsonObject o;
        for (const char *key : {"group", "op", "field", "label", "value", "def", "min", "max", "enabled", "type", "decimals", "suffix", "text", "options", "channel"})
            if (p.contains(QLatin1String(key))) o.insert(QLatin1String(key), json(p.value(QLatin1String(key))));
        out.append(o);
    }
    return out;
}

QJsonObject propertiesJson(QObject *o, const QStringList &skip) {
    QJsonObject out;
    const QMetaObject *mo = o->metaObject();
    for (int i = QObject::staticMetaObject.propertyCount(); i < mo->propertyCount(); ++i) {
        const QMetaProperty p = mo->property(i);
        if (!p.isReadable() || (p.metaType().flags() & QMetaType::PointerToQObject) || skip.contains(QLatin1String(p.name()))) continue;
        out.insert(QLatin1String(p.name()), json(p.read(o)));
    }
    return out;
}

int doOps(const QCommandLineParser &parser, Backend &backend, EngineService &engine, HeadlessCapture &capture) {
    const QString filter = parser.value(QStringLiteral("filter")).toLower();
    const auto wanted = [&](const QString &text) { return filter.isEmpty() || text.toLower().contains(filter); };
    QJsonArray ops, properties, params;
    for (const Target &t : targetsOf(backend, engine, capture)) {
        QSet<QByteArray> seen;
        for (const QMetaMethod &m : methodsOf(t.object)) {
            const QString name = QStringLiteral("%1.%2").arg(QLatin1String(t.prefix), QString::fromLatin1(m.name()));
            if (!refusal(name).isEmpty() || !wanted(name)) continue;
            // Default arguments appear as shorter copies: list the full one.
            const QByteArray key = m.name();
            if (seen.contains(key)) continue;
            QMetaMethod longest = m;
            for (const QMetaMethod &o : methodsOf(t.object)) if (o.name() == key && o.parameterCount() > longest.parameterCount()) longest = o;
            seen.insert(key);
            QJsonObject d = describe(t, longest);
            int fewest = longest.parameterCount();
            for (const QMetaMethod &o : methodsOf(t.object)) if (o.name() == key) fewest = qMin(fewest, o.parameterCount());
            d.insert(QStringLiteral("required"), fewest);
            ops.append(d);
        }
        const QMetaObject *mo = t.object->metaObject();
        for (int i = QObject::staticMetaObject.propertyCount(); i < mo->propertyCount(); ++i) {
            const QMetaProperty p = mo->property(i);
            if (p.metaType().flags() & QMetaType::PointerToQObject) continue;
            const QString name = QStringLiteral("%1.%2").arg(QLatin1String(t.prefix), QLatin1String(p.name()));
            if (!wanted(name)) continue;
            properties.append(QJsonObject{{QStringLiteral("op"), name}, {QStringLiteral("type"), QString::fromLatin1(p.typeName())}, {QStringLiteral("writable"), p.isWritable()}});
        }
    }
    for (const QVariant &v : EngineService::curatedParams()) {
        const QVariantMap p = v.toMap();
        const QString text = p.value(QStringLiteral("op")).toString() + QLatin1Char('.') + p.value(QStringLiteral("field")).toString() + QLatin1Char(' ') + p.value(QStringLiteral("label")).toString();
        if (!wanted(text)) continue;
        QJsonObject o;
        for (const char *key : {"group", "op", "field", "label", "suffix", "kind", "channel"}) if (p.contains(QLatin1String(key))) o.insert(QLatin1String(key), json(p.value(QLatin1String(key))));
        params.append(o);
    }
    return say(QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("verb"), QStringLiteral("ops")},
                           {QStringLiteral("count"), ops.size()}, {QStringLiteral("ops"), ops},
                           {QStringLiteral("properties"), properties}, {QStringLiteral("params"), params},
                           {QStringLiteral("note"), QStringLiteral("Develop controls are set with engine.setParam(op, field, value); inspect --photo gives each one's range, default and current value.")}}, 0);
}

int doInspect(const QCommandLineParser &parser, Backend &backend, EngineService &engine, HeadlessCapture &capture) {
    const bool full = parser.isSet(QStringLiteral("full"));
    QJsonObject out{{QStringLiteral("ok"), true}, {QStringLiteral("verb"), QStringLiteral("inspect")}, {QStringLiteral("catalog"), backend.catalogPath()}};
    if (!parser.isSet(QStringLiteral("photo"))) {
        showEverything(backend);
        QJsonArray photos;
        for (int id : backend.model()->ids()) {
            const QVariantMap info = backend.info(id);
            QJsonObject o;
            for (const char *key : {"id", "filename", "path", "variant", "variantOf", "format", "isRaw", "captured", "camera", "lens", "rating", "flag", "label", "title", "offline"})
                if (info.contains(QLatin1String(key))) o.insert(QLatin1String(key), json(info.value(QLatin1String(key))));
            photos.append(o);
        }
        QJsonArray presets;
        for (const QVariant &v : backend.presets()) {
            const QVariantMap p = v.toMap();
            QJsonObject o{{QStringLiteral("name"), p.value(QStringLiteral("name")).toString()}, {QStringLiteral("category"), p.value(QStringLiteral("category")).toString()}};
            if (full) o.insert(QStringLiteral("values"), json(p.value(QStringLiteral("values"))));
            presets.append(o);
        }
        out.insert(QStringLiteral("count"), photos.size());
        out.insert(QStringLiteral("photos"), photos);
        out.insert(QStringLiteral("albums"), json(backend.albums()));
        out.insert(QStringLiteral("smartAlbums"), json(backend.smartAlbums()));
        out.insert(QStringLiteral("keywords"), json(backend.allKeywords()));
        out.insert(QStringLiteral("presets"), presets);
        out.insert(QStringLiteral("ai"), propertiesJson(engine.ai(), {}));
        out.insert(QStringLiteral("denoise"), propertiesJson(engine.denoise(), {}));
        out.insert(QStringLiteral("match"), propertiesJson(engine.imageMatch(), {}));
        out.insert(QStringLiteral("capture"), QJsonObject::fromVariantMap(capture.state()));
        if (full) out.insert(QStringLiteral("state"), propertiesJson(&backend, {QStringLiteral("shortcutOverrides")}));
        return say(out, 0);
    }
    QString error;
    const QString ref = parser.value(QStringLiteral("photo"));
    bool number = false; const int asNumber = ref.toInt(&number);
    const int id = photoId(backend, number ? QJsonValue(asNumber) : QJsonValue(ref), &error);
    if (!id) return say(problem(error), 1);
    if (!openPhoto(backend, engine, id, &error)) return say(problem(error), 1);
    QJsonObject photo;
    const QVariantMap info = backend.info(id);
    for (auto it = info.cbegin(); it != info.cend(); ++it) if (it.key() != QLatin1String("thumb")) photo.insert(it.key(), json(it.value()));
    photo.insert(QStringLiteral("keywords"), json(backend.currentKeywords()));
    out.insert(QStringLiteral("photo"), photo);
    QJsonArray snapshots;
    for (const QVariant &v : backend.snapshots(id)) {
        QVariantMap m = v.toMap();
        if (!full) m.remove(QStringLiteral("values"));
        snapshots.append(json(m));
    }
    out.insert(QStringLiteral("snapshots"), snapshots);
    out.insert(QStringLiteral("variants"), json(backend.variantsOf(id)));
    QJsonArray history;
    for (const QVariant &v : engine.history()) history.append(json(v));
    QJsonObject develop{
        {QStringLiteral("params"), paramsJson(engine.params(), full)},
        {QStringLiteral("paramsShown"), full ? QStringLiteral("all") : QStringLiteral("moved from their default (--full for all)")},
        {QStringLiteral("curve"), json(engine.curve())}, {QStringLiteral("parametric"), json(engine.parametric())},
        {QStringLiteral("zones"), json(engine.zones())}, {QStringLiteral("locals"), json(engine.locals())},
        {QStringLiteral("spots"), json(engine.spots())}, {QStringLiteral("crop"), json(engine.crop())},
        {QStringLiteral("lens"), json(engine.lensProfile())},
        {QStringLiteral("history"), history}, {QStringLiteral("historyEnd"), engine.historyEnd()},
        {QStringLiteral("render"), QJsonObject{{QStringLiteral("width"), engine.renderWidth()}, {QStringLiteral("height"), engine.renderHeight()},
                                               {QStringLiteral("clippedHighlights"), engine.clippedHighlights()}, {QStringLiteral("clippedShadows"), engine.clippedShadows()}}}};
    if (full) develop.insert(QStringLiteral("state"), propertiesJson(&engine, {QStringLiteral("histogram"), QStringLiteral("curveHistogram"), QStringLiteral("detailTiles"), QStringLiteral("originalDetailTiles"), QStringLiteral("params"), QStringLiteral("history")}));
    develop.insert(QStringLiteral("cameraProfile"), json(engine.cameraProfileState()));
    develop.insert(QStringLiteral("creativeProfiles"), json(engine.creativeProfiles()));
    develop.insert(QStringLiteral("ai"), propertiesJson(engine.ai(), {}));
    develop.insert(QStringLiteral("denoise"), propertiesJson(engine.denoise(), {}));
    develop.insert(QStringLiteral("match"), propertiesJson(engine.imageMatch(), {}));
    out.insert(QStringLiteral("develop"), develop);
    return say(out, 0);
}

// One operation, resolved against its target: the overload whose arguments
// match what was given, each converted to the type the method takes.
struct Call { QObject *object = nullptr; QMetaMethod method; QMetaProperty property; bool isProperty = false; QVariantList args; QString name; };

bool resolve(Backend &backend, EngineService &engine, HeadlessCapture &capture, const QJsonObject &op, Call *call, QString *error) {
    QString name = op.value(QStringLiteral("op")).toString().trimmed();
    if (name.isEmpty()) { *error = QStringLiteral("an operation needs \"op\""); return false; }
    const auto targets = targetsOf(backend, engine, capture);
    if (!name.contains(QLatin1Char('.'))) {
        // Unprefixed: fine when only one side has it.
        QStringList found;
        for (const Target &t : targets) {
            bool has = false;
            for (const QMetaMethod &m : methodsOf(t.object)) if (QString::fromLatin1(m.name()) == name) has = true;
            if (t.object->metaObject()->indexOfProperty(name.toLatin1().constData()) >= 0) has = true;
            if (has) found << QStringLiteral("%1.%2").arg(QLatin1String(t.prefix), name);
        }
        if (found.size() != 1) { *error = found.isEmpty() ? QStringLiteral("%1 is not an operation (see: omaraw ops --filter %1)").arg(name)
                                                           : QStringLiteral("%1 is ambiguous: %2").arg(name, found.join(QStringLiteral(", "))); return false; }
        name = found.first();
    }
    const QString why = refusal(name);
    if (!why.isEmpty()) { *error = why; return false; }
    const QString prefix = name.section(QLatin1Char('.'), 0, 0), member = name.section(QLatin1Char('.'), 1);
    QObject *object = nullptr;
    for (const auto &target : targets) if (prefix == QLatin1String(target.prefix)) object = target.object;
    if (!object) { *error = QStringLiteral("%1: the prefix is catalog., engine., ai., denoise., match. or capture.").arg(name); return false; }
    call->object = object; call->name = name;

    QList<QMetaMethod> overloads;
    for (const QMetaMethod &m : methodsOf(object)) if (QString::fromLatin1(m.name()) == member) overloads << m;
    if (overloads.isEmpty()) {
        const int index = object->metaObject()->indexOfProperty(member.toLatin1().constData());
        if (index < 0 || index < QObject::staticMetaObject.propertyCount()) { *error = QStringLiteral("%1 is not an operation (see: omaraw ops --filter %2)").arg(name, member); return false; }
        const QMetaProperty p = object->metaObject()->property(index);
        if (!p.isReadable() || (p.metaType().flags() & QMetaType::PointerToQObject)) { *error = QStringLiteral("%1 is not a JSON setting; use its own namespace (ai. for local AI tools)").arg(name); return false; }
        if (!op.contains(QStringLiteral("value"))) {
            call->isProperty = true; call->property = p; return true;
        }
        if (!p.isWritable()) { *error = QStringLiteral("%1 can be read but not set").arg(name); return false; }
        QVariant v = op.value(QStringLiteral("value")).toVariant();
        if (!v.convert(p.metaType())) { *error = QStringLiteral("%1 takes a %2").arg(name, QString::fromLatin1(p.typeName())); return false; }
        call->isProperty = true; call->property = p; call->args = {v};
        return true;
    }
    const QJsonValue given = op.value(QStringLiteral("args"));
    QVariantList raw;
    const QMetaMethod *chosen = nullptr;
    if (given.isObject()) {
        const QJsonObject named = given.toObject();
        for (const QMetaMethod &m : overloads) {
            const QList<QByteArray> names = m.parameterNames();
            if (names.size() != named.size()) continue;
            bool all = true;
            for (const QByteArray &n : names) if (!named.contains(QString::fromLatin1(n))) all = false;
            if (!all) continue;
            chosen = &m;
            for (const QByteArray &n : names) raw << named.value(QString::fromLatin1(n)).toVariant();
            break;
        }
    } else {
        const QJsonArray positional = given.isArray() ? given.toArray() : given.isUndefined() || given.isNull() ? QJsonArray() : QJsonArray{given};
        for (const QMetaMethod &m : overloads) if (m.parameterCount() == positional.size()) { chosen = &m; break; }
        for (const QJsonValue &v : positional) raw << v.toVariant();
    }
    if (!chosen) {
        QStringList shapes;
        for (const QMetaMethod &m : overloads) shapes << QString::fromLatin1(m.methodSignature());
        *error = QStringLiteral("%1 takes %2").arg(name, shapes.join(QStringLiteral(" or ")));
        return false;
    }
    const QList<QByteArray> names = chosen->parameterNames();
    for (int i = 0; i < raw.size(); ++i) {
        const QMetaType type(chosen->parameterType(i));
        if (type.id() == QMetaType::QVariant) continue;
        // A number with a fraction, or one too large, is not quietly rounded
        // into an id or an index.
        if ((type.id() == QMetaType::Int || type.id() == QMetaType::UInt) && raw[i].canConvert<double>() && raw[i].typeId() != QMetaType::QString) {
            const double d = raw[i].toDouble();
            if (d != std::floor(d) || d > 2147483647.0 || d < -2147483648.0) {
                *error = QStringLiteral("%1: %2 must be a whole number — it takes %3").arg(name, QString::fromLatin1(names.value(i)), QString::fromLatin1(chosen->methodSignature()));
                return false;
            }
        }
        if (!raw[i].convert(type)) {
            *error = QStringLiteral("%1: %2 must be a %3 — it takes %4").arg(name, QString::fromLatin1(names.value(i)), QString::fromLatin1(chosen->parameterTypeName(i)),
                                                                              QString::fromLatin1(chosen->methodSignature()));
            return false;
        }
    }
    call->method = *chosen; call->args = raw;
    return true;
}

bool invoke(Call &call, QVariant *result, QString *error) {
    if (call.isProperty) {
        if (!call.args.isEmpty() && !call.property.write(call.object, call.args.first())) { *error = QStringLiteral("%1 did not take the value").arg(call.name); return false; }
        *result = call.property.read(call.object);
        return true;
    }
    const QMetaMethod &m = call.method;
    QList<QByteArray> typeNames;
    for (int i = 0; i < m.parameterCount(); ++i) typeNames << m.parameterTypeName(i);
    QGenericArgument a[10];
    for (int i = 0; i < call.args.size() && i < 10; ++i)
        a[i] = QGenericArgument(typeNames[i].constData(), QMetaType(m.parameterType(i)).id() == QMetaType::QVariant ? static_cast<const void *>(&call.args[i]) : call.args[i].constData());
    const QByteArray returnName = m.typeName();
    QVariant holder;
    QGenericReturnArgument r;
    if (m.returnType() == QMetaType::QVariant) r = QGenericReturnArgument("QVariant", &holder);
    else if (m.returnType() != QMetaType::Void) { holder = QVariant(QMetaType(m.returnType())); r = QGenericReturnArgument(returnName.constData(), holder.data()); }
    if (!m.invoke(call.object, Qt::DirectConnection, r, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9])) {
        *error = QStringLiteral("%1 could not be called").arg(call.name);
        return false;
    }
    *result = holder;
    return true;
}

// Catalog operations that capture or compare the photo as Develop has it:
// the photo must be open in the engine, not only selected.
bool needsOpen(const Call &call, const Backend &backend) {
    if (call.isProperty || call.object != &backend) return false;
    static const QStringList open{QStringLiteral("saveSnapshot"), QStringLiteral("compareSnapshot"), QStringLiteral("saveCurrentPreset"),
                                  QStringLiteral("saveCurrentModulePreset"), QStringLiteral("giveFirstEdit"), QStringLiteral("applyPreset"),
                                  QStringLiteral("openAiVersion"), QStringLiteral("openAiOriginal"), QStringLiteral("openLatestAiVersion")};
    return open.contains(QString::fromLatin1(call.method.name()));
}

// Engine operations that act on the open photo and would otherwise return
// quietly having done nothing.
bool needsPhoto(const Call &call, const EngineService &engine) {
    if (call.isProperty) return false;
    const QString name = QString::fromLatin1(call.method.name());
    if (call.object == engine.imageMatch()) {
        static const QStringList photoMethods{"useCurrent","preview","apply","applySelection","detailAt","editDraft","editApplied","reset"};
        return engine.imageId()<0 && photoMethods.contains(name);
    }
    if (call.object == engine.ai() || call.object == engine.denoise())
        return engine.imageId() < 0 && name != QLatin1String("install") && name != QLatin1String("cancel");
    if (call.object == &engine) {
        if (engine.imageId() >= 0) return false;
        static const QStringList prefixes{QStringLiteral("set"), QStringLiteral("add"), QStringLiteral("remove"), QStringLiteral("reset"),
            QStringLiteral("apply"), QStringLiteral("paste"), QStringLiteral("move"), QStringLiteral("rename"), QStringLiteral("duplicate"),
            QStringLiteral("straighten"), QStringLiteral("correct"), QStringLiteral("auto"), QStringLiteral("pick"), QStringLiteral("whiteBalance"),
            QStringLiteral("jump"), QStringLiteral("undo"), QStringLiteral("redo"), QStringLiteral("compare"), QStringLiteral("rebuild"),
            QStringLiteral("toolAction"), QStringLiteral("beginUndoGroup"), QStringLiteral("endUndoGroup")};
        if (name == QLatin1String("exportJpeg")) return true;
        if (name == QLatin1String("selectCameraProfile") || name == QLatin1String("selectCreativeProfile") || name == QLatin1String("importCreativeProfile")) return true;
        for (const QString &p : prefixes) if (name.startsWith(p)) return true;
        return false;
    }
    return false;
}

// AI drafts last for one process. Refuse missing/invalid selection state instead
// of reporting success when a UI-oriented void method would quietly return.
QString aiCallError(const Call &call, const EngineService &engine) {
    if (call.object != engine.ai() || call.isProperty) return {};
    const auto *ai = static_cast<AiService *>(engine.ai());
    const QString name = QString::fromLatin1(call.method.name());
    if (name == QLatin1String("install") || name == QLatin1String("cancel")) return {};
    if (name == QLatin1String("start"))
        return call.args.first() == QStringLiteral("mask") || call.args.first() == QStringLiteral("remove")
            ? QString() : QStringLiteral("ai.start takes mask or remove");
    if (ai->mode().isEmpty()) return QStringLiteral("start an AI session with ai.start in this apply run first");
    if ((name == QLatin1String("acceptMask") || name == QLatin1String("refineEdges")) && ai->mode() != QLatin1String("mask"))
        return QStringLiteral("%1 needs ai.start(\"mask\")").arg(call.name);
    if ((name == QLatin1String("remove") || name == QLatin1String("saveCopy") || name == QLatin1String("apply")) && ai->mode() != QLatin1String("remove"))
        return QStringLiteral("%1 needs ai.start(\"remove\")").arg(call.name);
    if ((name == QLatin1String("acceptMask") || name == QLatin1String("refineEdges") || name == QLatin1String("remove") || name == QLatin1String("invert")) && !ai->hasSelection())
        return QStringLiteral("%1 needs a selection first").arg(call.name);
    if ((name == QLatin1String("saveCopy") || name == QLatin1String("apply")) && ai->result().isEmpty()) return QStringLiteral("run ai.remove before applying a repair or saving a rendered copy");
    if (name == QLatin1String("undoPoint") && !ai->canUndoSelection()) return QStringLiteral("there is no AI selection step to undo");
    const auto coordinate = [](const QVariant &v) { bool ok = false; const double n = v.toDouble(&ok); return ok && std::isfinite(n) && n >= 0 && n <= 1; };
    if (name == QLatin1String("point") || name == QLatin1String("selectBox")) {
        for (int i = 0; i < (name == QLatin1String("point") ? 2 : 4); ++i)
            if (!coordinate(call.args[i])) return QStringLiteral("AI coordinates must be between 0 and 1");
        if (name == QLatin1String("point") && ai->points().size() >= 64) return QStringLiteral("an AI selection takes at most 64 points");
        if (name == QLatin1String("selectBox") && (qAbs(call.args[2].toDouble() - call.args[0].toDouble()) < .002 || qAbs(call.args[3].toDouble() - call.args[1].toDouble()) < .002))
            return QStringLiteral("the AI box is too small; each side must span at least 0.002 of the image");
    }
    if (name == QLatin1String("brushStroke")) {
        const auto points = call.args.first().toList();
        if (points.isEmpty() || points.size() > 4096) return QStringLiteral("a brush stroke needs 1 to 4096 [x, y] points");
        for (const auto &entry : points) {
            const auto pair = entry.toList();
            if (pair.size() != 2 || !coordinate(pair[0]) || !coordinate(pair[1])) return QStringLiteral("brush points must be [x, y] coordinates between 0 and 1");
        }
    }
    return {};
}
QString matchCallError(const Call &call,const EngineService &engine) {
    if(call.object!=engine.imageMatch()) return {};
    const auto *match=static_cast<ImageMatchService *>(engine.imageMatch());
    if(call.isProperty) {
        if(call.args.isEmpty() || QByteArray(call.property.name())!="options") return {};
        const auto options=call.args.first().toMap(), defaults=ImageMatch::Options{}.toMap();
        for(auto it=options.cbegin();it!=options.cend();++it) {
            if(!defaults.contains(it.key())) return QStringLiteral("Unknown Image Match option: %1").arg(it.key());
            if(it.key()=="mode") {
                if(!QStringList{"creative","consistency"}.contains(it.value().toString())) return QStringLiteral("Match mode is creative or consistency.");
            } else if(it.key().endsWith("Strength")) {
                bool converted=false;const double value=it.value().toDouble(&converted);
                if(!converted || !std::isfinite(value) || value<0 || value>1) return QStringLiteral("Match strengths must be between 0 and 1.");
            } else if(it.value().metaType().id()!=QMetaType::Bool) return QStringLiteral("Match switches take true or false.");
        }
        return {};
    }
    const QString name=QString::fromLatin1(call.method.name());
    if(name=="cancel" || name=="savePreview" || name=="showFit") return {};
    if(!engine.ready()) return QStringLiteral("Image Match needs the develop engine ready.");
    if((name=="preview" || name=="apply" || name=="applySelection") && !match->hasReference())
        return QStringLiteral("Choose match.setReference or match.useCurrent in this apply run first.");
    if((name=="apply" || name=="detailAt" || name=="editDraft") && !match->hasPreview())
        return QStringLiteral("Run match.preview for this photo first.");
    if(name=="undoBatch" && !match->canUndoBatch()) return QStringLiteral("There is no completed batch to undo in this run.");
    if(name=="applySelection") {
        const auto items=call.args.first().toList();
        if(items.isEmpty()) return QStringLiteral("Image Match needs target items containing path and variant.");
        for(const auto &v:items) {
            const auto item=v.toMap();
            if(item.value("path").toString().isEmpty() || item.value("variant").toInt()<0)
                return QStringLiteral("Each match target needs a path and a nonnegative variant.");
        }
    }
    if(name=="editDraft") {
        const auto parameters=match->report().value("parameters").toMap();
        if(!parameters.contains(call.args[0].toString())) return QStringLiteral("Unknown match parameter.");
    }
    if(name=="detailAt") for(const auto &arg:call.args) {
        const double value=arg.toDouble();
        if(!std::isfinite(value) || value<0 || value>1) return QStringLiteral("Detail coordinates must be between 0 and 1.");
    }
    if(name=="editApplied") {
        bool found=false;
        for(const auto &v:engine.paramsFor("omarawmatch")) {
            const auto p=v.toMap();
            if(p.value("field")==call.args.first() && (p.value("enabled").toBool() || p.value("configured").toBool())) found=true;
        }
        if(!found) return QStringLiteral("That applied Image Match control is unavailable.");
    }
    return {};
}
// Catalog operations on the selection. `photo` is the one the operation
// names (0 = none): the Library always selects something once a catalog is
// shown, so the selection itself proves nothing — without a named photo a
// selection operation would land on whichever photo sorts first.
bool needsSelection(const Call &call, int photo, const Backend &backend) {
    if (call.isProperty || photo) return false;
    const QString name = QString::fromLatin1(call.method.name());
    if (call.object == &backend) {
        // These methods fall back to the current photo/selection only when
        // their optional id is zero. An explicit id already names the target.
        const QList<QByteArray> names = call.method.parameterNames();
        const int idArgument = names.indexOf("id");
        if (!needsOpen(call, backend) && idArgument >= 0 && call.args.value(idArgument).toInt() > 0) return false;
        static const QStringList selectionOps{QStringLiteral("addKeyword"), QStringLiteral("addSelectionToAlbum"), QStringLiteral("addSelectionToQuickCollection"),
            QStringLiteral("buildSelectionSmartPreviews"), QStringLiteral("hashSelection"), QStringLiteral("keepSelectionOffline"), QStringLiteral("moveSelectionTo"),
            QStringLiteral("moveSelectionToTrash"), QStringLiteral("removeKeyword"), QStringLiteral("removeSelectionFromAlbum"), QStringLiteral("removeSelectionFromCatalog"),
            QStringLiteral("setCaption"), QStringLiteral("setCopyright"), QStringLiteral("setCreator"), QStringLiteral("setFlag"), QStringLiteral("setLabel"),
            QStringLiteral("setRating"), QStringLiteral("setTitle"), QStringLiteral("toggleFlag"), QStringLiteral("toggleLabel"), QStringLiteral("unstackSelection"),
            QStringLiteral("writeSidecarsForSelection"), QStringLiteral("readSidecarsForSelection"), QStringLiteral("stackSelection"),
            QStringLiteral("scanSelectionAutoTags"), QStringLiteral("setCurrentAutoTag"), QStringLiteral("resetCurrentAutoTagCorrections"),
            QStringLiteral("createVariant"), QStringLiteral("promoteVariant"), QStringLiteral("deleteVariant"),
            QStringLiteral("setStackTop"), QStringLiteral("moveInAlbum"), QStringLiteral("renamePhoto")};
        return selectionOps.contains(name) || needsOpen(call, backend);
    }
    return false;
}

int doApply(const QCommandLineParser &parser, Backend &backend, EngineService &engine, HeadlessCapture &capture, const QStringList &rest) {
    // The operations: a file, or "-" (or nothing) for standard input.
    QByteArray text;
    const QString source = rest.isEmpty() ? QStringLiteral("-") : rest.first();
    if (source == QLatin1String("-")) { QFile in; if (!in.open(stdin, QIODevice::ReadOnly)) return say(problem(QStringLiteral("cannot read standard input")), 2); text = in.readAll(); }
    else { QFile in(source); if (!in.open(QIODevice::ReadOnly)) return say(problem(QStringLiteral("cannot read %1").arg(source)), 2); text = in.readAll(); }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(text, &parseError);
    if (doc.isNull()) return say(problem(QStringLiteral("the operations are not JSON: %1").arg(parseError.errorString())), 2);
    const QJsonArray ops = doc.isArray() ? doc.array() : doc.object().value(QStringLiteral("ops")).toArray();
    if (ops.isEmpty()) return say(problem(QStringLiteral("no operations: give {\"ops\": [...]} or a list")), 2);
    const bool dry = parser.isSet(QStringLiteral("dry-run")), keepGoing = parser.isSet(QStringLiteral("keep-going"));

    // Every operation is checked before any runs, so a typo on the tenth
    // does not leave the first nine done.
    QString error;
    QJsonValue startPhoto = doc.isObject() ? doc.object().value(QStringLiteral("photo")) : QJsonValue();
    if (parser.isSet(QStringLiteral("photo"))) { bool number = false; const int n = parser.value(QStringLiteral("photo")).toInt(&number); startPhoto = number ? QJsonValue(n) : QJsonValue(parser.value(QStringLiteral("photo"))); }
    QVector<Call> calls(ops.size());
    QVector<int> photos(ops.size(), 0);
    int current = 0;
    if (!startPhoto.isUndefined() && !startPhoto.isNull()) { current = photoId(backend, startPhoto, &error); if (!current) return say(problem(error), 2); }
    QJsonArray results;
    int bad = 0;
    for (int i = 0; i < ops.size(); ++i) {
        const QJsonObject op = ops.at(i).toObject();
        if (op.contains(QStringLiteral("photo"))) {
            current = photoId(backend, op.value(QStringLiteral("photo")), &error);
            // Operations after a photo that is not there belong to it, not to
            // whatever was open before: they fail with it.
            if (!current) { current = -1; ++bad; results.append(QJsonObject{{QStringLiteral("index"), i}, {QStringLiteral("ok"), false}, {QStringLiteral("error"), error}}); continue; }
        }
        photos[i] = current;
        if (!resolve(backend, engine, capture, op, &calls[i], &error)) {
            ++bad;
            results.append(QJsonObject{{QStringLiteral("index"), i}, {QStringLiteral("op"), op.value(QStringLiteral("op"))}, {QStringLiteral("ok"), false}, {QStringLiteral("error"), error}});
        }
    }
    if (bad && !keepGoing) return say(QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("verb"), QStringLiteral("apply")},
                                                  {QStringLiteral("error"), QStringLiteral("%1 of %2 operations cannot run; nothing was done").arg(bad).arg(ops.size())},
                                                  {QStringLiteral("problems"), results}}, 2);
    if (dry) return say(QJsonObject{{QStringLiteral("ok"), bad == 0}, {QStringLiteral("verb"), QStringLiteral("apply")}, {QStringLiteral("dryRun"), true},
                                    {QStringLiteral("checked"), ops.size()}, {QStringLiteral("problems"), results}}, bad ? 2 : 0);

    results = QJsonArray();
    int done = 0, failed = 0, open = 0;
    bool firstEdit = false;
    // A selection made on purpose earlier in this run (catalog.select,
    // selectAll…) counts as naming the photos a selection operation acts on.
    bool chosen = false;
    for (int i = 0; i < ops.size(); ++i) {
        const QJsonObject op = ops.at(i).toObject();
        QJsonObject row{{QStringLiteral("index"), i}, {QStringLiteral("op"), calls[i].name.isEmpty() ? op.value(QStringLiteral("op")).toString() : calls[i].name}};
        if (!calls[i].object) { row.insert(QStringLiteral("ok"), false); row.insert(QStringLiteral("error"), QStringLiteral("skipped: it could not be resolved")); results.append(row); ++failed; continue; }
        if (photos[i] < 0) { row.insert(QStringLiteral("ok"), false); row.insert(QStringLiteral("error"), QStringLiteral("skipped: its photo is not in this catalog")); results.append(row); ++failed; continue; }
        // Catalog operations act on the selection, which only has to follow
        // the photo; they work on photos Develop cannot open (offline, or no
        // engine). Develop operations need the photo open.
        // The selection follows the op's photo every time: an earlier catalog
        // op may have moved it even when Develop still has this photo open.
        if (photos[i] && calls[i].object == &backend && !needsOpen(calls[i], backend)) { if (backend.currentId() != photos[i] || backend.selectedCount() != 1) backend.select(photos[i]); }
        else if (photos[i] && (photos[i] != open || engine.imageId() < 0
                 || engine.imagePath() != backend.info(photos[i]).value(QStringLiteral("path")).toString()
                 || engine.imageVariant() != backend.info(photos[i]).value(QStringLiteral("variant")).toInt())) {
            if (!openPhoto(backend, engine, photos[i], &error)) { row.insert(QStringLiteral("ok"), false); row.insert(QStringLiteral("error"), error); results.append(row); ++failed; if (!keepGoing) break; continue; }
            open = photos[i];
            // A newly imported photo starts from the same first edit Develop gives it.
            firstEdit = backend.giveFirstEdit() || firstEdit;
            settle(backend, engine);
        }
        if (needsPhoto(calls[i], engine) || (!chosen && needsSelection(calls[i], photos[i], backend))) {
            row.insert(QStringLiteral("ok"), false); row.insert(QStringLiteral("error"), QStringLiteral("no photo named: give \"photo\" (or --photo) first"));
            results.append(row); ++failed; if (!keepGoing) break; continue;
        }
        // Most engine calls return before the worker has done anything; what
        // the engine refuses, or an export that fails, arrives afterwards.
        QString refused, wrote;
        auto *ai = static_cast<AiService *>(engine.ai());
        auto *denoise = static_cast<DenoiseService *>(engine.denoise());
        auto *match = static_cast<ImageMatchService *>(engine.imageMatch());
        const auto matchFailedConn = QObject::connect(match, &ImageMatchService::operationFailed, match, [&refused](const QString &message) { if(refused.isEmpty()) refused=message; });
        const auto captureFailedConn = QObject::connect(&capture, &HeadlessCapture::operationFailed, &capture, [&refused](const QString &message) { if(refused.isEmpty()) refused=message; });
        const bool tagCall = calls[i].object==&backend && calls[i].name.contains(QStringLiteral("AutoTag"),Qt::CaseInsensitive);
        const auto tagFailedConn = QObject::connect(&backend, &Backend::autoTagFailed, &backend, [&](const QString &message) { if(tagCall && refused.isEmpty()) refused=message; });
        const auto denoiseFailedConn = QObject::connect(denoise, &DenoiseService::operationFailed, denoise, [&refused](const QString &message) { if (refused.isEmpty()) refused = message; });
        const auto denoiseSavedConn = QObject::connect(denoise, &DenoiseService::saved, denoise, [&wrote](const QString &path) { wrote = path; });
        const auto refusedConn = QObject::connect(&engine, &EngineService::engineFailed, &engine, [&refused](const QString &e) { if (refused.isEmpty()) refused = e; });
        const auto exportedConn = QObject::connect(&engine, &EngineService::exported, &engine, [&refused, &wrote](const QString &path, bool ok) {
            if (ok) wrote = path; else if (refused.isEmpty()) refused = QStringLiteral("the export failed");
        });
        const auto aiFailedConn = QObject::connect(ai, &AiService::operationFailed, ai, [&refused](const QString &message) { if (refused.isEmpty()) refused = message; });
        const auto aiSavedConn = QObject::connect(ai, &AiService::saved, ai, [&wrote](const QString &path) { wrote = path; });
        QVariant result;
        error = aiCallError(calls[i], engine);
        if (error.isEmpty()) error = matchCallError(calls[i],engine);
        bool ok = error.isEmpty() && invoke(calls[i], &result, &error);
        // Full-resolution Restormer on CPU can take hours. DenoiseService also
        // enforces a five-minute inactivity timeout, refreshed by tile progress.
        const int timeout = tagCall ? 6 * 60 * 60 * 1000 : calls[i].object == denoise ? 12 * 60 * 60 * 1000 : calls[i].name == QLatin1String("ai.install") ? 25 * 60 * 1000 : 10 * 60 * 1000;
        const bool settled = !ok || settle(backend, engine, timeout);
        QObject::disconnect(refusedConn); QObject::disconnect(exportedConn);
        QObject::disconnect(aiFailedConn); QObject::disconnect(aiSavedConn);
        QObject::disconnect(denoiseFailedConn); QObject::disconnect(denoiseSavedConn);
        QObject::disconnect(matchFailedConn); QObject::disconnect(captureFailedConn); QObject::disconnect(tagFailedConn);
        if (!settled) { ai->cancel(); denoise->cancel(); match->cancel(); if(tagCall) backend.setAutoTagPaused(true); row.insert(QStringLiteral("ok"), false); row.insert(QStringLiteral("error"), QStringLiteral("did not settle within %1 minutes").arg(timeout / 60000)); results.append(row); ++failed; break; }
        if (ok && !refused.isEmpty()) { ok = false; error = refused; }
        if (ok && tagCall && !calls[i].isProperty && result.metaType().id()==QMetaType::Bool && !result.toBool()) {
            ok=false; error=QStringLiteral("%1 was refused: %2").arg(calls[i].name,backend.statusMessage());
        }
        if (ok && tagCall && (calls[i].name==QStringLiteral("catalog.scanSelectionAutoTags")) && backend.autoTagPending()) {
            ok=false; error=QStringLiteral("Tagging did not finish: %1").arg(backend.autoTagStatus());
        }
        static const QStringList profileCommands{QStringLiteral("engine.selectCameraProfile"), QStringLiteral("engine.importCameraProfile"),
            QStringLiteral("engine.applyCameraLook"), QStringLiteral("engine.selectCreativeProfile"), QStringLiteral("engine.importCreativeProfile")};
        if (ok && profileCommands.contains(calls[i].name) && !result.toBool()) {
            ok = false; error = QStringLiteral("%1 was refused: %2").arg(calls[i].name, engine.status());
        }
        // A catalog call that needs the photo open answers 0 or false when it could not.
        if (ok && needsOpen(calls[i], backend) && (!result.isValid() || !result.toBool())) { ok = false; error = QStringLiteral("%1 did not happen: %2").arg(calls[i].name, backend.statusMessage()); }
        static const QStringList choosing{QStringLiteral("select"), QStringLiteral("selectRow"), QStringLiteral("selectAll"),
                                          QStringLiteral("selectDuplicates")};
        if (ok && calls[i].object == &backend && !calls[i].isProperty && choosing.contains(QString::fromLatin1(calls[i].method.name()))) chosen = true;
        row.insert(QStringLiteral("ok"), ok);
        if (calls[i].object == ai) row.insert(QStringLiteral("state"), propertiesJson(ai, {}));
        if (calls[i].object == match) row.insert(QStringLiteral("state"), propertiesJson(match, {}));
        if (calls[i].object == &capture) row.insert(QStringLiteral("state"), QJsonObject::fromVariantMap(capture.state()));
        if (calls[i].object == denoise) row.insert(QStringLiteral("state"), propertiesJson(denoise, {}));
        if (calls[i].object==&capture && result.canConvert<QVariantMap>()) row.insert(QStringLiteral("result"),json(result));
        if (ok) { if (result.isValid()) row.insert(QStringLiteral("result"), json(result)); if (!wrote.isEmpty()) row.insert(QStringLiteral("wrote"), wrote); ++done; }
        else { row.insert(QStringLiteral("error"), error); ++failed; }
        results.append(row);
        if (!ok && !keepGoing) break;
    }
    QJsonObject out{{QStringLiteral("ok"), failed == 0}, {QStringLiteral("verb"), QStringLiteral("apply")}, {QStringLiteral("catalog"), backend.catalogPath()},
                    {QStringLiteral("done"), done}, {QStringLiteral("failed"), failed}, {QStringLiteral("results"), results}};
    if (open) { out.insert(QStringLiteral("photo"), open); out.insert(QStringLiteral("historyEnd"), engine.historyEnd()); }
    if (firstEdit) out.insert(QStringLiteral("firstEdit"), QStringLiteral("the photo was new, so it first got the automatic first edit (Reset in Develop removes it)"));
    return say(out, failed == 0 ? 0 : 1);
}

// ── skill: the agent skill that ships with the app ────────────────────────

// Where it ended up: beside a build, or in the package's share.
QString skillFolder() {
    const QString app = QCoreApplication::applicationDirPath();
    const QStringList places{app + QStringLiteral("/../skills/omaraw"), app + QStringLiteral("/skills/omaraw"),
                             app + QStringLiteral("/../share/omaraw/skills/omaraw"),
                             QStringLiteral("/usr/share/omaraw/skills/omaraw"), QStringLiteral("/usr/local/share/omaraw/skills/omaraw")};
    for (const QString &place : places)
        if (QFileInfo::exists(place + QStringLiteral("/SKILL.md"))) return QFileInfo(place).canonicalFilePath();
    return QString();
}

// Where the agents on this computer keep their skills. Omarchy's own skills
// are one directory under /usr/share linked into each of these, so OmaRAW's
// is put there the same way. The first two every install has; the rest only
// for an agent that is actually here — a directory for a tool somebody does
// not use is litter.
QVector<QPair<QString, bool>> skillPlaces() {
    const QString home = QDir::homePath();
    QVector<QPair<QString, bool>> places{{home + QStringLiteral("/.agents/skills"), true}, {home + QStringLiteral("/.claude/skills"), true}};
    const QVector<QPair<QString, QString>> others{{home + QStringLiteral("/.codex"), home + QStringLiteral("/.codex/skills")},
                                                  {home + QStringLiteral("/.pi"), home + QStringLiteral("/.pi/agent/skills")},
                                                  {home + QStringLiteral("/.hermes"), home + QStringLiteral("/.hermes/skills")}};
    for (const auto &other : others) if (QFileInfo::exists(other.first)) places.append({other.second, false});
    const QString profiles = home + QStringLiteral("/.hermes/profiles");
    if (QFileInfo::exists(profiles))
        for (const QString &profile : QDir(profiles).entryList(QDir::Dirs | QDir::NoDotAndDotDot))
            places.append({profiles + QLatin1Char('/') + profile + QStringLiteral("/skills"), false});
    return places;
}

// The skill is a file in the app; putting it where the agents look is a link
// owned by the person, including when the installer links it on their behalf.
int doSkill(const QCommandLineParser &parser) {
    const QString folder = skillFolder();
    if (folder.isEmpty()) return say(problem(QStringLiteral("the skill is not beside this copy of OmaRAW; it lives in skills/omaraw in the source")), 1);
    const bool link = parser.isSet(QStringLiteral("link")), force = parser.isSet(QStringLiteral("force"));
    QJsonArray rows;
    QStringList trouble;
    int linkedNow = 0, alreadyThere = 0;
    for (const auto &place : skillPlaces()) {
        const QString target = place.first + QStringLiteral("/omaraw");
        const QFileInfo already(target);
        const bool matches = already.exists() && already.canonicalFilePath() == folder;
        QJsonObject row{{QStringLiteral("place"), target}, {QStringLiteral("linked"), matches}};
        if (matches) ++alreadyThere;
        if (!link || matches) { rows.append(row); continue; }
        if (already.exists() || already.isSymLink()) {
            if (!force) {
                row.insert(QStringLiteral("note"), QStringLiteral("something else is there; --force replaces it"));
                trouble << QStringLiteral("%1 is already something else").arg(target);
                rows.append(row); continue;
            }
            const bool cleared = already.isSymLink() ? QFile::remove(target) : QDir(target).removeRecursively();
            if (!cleared) { trouble << QStringLiteral("%1 could not be replaced").arg(target); rows.append(row); continue; }
        }
        if (!QDir().mkpath(place.first) || !QFile::link(folder, target)) { trouble << QStringLiteral("%1 could not be linked").arg(target); rows.append(row); continue; }
        row.insert(QStringLiteral("linked"), true);
        ++linkedNow;
        rows.append(row);
    }
    QJsonObject out{{QStringLiteral("verb"), QStringLiteral("skill")}, {QStringLiteral("skill"), folder + QStringLiteral("/SKILL.md")},
                    {QStringLiteral("places"), rows}, {QStringLiteral("linked"), linkedNow + alreadyThere}};
    if (!trouble.isEmpty()) {
        QString said = trouble.join(QStringLiteral("; ")) + QLatin1Char('.');
        if (said.contains(QStringLiteral("already something else"))) said += QStringLiteral(" Use --force to replace what is there.");
        out.insert(QStringLiteral("ok"), false); out.insert(QStringLiteral("error"), said);
        return say(out, 1);
    }
    out.insert(QStringLiteral("ok"), true);
    out.insert(QStringLiteral("advice"), link ? QStringLiteral("The agents on this computer will find it in a new session.")
                                              : alreadyThere == rows.size() ? QStringLiteral("Every agent here already has it.")
                                                                            : QStringLiteral("Run `omaraw skill --link` to put it where the agents look."));
    return say(out, 0);
}

} // namespace

OutputGuard::OutputGuard(bool enabled) {
    if (!enabled) return;
    std::fflush(stdout);
    m_saved = ::dup(STDOUT_FILENO);
    if (m_saved >= 0) {
        if (::dup2(STDERR_FILENO, STDOUT_FILENO) < 0) {
            ::close(m_saved); m_saved = -1;
        } else g_answer = m_saved;
    }
}

OutputGuard::~OutputGuard() {
    if (m_saved < 0) return;
    std::fflush(stdout);
    ::dup2(m_saved, STDOUT_FILENO);
    ::close(m_saved);
    g_answer = -1;
}

const char *const kVerbs[] = {"import", "list", "export", "inspect", "apply", "ops", "skill"};

bool isVerb(const char *word) {
    for (const char *verb : kVerbs) if (!std::strcmp(word, verb)) return true;
    return false;
}

bool requested(int argc, char *argv[]) {
    // A verb means headless, the way the rest of the suite works: `omaraw`
    // opens a window, `omaraw list` answers a question. --headless remains
    // for the one ambiguous case, a folder that happens to share a verb's
    // name, since a bare folder still means "open, and import this".
    // Only the first plain word counts, and an option's value is not a
    // word: `omaraw --text list` searches for "list", it is not the verb.
    static const char *const withValue[] = {"catalog", "mode", "dest", "rating", "flag", "label", "text", "format", "bits",
                                            "quality", "max-edge", "suffix", "photo", "filter",
                                            // Qt's own, taken before the parser sees them
                                            "platform", "platformtheme", "style", "stylesheet", "qmljsdebugger", "plugin", "display"};
    bool forced = false;
    const char *first = nullptr;
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (!std::strcmp(a, "--headless")) { forced = true; continue; }
        if (!std::strcmp(a, "--")) { if (!first && i + 1 < argc) first = argv[i + 1]; break; }
        if (a[0] == '-') {
            const char *name = a + (a[1] == '-' ? 2 : 1);
            if (std::strchr(name, '=')) continue;
            for (const char *v : withValue) if (!std::strcmp(name, v)) { ++i; break; }
            continue;
        }
        if (!first) first = a;
    }
    return forced || (first && isVerb(first));
}

void addOptions(QCommandLineParser &parser) {
    parser.addOption({QStringLiteral("headless"), QStringLiteral("Force the verb form when a folder shares a verb\u2019s name")});
    parser.addOption({QStringLiteral("auto-tag"), QStringLiteral("import: detect supported subjects offline and wait for tagging")});
    parser.addOption({QStringLiteral("new-catalog"), QStringLiteral("Create --catalog when it does not exist yet")});
    parser.addOption({QStringLiteral("mode"), QStringLiteral("import: add, copy, move or verify"), QStringLiteral("mode")});
    parser.addOption({QStringLiteral("dest"), QStringLiteral("import: where copied or moved files land"), QStringLiteral("folder")});
    parser.addOption({QStringLiteral("rating"), QStringLiteral("list/export: at least this many stars"), QStringLiteral("stars")});
    parser.addOption({QStringLiteral("flag"), QStringLiteral("list/export: pick, reject or none"), QStringLiteral("flag")});
    parser.addOption({QStringLiteral("label"), QStringLiteral("list/export: colour label"), QStringLiteral("colour")});
    parser.addOption({QStringLiteral("text"), QStringLiteral("list/export: search text"), QStringLiteral("text")});
    parser.addOption({QStringLiteral("format"), QStringLiteral("export: jpeg, png, tiff…"), QStringLiteral("format")});
    parser.addOption({QStringLiteral("quality"), QStringLiteral("export: 1 to 100"), QStringLiteral("quality")});
    parser.addOption({QStringLiteral("max-edge"), QStringLiteral("export: longest side in pixels"), QStringLiteral("pixels")});
    parser.addOption({QStringLiteral("suffix"), QStringLiteral("export: added to each name"), QStringLiteral("suffix")});
    parser.addOption({QStringLiteral("bits"), QStringLiteral("export: 8 or 16 (tiff, png, psd)"), QStringLiteral("bits")});
    parser.addOption({QStringLiteral("location"), QStringLiteral("export: keep where the photo was taken (GPS)")});
    parser.addOption({QStringLiteral("history"), QStringLiteral("export: keep the Develop history in the file")});
    parser.addOption({QStringLiteral("photo"), QStringLiteral("inspect/apply: a photo's id, or its path"), QStringLiteral("photo")});
    parser.addOption({QStringLiteral("full"), QStringLiteral("inspect: everything, not only what moved")});
    parser.addOption({QStringLiteral("dry-run"), QStringLiteral("apply: check every operation, run none")});
    parser.addOption({QStringLiteral("keep-going"), QStringLiteral("apply: carry on past a failed operation")});
    parser.addOption({QStringLiteral("filter"), QStringLiteral("ops: only names containing this"), QStringLiteral("text")});
    parser.addOption({QStringLiteral("link"), QStringLiteral("skill: put it where the agents on this computer look")});
    parser.addOption({QStringLiteral("force"), QStringLiteral("skill --link: replace something else already there")});
}

int fail(const QString &message) { return say(problem(message), 2); }

int early(const QCommandLineParser &parser) {
    if (parser.positionalArguments().value(0) != QLatin1String("skill")) return -1;
    return doSkill(parser);
}

int run(const QCommandLineParser &parser, Backend &backend, EngineService &engine) {
    QStringList rest = parser.positionalArguments();
    if (rest.isEmpty()) return say(problem(QStringLiteral("--headless needs a verb: import, list, export, inspect, apply or ops")), 2);
    const QString verb = rest.takeFirst();
    if (verb == QLatin1String("list")) return doList(parser, backend);
    if (verb == QLatin1String("import")) return doImport(parser, backend, engine, rest);
    if (verb == QLatin1String("export")) return doExport(parser, backend, engine, rest);
    HeadlessCapture capture(&backend);
    if (verb == QLatin1String("ops")) return doOps(parser, backend, engine, capture);
    if (verb == QLatin1String("inspect")) return doInspect(parser, backend, engine, capture);
    if (verb == QLatin1String("apply")) return doApply(parser, backend, engine, capture, rest);
    return say(problem(QStringLiteral("%1 is not a verb; use import, list, export, inspect, apply or ops").arg(verb)), 2);
}

} // namespace Headless
