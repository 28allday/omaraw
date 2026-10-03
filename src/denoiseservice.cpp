#include "denoiseservice.h"
#include "airuntime.h"
#include "engineservice.h"
#include "aiservice.h"
#include "metadata.h"
#include "colourpipeline.h"
#include "displaycolour.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QUrl>
#include <QDateTime>
#include <cmath>
#include <signal.h>
#include <unistd.h>

DenoiseService::DenoiseService(EngineService *engine) : QObject(engine), m_engine(engine) {
    m_process.setUnixProcessParameters(QProcess::UnixProcessFlag::CreateNewSession);
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] { stop(); fail(tr("AI denoise timed out. Try CPU processing.")); });
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &DenoiseService::readOutput);
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] { m_errors += m_process.readAllStandardError(); m_errors = m_errors.right(4096); });
    connect(&m_process, &QProcess::finished, this, &DenoiseService::finished);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart && m_busy) fail(tr("Could not start AI denoise: %1").arg(m_process.errorString()));
    });
    connect(engine, &EngineService::imageChanged, this, [this] {
        // Settings belong to the selected photo even before its first request,
        // and after cancellation has discarded the worker's source identity.
        if (m_photoId == m_engine->imageId() && m_photoPath == m_engine->imagePath()) return;
        m_photoId = m_engine->imageId(); m_photoPath = m_engine->imagePath();
        if (m_busy && !m_installing && !m_finishingCopy) cancel();
        clearPreview(); m_image = -1; m_iso = 0; m_x = m_y = .5;
        m_previewArea.clear(); m_navigationPending = false; emit previewAreaChanged();
        m_sensor.clear(); m_model.clear(); m_actualBackend.clear(); emit changed();
    });
    connect(engine, &EngineService::editRevisionChanged, this, [this] {
        if (m_busy && !m_installing && m_request.value("action") == "preview") cancel();
        else if (!before().isEmpty()) { clearPreview(); emit changed(); }
    });
    connect(engine, &EngineService::denoisePreviewReady, this,
        [this](int ticket, const QImage &before, const QImage &after, const QString &error) {
            if (!m_busy || m_installing || ticket != m_serial || m_request.value("action") != "preview") return;
            if (!error.isEmpty() || before.isNull() || after.isNull() || before.size() != after.size()) {
                fail(error.isEmpty() ? tr("Cannot render the denoise comparison.") : error); return;
            }
            { QMutexLocker lock(&m_mutex); m_before = before; m_after = after; }
            const int sw = m_answer.value("sensorWidth").toInt(), sh = m_answer.value("sensorHeight").toInt();
            const int w = m_answer.value("width").toInt(), h = m_answer.value("height").toInt();
            const int x = m_answer.value("left").toInt(), y = m_answer.value("top").toInt();
            const int orientation = m_answer.value("orientation", 1).toInt();
            int dx = x, dy = y;
            switch (orientation) {
            case 2: dx = sw-x-w; break;
            case 3: dx = sw-x-w; dy = sh-y-h; break;
            case 4: dy = sh-y-h; break;
            case 5: dx = y; dy = x; break;
            case 6: dx = sh-y-h; dy = x; break;
            case 7: dx = sh-y-h; dy = sw-x-w; break;
            case 8: dx = y; dy = sw-x-w; break;
            }
            const bool transpose = orientation >= 5;
            m_previewArea = {{"x", dx}, {"y", dy}, {"width", transpose ? h : w}, {"height", transpose ? w : h},
                {"frameWidth", transpose ? sh : sw}, {"frameHeight", transpose ? sw : sh},
                {"sensorWidth", sw}, {"sensorHeight", sh}, {"sensorX", x}, {"sensorY", y}, {"orientation", orientation}};
            emit previewAreaChanged();
            ++m_serial; m_busy = false; m_progress = 1;
            m_status = tr("Preview ready · %1 · %2. Colour adjustments included.").arg(m_model, m_actualBackend);
            emit changed();
            if (m_navigationPending) {
                const auto position = m_pendingPosition;
                m_navigationPending = false;
                previewAt(position.x(), position.y());
            }
        });
    connect(engine, &EngineService::denoiseCopyPrepared, this, [this](const QString &path, const QString &error) {
        if (!m_finishingCopy || path != m_destination) return;
        m_finishingCopy = false;
        if (!error.isEmpty()) {
            fail(tr("DNG saved at %1, but its edits could not be copied: %2").arg(path, error)); return;
        }
        const QString original = m_source; const int variant = m_variant;
        m_busy = false; m_progress = 1;
        m_status = tr("AI denoise applied. The original is unchanged."); emit changed();
        emit saved(path, original, variant);
    });
    connect(&DisplayColour::instance(), &DisplayColour::changed, this, &DenoiseService::changed);
}
DenoiseService::~DenoiseService() { stop(); }
QString DenoiseService::home() const {
    return AiRuntime::home("OMARAW_DENOISE_HOME", "denoise", "denoise/v1");
}
bool DenoiseService::installed() const {
    QFile ready(home() + "/ready.json"), manifest(":/denoise/models.json");
    if (m_repairRequired || !QFileInfo::exists(home() + "/venv/bin/python") || !ready.open(QIODevice::ReadOnly) || !manifest.open(QIODevice::ReadOnly)) return false;
    const auto expected = QJsonDocument::fromJson(manifest.readAll());
    if (QJsonDocument::fromJson(ready.readAll()) != expected) return false;
    for (const auto &value : expected.object().value("models").toArray()) {
        const auto entry = value.toObject(); QFileInfo f(home() + "/models/" + entry.value("name").toString());
        if (!f.isFile() || f.size() != entry.value("bytes").toInteger()) return false;
    }
    return true;
}
bool DenoiseService::prepareScripts() {
    if (AiRuntime::bundled(home())) return QFileInfo::exists(home() + "/scripts/worker.py");
    if (!QDir().mkpath(home() + "/scripts")) return false;
    for (const QString &name : {"worker.py", "models.json", "LICENSES.txt"}) {
        QFile source(":/denoise/" + name); QSaveFile out(home() + "/scripts/" + name);
        if (!source.open(QIODevice::ReadOnly) || !out.open(QIODevice::WriteOnly)) return false;
        const auto bytes = source.readAll();
        if (out.write(bytes) != bytes.size() || !out.commit()) return false;
    }
    return true;
}
void DenoiseService::clearPreview() {
    { QMutexLocker lock(&m_mutex); m_before = {}; m_after = {}; }
    ++m_serial;
}
bool DenoiseService::mutableSetting() {
    if (m_busy) { emit operationFailed(tr("Wait for AI denoise to finish or cancel it first.")); return false; }
    return true;
}
void DenoiseService::setStrength(double v) {
    if (!mutableSetting()) return;
    if (!std::isfinite(v) || v < 0 || v > 1) { fail(tr("Strength must be between 0 and 1.")); return; }
    if (v == m_strength) return;
    m_strength = v; clearPreview(); emit changed();
}
void DenoiseService::setBackend(const QString &v) {
    if (!mutableSetting()) return;
    if (v != "auto" && v != "cpu") { fail(tr("Denoise backend must be auto or cpu.")); return; }
    m_backend = v; emit changed();
}
void DenoiseService::setIsoOverride(int v) {
    if (!mutableSetting()) return;
    if (v < 0 || v > 10000000) { fail(tr("ISO must be zero (from photo) or a positive value up to 10000000.")); return; }
    if (v == m_iso) return;
    m_iso = v; clearPreview(); emit changed();
}
void DenoiseService::setPreviewX(double v) {
    if (!mutableSetting()) return;
    if (!std::isfinite(v) || v < 0 || v > 1) { fail(tr("Preview position must be between 0 and 1.")); return; }
    if (v == m_x) return;
    m_x = v; clearPreview(); emit changed();
}
void DenoiseService::setPreviewY(double v) {
    if (!mutableSetting()) return;
    if (!std::isfinite(v) || v < 0 || v > 1) { fail(tr("Preview position must be between 0 and 1.")); return; }
    if (v == m_y) return;
    m_y = v; clearPreview(); emit changed();
}
QImage DenoiseService::previewCopy(bool after) const { QMutexLocker lock(&m_mutex); return after ? m_after : m_before; }
QString DenoiseService::before() const {
    return previewCopy(false).isNull() ? QString() : QString("image://engine/_denoise/before/%1/%2").arg(m_serial).arg(DisplayColour::instance().revision());
}
QString DenoiseService::after() const {
    return previewCopy(true).isNull() ? QString() : QString("image://engine/_denoise/after/%1/%2").arg(m_serial).arg(DisplayColour::instance().revision());
}
void DenoiseService::stop() {
    m_timeout.stop(); m_stopping = true;
    if (m_process.state() != QProcess::NotRunning) {
        const auto pid = m_process.processId();
        if (pid > 0) ::kill(-pid, SIGTERM);
        if (!m_process.waitForFinished(500)) { if (pid > 0) ::kill(-pid, SIGKILL); m_process.kill(); m_process.waitForFinished(1000); }
    }
    m_stopping = false;
}
void DenoiseService::reset() {
    if (m_finishingCopy) return;
    cancel();
    setStrength(.6); setIsoOverride(0);
    setPreviewX(.5); setPreviewY(.5);
    clearPreview(); m_status = tr("AI denoise reset."); emit changed();
}

void DenoiseService::cancel() {
    if (m_finishingCopy) return; // The published copy is being registered; finish this short step.
    m_navigationPending = false;
    stop(); m_busy = false; m_installing = false; m_temp.reset(); clearPreview();
    m_image = -1; m_answer.clear(); m_progress = 0; m_status = tr("AI denoise cancelled."); emit changed();
}
void DenoiseService::fail(const QString &message) {
    m_navigationPending = false;
    m_timeout.stop(); m_busy = false; m_installing = false; m_status = message; emit changed(); emit operationFailed(message);
}
void DenoiseService::install() {
    if (!mutableSetting()) return;
    m_status = installed() ? tr("AI denoise is included and ready.")
                          : tr("Reinstall the OmaRAW package to restore its included denoise tools.");
    emit changed();
}
void DenoiseService::preview() { start("preview"); }
void DenoiseService::previewAt(double x, double y) {
    if (!std::isfinite(x) || !std::isfinite(y) || m_previewArea.isEmpty()) return;
    x = qBound(0.0, x, 1.0); y = qBound(0.0, y, 1.0);
    if (m_busy) {
        if (!m_installing && m_request.value("action") == "preview") {
            m_pendingPosition = QPointF(x, y); m_navigationPending = true;
        }
        return;
    }
    // Navigation is expressed in the upright picture, while the worker crops
    // the unrotated sensor. Before and After always share this same region.
    double sx = x, sy = y;
    switch (m_previewArea.value("orientation", 1).toInt()) {
    case 2: sx = 1-x; break;
    case 3: sx = 1-x; sy = 1-y; break;
    case 4: sy = 1-y; break;
    case 5: sx = y; sy = x; break;
    case 6: sx = y; sy = 1-x; break;
    case 7: sx = 1-y; sy = 1-x; break;
    case 8: sx = 1-y; sy = x; break;
    }
    const int w = m_previewArea.value("sensorWidth").toInt(), h = m_previewArea.value("sensorHeight").toInt();
    if (w <= 0 || h <= 0) return;
    const double halfW = qMin(512, w)/2.0, halfH = qMin(512, h)/2.0;
    sx = qBound(halfW, sx*w, w-halfW)/w; sy = qBound(halfH, sy*h, h-halfH)/h;
    if (!before().isEmpty() && qAbs(sx*w-m_previewArea.value("sensorX").toDouble()-halfW) < 1
        && qAbs(sy*h-m_previewArea.value("sensorY").toDouble()-halfH) < 1) return;
    m_x = sx; m_y = sy;
    start("preview", {}, true);
}
void DenoiseService::apply() {
    if (!mutableSetting()) return;
    const QFileInfo source(m_engine->imagePath());
    if (m_engine->imageId() < 0 || !source.isFile()) { fail(tr("Open an original RAW photo to apply AI denoise.")); return; }
    const QString stem = source.absolutePath() + '/' + source.completeBaseName() + "-denoised";
    for (int number = 0; number < 10000; ++number) {
        const QString destination = stem + (number ? '-' + QString::number(number) : QString()) + ".dng";
        bool used = QFileInfo::exists(destination) || QFileInfo(destination).isSymLink();
        for (const auto &sidecar : Metadata::sidecarNames(destination))
            used = used || QFileInfo::exists(sidecar) || QFileInfo(sidecar).isSymLink();
        if (!used) { saveCopy(destination); return; }
    }
    fail(tr("Could not find an unused DNG name. Choose Save copy as…"));
}
void DenoiseService::saveCopy(const QString &fileOrUrl) {
    if (!mutableSetting()) return;
    const QUrl url(fileOrUrl);
    if (!url.scheme().isEmpty() && !url.isLocalFile()) { fail(tr("Choose a local DNG filename.")); return; }
    const QFileInfo file(url.isLocalFile() ? url.toLocalFile() : fileOrUrl);
    if (fileOrUrl.isEmpty() || file.suffix().compare("dng", Qt::CaseInsensitive) != 0) { fail(tr("Choose a .dng filename.")); return; }
    if (file.exists() || file.isSymLink()) { fail(tr("That file already exists. Choose a new DNG filename.")); return; }
    for (const auto &sidecar : Metadata::sidecarNames(file.absoluteFilePath()))
        if (QFileInfo::exists(sidecar) || QFileInfo(sidecar).isSymLink()) { fail(tr("A sidecar already uses that name. Choose a new DNG filename.")); return; }
    if (!QFileInfo(file.absolutePath()).isDir()) { fail(tr("The destination folder does not exist.")); return; }
    start("save", file.absoluteFilePath());
}
void DenoiseService::start(const QString &action, const QString &destination, bool keepPreview) {
    if (!mutableSetting()) return;
    if (!installed()) { fail(tr("The included denoise tools are missing or damaged. Reinstall the OmaRAW package.")); return; }
    if (m_engine->imageId() < 0 || !m_engine->maintenanceReady() || static_cast<AiService *>(m_engine->ai())->busy()) {
        fail(tr("Open a RAW photo and wait for processing to finish.")); return;
    }
    const QString source = m_engine->imagePath();
    if (!Metadata::isRawExtension(QFileInfo(source).suffix().toLower())) { fail(tr("AI denoise needs a Bayer or X-Trans RAW photo.")); return; }
    AssetRecord record;
    if (!QFileInfo(source).isFile() || !Metadata::read(source, record)) { fail(tr("The original RAW must be online to denoise it.")); return; }
    const int iso = m_iso ? m_iso : record.iso;
    if (!prepareScripts()) { fail(tr("Cannot write the local denoise folder.")); return; }
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (!QDir().mkpath(cache)) { fail(tr("Cannot create the denoise cache folder.")); return; }
    m_temp = std::make_shared<QTemporaryDir>(cache + "/denoise-XXXXXX");
    if (!m_temp->isValid()) { fail(tr("Cannot create the denoise working folder.")); return; }
    m_source = source; m_image = m_engine->imageId(); m_variant = m_engine->imageVariant();
    const QFileInfo stat(source); m_sourceSize = stat.size(); m_sourceModified = stat.lastModified().toMSecsSinceEpoch();
    m_destination = destination; m_retried = false; m_actualBackend.clear();
    m_sensor.clear(); m_model.clear();
    m_request = {{"action", action}, {"image", source}, {"directory", m_temp->path()}, {"strength", m_strength},
                 {"iso", iso}, {"backend", m_backend}, {"x", m_x}, {"y", m_y}, {"make", record.make}, {"model", record.model}};
    const auto calibration = m_engine->toolState().value("denoiseCalibration").toMap();
    if (!calibration.isEmpty()) m_request.insert("calibration", calibration);
    if (QFileInfo::exists(m_engine->rawCameraDatabase())) m_request.insert("cameraDatabase", m_engine->rawCameraDatabase());
    m_finishingCopy = false;
    if (action == "save") {
        m_copyValues = m_engine->currentValues(true);
        m_copyValues += m_engine->toolState().value("whiteBalanceParams").toList();
        m_copyValues += m_engine->toolState().value("denoiseExposureParams").toList();
        m_copyValues << QVariantMap{{"denoiseCrop", m_engine->crop()}};
        const auto lens = m_engine->lensProfile();
        if (lens.value("overridden").toBool()) m_copyValues << QVariantMap{{"denoiseLensOverride", lens.value("lens")}};
    }
    if (action == "preview") {
        if (keepPreview) ++m_serial; else clearPreview();
        m_previewValues = m_engine->currentValues();
        m_previewValues += m_engine->toolState().value("whiteBalanceParams").toList();
        m_previewValues += m_engine->toolState().value("denoiseExposureParams").toList();
        m_request.insert("developPreview", true);
    }
    m_busy = true; m_progress = 0; m_status = tr("Preparing AI denoise…"); emit changed(); launch();
}
void DenoiseService::launch() {
    m_output.clear(); m_errors.clear(); m_answer.clear();
    QSaveFile request(m_temp->filePath("request.json"));
    const auto data = QJsonDocument::fromVariant(m_request).toJson(QJsonDocument::Compact);
    if (!request.open(QIODevice::WriteOnly) || request.write(data) != data.size() || !request.commit()) { fail(tr("Cannot write the denoise request.")); return; }
    m_timeout.start(5 * 60 * 1000);
    m_process.start(home() + "/venv/bin/python", {"-I", "-B", home() + "/scripts/worker.py", "--home", home(), "--request", request.fileName()});
}
void DenoiseService::readOutput() {
    m_output += m_process.readAllStandardOutput();
    while (m_output.contains('\n')) {
        const int end = m_output.indexOf('\n'); const auto line = m_output.left(end); m_output.remove(0, end+1);
        const auto answer = QJsonDocument::fromJson(line).object();
        if (answer.value("event") == "progress") {
            m_status = answer.value("message").toString(); m_progress = answer.value("progress").toDouble(m_progress);
            if (!m_installing) m_timeout.start(5 * 60 * 1000);
            emit changed();
        } else if (answer.value("event") == "sensor") {
            m_sensor = answer.value("sensor").toString(); m_model = answer.value("model").toString();
            emit changed();
        } else if (answer.contains("ok")) m_answer = answer.toVariantMap();
    }
}
void DenoiseService::finished(int code, QProcess::ExitStatus exit) {
    if (m_stopping || !m_busy) return;
    readOutput(); m_timeout.stop();
    if (m_installing) {
        m_installing = false;
        if (code == 0 && m_answer.value("ok").toBool()) m_repairRequired = false;
        if (code == 0 && installed()) { m_busy = false; m_status = tr("AI denoise is ready."); emit changed(); }
        else fail(tr("AI denoise setup failed: %1").arg(m_answer.value("error", m_errors.right(1500)).toString()));
        return;
    }
    if ((exit == QProcess::CrashExit || (code != 0 && m_answer.isEmpty())) && !m_retried && m_request.value("backend") == "auto") {
        m_retried = true; m_request["backend"] = "cpu"; m_status = tr("GPU processing stopped. Retrying on CPU…"); emit changed(); launch(); return;
    }
    if (code != 0 || !m_answer.value("ok").toBool()) {
        if (m_answer.value("repair_required").toBool()) m_repairRequired = true;
        fail(m_answer.value("error", tr("AI denoise stopped unexpectedly. Try CPU processing.")).toString()); return;
    }
    const QFileInfo source(m_source);
    if (m_image != m_engine->imageId() || !source.isFile() || source.size() != m_sourceSize || source.lastModified().toMSecsSinceEpoch() != m_sourceModified) {
        fail(tr("The source photo changed. Run denoise again.")); return;
    }
    m_actualBackend = m_retried ? tr("CPU (GPU worker stopped)") : m_answer.value("backend").toString();
    if (m_request.value("action") == "preview") {
        m_status = tr("Applying your colour adjustments to the comparison…"); emit changed();
        if (m_busy && m_temp) m_engine->renderDenoisePreview(m_serial, m_temp, m_source, m_previewValues);
        return;
    }
    const QString generated = m_temp->filePath("denoised.dng"); QString error;
    if (!Metadata::copyDenoiseMetadata(m_source, generated, &error)) { fail(tr("Cannot preserve camera metadata: %1").arg(error)); return; }
    // Stage on the destination filesystem. link() publishes atomically and
    // refuses existing names, including dangling symlinks, without a race.
    QTemporaryFile stage(QFileInfo(m_destination).absolutePath() + "/.omaraw-denoise-XXXXXX.dng");
    QFile input(generated);
    if (!stage.open() || !input.open(QIODevice::ReadOnly)) { fail(tr("Cannot prepare the DNG copy in the destination folder.")); return; }
    while (!input.atEnd()) {
        const auto bytes = input.read(1024 * 1024);
        if (bytes.isEmpty() || stage.write(bytes) != bytes.size()) { fail(tr("Could not write the DNG copy. Check free disk space.")); return; }
    }
    if (!stage.flush() || ::fsync(stage.handle())) {
        fail(tr("Could not write the DNG copy. Check free disk space.")); return;
    }
    // A long denoise and staging copy leave time for another application to
    // create an edit sidecar. Check again immediately before publication.
    for (const auto &sidecar : Metadata::sidecarNames(m_destination))
        if (QFileInfo::exists(sidecar) || QFileInfo(sidecar).isSymLink()) { fail(tr("A sidecar now uses that name. Choose a new DNG filename.")); return; }
    if (::link(QFile::encodeName(stage.fileName()).constData(), QFile::encodeName(m_destination).constData())) {
        fail(tr("Cannot save the DNG copy. Check disk space, permissions and that the filename is unused.")); return;
    }
    m_finishingCopy = true; m_status = tr("Keeping your edits on the denoised copy…"); emit changed();
    m_engine->prepareDenoiseCopy(m_destination, m_copyValues);
}
