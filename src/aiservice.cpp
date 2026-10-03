#include "aiservice.h"
#include "engineservice.h"
#include "displaycolour.h"
#include "colourpipeline.h"
#include "metadata.h"
#include <QColorSpace>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QPainter>
#include <QPainterPath>
#include <QLineF>
#include <deque>
#include <cmath>
#include <signal.h>

AiService::AiService(EngineService *engine) : QObject(engine), m_engine(engine) {
    m_process.setParent(this); m_process.setObjectName("aiWorker");
    m_idle.setParent(this); m_idle.setObjectName("aiIdleTimer");
    m_gpu = QSettings().value("ai/vulkan", false).toBool();
    connect(&DisplayColour::instance(), &DisplayColour::changed, this, &AiService::changed);
    m_timeout.setSingleShot(true); m_idle.setSingleShot(true); m_idle.setInterval(120000);
    m_process.setUnixProcessParameters(QProcess::UnixProcessFlag::CreateNewSession);
    connect(&m_timeout, &QTimer::timeout, this, [this] { stopProcess(); failed(tr("AI processing timed out. Try a smaller selection or use CPU.")); });
    connect(&m_idle, &QTimer::timeout, this, &AiService::stopProcess);
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &AiService::readOutput);
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] { m_errors += m_process.readAllStandardError(); m_errors = m_errors.right(8192); });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (m_busy && e == QProcess::FailedToStart) failed(tr("Could not start local AI tools: %1").arg(m_process.errorString()));
    });
    connect(&m_process, &QProcess::finished, this, [this](int exit, QProcess::ExitStatus) {
        if (m_stopping) return;
        readOutput();
        if (m_installing) {
            m_installing = false;
            if (exit == 0) m_repairRequired = false;
            if (exit == 0 && installed()) { setBusy(false); m_status = tr("Local AI tools are ready."); emit changed(); }
            else failed(tr("AI setup failed. %1").arg(m_errors.right(1500).trimmed()));
        } else if (m_busy && !m_sourcePending) failed(tr("The AI worker stopped. Try again with CPU selected."));
    });
    auto invalidate = [this] {
        // Creating the editable mask itself advances the edit revision.
        // Keep its draft until the worker confirms that conversion succeeded.
        if (m_acceptingMask && m_engine->imageId() == m_image) return;
        if (!m_mode.isEmpty() && !current()) { cancel(); m_status = tr("The photo changed. Start a new selection."); emit changed(); }
    };
    connect(engine, &EngineService::imageChanged, this, invalidate);
    connect(engine, &EngineService::editRevisionChanged, this, invalidate);
    m_resume.setInterval(50);
    connect(&m_resume,&QTimer::timeout,this,[this] {
        if(m_engine->imageId()!=m_image) { m_resume.stop(); setBusy(false); return; }
        if(m_engine->maintenanceReady()) { m_resume.stop(); setBusy(false); start("remove"); m_status=tr("Removal applied. Select another object, or continue editing."); emit changed(); }
    });
    connect(engine,&EngineService::aiRepairSourceReady,this,[this](int ticket,const QString &path,const QString &error) {
        m_sourcePending=false;
        if(ticket!=m_request || !current() || m_mode!="remove") { if(m_mode.isEmpty()) m_temp.reset(); return; }
        if(!error.isEmpty()) { failed(error); return; }
        m_sourcePath=path; m_status=tr("Removing the selection…"); emit changed();
        send({{"action","repair"},{"image",path},{"mask",m_temp->filePath("source.pgm")},
              {"margin",m_removalMargin},{"output",m_temp->filePath("repair.orp")}});
    });
    connect(engine,&EngineService::aiRepairReady,this,[this](int ticket,const QString &key,const QImage &preview,const QString &error) {
        m_sourcePending=false;
        if(ticket!=m_request || !current() || m_mode!="remove") { m_engine->discardAiRepair(key); if(m_mode.isEmpty()) m_temp.reset(); return; }
        if(!error.isEmpty()) { failed(error); return; }
        m_repairKey=key; m_result=key;
        { QMutexLocker lock(&m_mutex); m_preview=preview; }
        m_showResult=true; m_status=tr("Preview ready. Apply removal; all adjustments stay editable."); setBusy(false); m_idle.start();
    });
    connect(engine,&EngineService::aiSourceReady,this,[this](int ticket,const QString &path,const QString &error) {
        m_sourcePending=false;
        if(ticket!=m_request || !current() || !m_savingCopy) { if(m_mode.isEmpty()) m_temp.reset(); return; }
        if(!error.isEmpty()) { failed(error); return; }
        send({{"action","render_copy"},{"image",path},{"output",m_temp->filePath("removed.dng")}});
    });
    connect(engine, &EngineService::aiRefinementSourceReady, this, [this](int ticket, const QString &path, const QString &error) {
        m_sourcePending = false;
        if (ticket != m_request || !current() || m_mode != "mask") { if (m_mode.isEmpty()) m_temp.reset(); return; }
        if (!error.isEmpty()) { failed(error); return; }
        m_status = tr("Refining the detailed selection edge…");
        send({{"action", "refine"}, {"image", path}, {"mask", m_maskPath},
              {"output", m_temp->filePath(QString("mask-%1.png").arg(m_request + 1))}});
    });
    connect(engine, &EngineService::aiMaskAccepted, this, [this](bool ok, const QString &error) {
        if (!m_acceptingMask) return;
        m_acceptingMask = false;
        if (!ok) {
            m_revision = m_engine->editRevision();
            failed(tr("Could not create an editable mask: %1 Your selection is still available.").arg(error));
            return;
        }
        cancel();
        m_engine->setMaskShown(true);
        m_status = tr("AI mask added. Refine its paths or adjust the sliders.");
        emit changed(); emit maskCreated();
    });
    connect(engine, &EngineService::aiCopyPrepared, this, [this](int ticket, const QString &error) {
        m_sourcePending = false;
        if (ticket != m_request || !current() || m_mode != "remove") { if (m_mode.isEmpty()) m_temp.reset(); return; }
        if (!error.isEmpty()) { m_result.clear(); failed(tr("Cannot preserve the photo metadata: %1").arg(error)); return; }
        send({{"action","save"},{"image",m_temp->filePath("removed.dng")},{"output",m_savedPath},{"sidecars",Metadata::sidecarNames(m_savedPath)}});
    });
}

AiService::~AiService() { stopProcess(); }

QString AiService::result() const {
    return m_result.isEmpty() ? QString() : QString("image://engine/_ai/%1/%2").arg(m_request).arg(DisplayColour::instance().revision());
}

QString AiService::home() const {
    const QString override = qEnvironmentVariable("OMARAW_AI_HOME");
    return override.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/omaraw/ai/v2" : override;
}

bool AiService::installed() const {
    QFile ready(home() + "/ready.json"), manifest(":/ai/models.json");
    if (m_repairRequired || !QFileInfo::exists(home() + "/venv/bin/python") || !ready.open(QIODevice::ReadOnly)
        || !manifest.open(QIODevice::ReadOnly)) return false;
    const auto expected = QJsonDocument::fromJson(manifest.readAll());
    if (QJsonDocument::fromJson(ready.readAll()) != expected) return false;
    for (const auto entry : expected.object().value("models").toArray()) {
        const auto model = entry.toObject(); const QFileInfo file(home() + "/models/" + model.value("name").toString());
        if (!file.isFile() || file.size() != model.value("bytes").toInteger()) return false;
    }
    return true;
}

bool AiService::prepareScripts() {
    if (!QDir().mkpath(home() + "/scripts")) return false;
    for (const QString name : {"worker.py", "setup.py", "models.json", "LICENSES.txt"}) {
        QFile source(":/ai/" + name); QSaveFile target(home() + "/scripts/" + name);
        if (!source.open(QIODevice::ReadOnly) || !target.open(QIODevice::WriteOnly)) return false;
        const auto data = source.readAll();
        if (target.write(data) != data.size() || !target.commit()) return false;
    }
    return true;
}

bool AiService::current() const { return m_engine->imageId() == m_image && m_engine->editRevision() == m_revision && !m_engine->cropMode(); }
void AiService::setBusy(bool value) { m_busy = value; if (!value) m_timeout.stop(); emit changed(); }
void AiService::failed(const QString &message) {
    m_resume.stop();
    m_installing = false; m_status = message; setBusy(false);
    emit operationFailed(message);
    if (m_process.state() != QProcess::NotRunning && !m_sourcePending) m_idle.start();
}

void AiService::stopProcess() {
    m_idle.stop(); m_timeout.stop();
    const bool busy = m_busy; m_busy = false;
    m_stopping = true;
    if (m_process.state() != QProcess::NotRunning) {
        const auto pid = m_process.processId();
        if (pid > 0) ::kill(-pid, SIGTERM);
        if (!m_process.waitForFinished(500)) { if (pid > 0) ::kill(-pid, SIGKILL); m_process.kill(); m_process.waitForFinished(500); }
    }
    m_stopping = false; m_busy = busy; m_output.clear(); m_errors.clear();
}

void AiService::setUseGpu(bool value) {
    if (m_busy || m_gpu == value) return;
    stopProcess(); m_gpu = value; QSettings().setValue("ai/vulkan", value); emit changed();
}

void AiService::setBrushSize(double value) {
    if (!m_busy && std::isfinite(value)) { m_brushSize = qBound(.002, value, .1); emit changed(); }
}

void AiService::setRemovalMargin(double value) {
    if (m_busy || !std::isfinite(value)) return;
    value = qBound(0., value, .03);
    if (value == m_removalMargin) return;
    m_removalMargin = value;
    if (m_mode == "remove" && hasSelection()) {
        ++m_request; m_result.clear(); updateOverlay();
        m_status = tr("Edge coverage changed. Preview removal again.");
    }
    emit changed();
}

void AiService::rememberSelection() {
    m_selectionUndo.append({m_maskPath, m_points, m_box, m_inverted});
    if (m_selectionUndo.size() > 32) m_selectionUndo.removeFirst();
}

bool AiService::writeSelection(const QImage &mask) {
    const QString path = m_temp->filePath(QString("mask-%1.png").arg(++m_request));
    QImage pixels = mask; pixels.setColorSpace({});
    if (!pixels.save(path)) { failed(tr("Cannot save the selection.")); return false; }
    m_maskPath = path; m_result.clear();
    return updateOverlay();
}

bool AiService::updateOverlay() {
    m_overlay.clear();
    if (m_maskPath.isEmpty()) return true;
    QImage mask(m_maskPath); mask = mask.convertToFormat(QImage::Format_Grayscale8);
    if (mask.isNull()) { failed(tr("AI returned an unreadable selection.")); return false; }
    const int radius = m_mode == "remove" ? qRound(qMax(mask.width(), mask.height()) * m_removalMargin) : 0;
    if (radius) {
        // Separable square dilation, matching worker.removal_masks. Linear
        // time avoids UI stalls on large selections or a wide margin.
        QImage horizontal(mask.size(), QImage::Format_Grayscale8), expanded(mask.size(), QImage::Format_Grayscale8);
        auto line = [radius](int count, auto read, auto write) {
            std::deque<int> queue; int next = 0;
            for (int i = 0; i < count; ++i) {
                while (next < count && next <= i + radius) {
                    while (!queue.empty() && read(queue.back()) <= read(next)) queue.pop_back();
                    queue.push_back(next++);
                }
                while (queue.front() < i - radius) queue.pop_front();
                write(i, read(queue.front()));
            }
        };
        for (int y = 0; y < mask.height(); ++y)
            line(mask.width(), [&](int x) { return mask.constScanLine(y)[x]; }, [&](int x, uchar v) { horizontal.scanLine(y)[x] = v; });
        for (int x = 0; x < mask.width(); ++x)
            line(mask.height(), [&](int y) { return horizontal.constScanLine(y)[x]; }, [&](int y, uchar v) { expanded.scanLine(y)[x] = v; });
        mask = expanded;
    }
    QImage overlay(mask.size(), QImage::Format_ARGB32);
    bool selected = false;
    for (int y = 0; y < mask.height(); ++y) {
        auto *row = reinterpret_cast<QRgb *>(overlay.scanLine(y));
        for (int x = 0; x < mask.width(); ++x) {
            const int coverage = mask.constScanLine(y)[x]; selected |= coverage > 127;
            row[x] = qRgba(70, 190, 245, m_mode == "remove" ? (coverage > 127 ? 110 : 0) : (coverage * 110 + 127) / 255);
        }
    }
    if (!selected) { m_maskPath.clear(); return true; }
    const QString path = m_temp->filePath(QString("overlay-%1.png").arg(m_request));
    if (!overlay.save(path)) { failed(tr("Cannot show the AI selection.")); return false; }
    m_overlay = QUrl::fromLocalFile(path).toString();
    return true;
}

void AiService::brushStroke(const QVariantList &points, bool subtract) {
    if (m_busy || (m_mode != "mask" && m_mode != "remove") || !current() || points.isEmpty() || points.size() > 4096) return;
    QImage mask = hasSelection() ? QImage(m_maskPath) : QImage(m_temp->filePath("source.png"));
    if (mask.isNull()) return;
    mask = mask.convertToFormat(QImage::Format_RGB32);
    if (!hasSelection()) mask.fill(Qt::black);
    QPainterPath stroke;
    for (int i = 0; i < points.size(); ++i) {
        const auto point = points[i].toList();
        if (point.size() != 2) return;
        const double x = point[0].toDouble(), y = point[1].toDouble();
        if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || x > 1 || y < 0 || y > 1) return;
        const QPointF p(x * mask.width(), y * mask.height());
        if (i == 0) stroke.moveTo(p); else stroke.lineTo(p);
    }
    const double radius = m_brushSize * qMax(mask.width(), mask.height());
    if (m_mode == "mask" && m_objectBrush) {
        if (subtract && !hasSelection() && m_points.isEmpty() && m_box.isEmpty()) {
            m_status = tr("Paint over an object first, then right-drag to exclude unwanted areas.");
            emit changed(); return;
        }
        // A scribble supplies bounded, evenly spaced SAM prompts. Keep the
        // object boundary from inference, not the footprint of the paint.
        QPainterPathStroker stroker; stroker.setWidth(radius * 2);
        stroker.setCapStyle(Qt::RoundCap); stroker.setJoinStyle(Qt::RoundJoin);
        QPainterPath covered = stroker.createStroke(stroke);
        covered.addEllipse(stroke.currentPosition(), radius, radius);
        covered.setFillRule(Qt::WindingFill);
        QVariantList prompts = m_points;
        prompts.removeIf([&](const QVariant &entry) {
            const auto p = entry.toList();
            return p[2].toInt() == (subtract ? 1 : 0)
                && covered.contains(QPointF(p[0].toDouble()*mask.width(), p[1].toDouble()*mask.height()));
        });
        const double length = stroke.length();
        const int count = qBound(1, int(std::ceil(length / qMax(2., radius * 2))) + 1, 16);
        for (int i = 0; i < count; ++i) {
            const QPointF p = count == 1 ? stroke.currentPosition()
                : stroke.pointAtPercent(stroke.percentAtLength(length * i / (count - 1)));
            bool nearby = false;
            for (const auto &entry : prompts) {
                const auto old = entry.toList();
                nearby |= old[2].toInt() == (subtract ? 0 : 1)
                    && QLineF(p, QPointF(old[0].toDouble()*mask.width(), old[1].toDouble()*mask.height())).length() < radius / 2;
            }
            if (!nearby) prompts.append(QVariant(QVariantList{p.x()/mask.width(), p.y()/mask.height(), subtract ? 0 : 1}));
        }
        if (prompts.size() + !m_box.isEmpty() > 64) {
            m_status = tr("The selection has too many refinements. Undo a selection step or Clear to start again.");
            emit changed(); return;
        }
        rememberSelection(); m_points = prompts;
        bool included = !m_box.isEmpty();
        for (const auto &entry : m_points) included |= entry.toList()[2].toInt() == 1;
        if (!included) {
            m_maskPath.clear(); m_overlay.clear(); m_result.clear();
            m_status = tr("Selection cleared. Paint over another object to select it."); emit changed();
        } else requestMask();
        return;
    }
    { QPainter painter(&mask); const QColor colour = subtract ? Qt::black : Qt::white;
      painter.setPen(QPen(colour, radius * 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      painter.drawPath(stroke); painter.setPen(Qt::NoPen); painter.setBrush(colour);
      painter.drawEllipse(stroke.currentPosition(), radius, radius); }
    rememberSelection();
    if (!writeSelection(mask.convertToFormat(QImage::Format_Grayscale8))) return;
    m_status = m_mode == "remove"
        ? tr("Selection brushed. Include the object's shadow and reflection before previewing.")
        : tr("Selection brushed. Keep refining or create an editable mask.");
    emit changed();
}

void AiService::install() {
    if (m_busy) return;
    stopProcess();
    if (!prepareScripts()) { failed(tr("Cannot write the local AI tools folder.")); return; }
    m_installing = true; m_status = tr("Installing local AI tools…"); setBusy(true);
    m_timeout.start(20 * 60 * 1000);
    m_process.start("python3", {home() + "/scripts/setup.py", home()});
}

void AiService::start(const QString &mode) {
    if (m_busy || m_sourcePending || (mode != "mask" && mode != "remove")) return;
    if (!installed()) { failed(tr("Download local AI tools first.")); return; }
    if (!m_engine->maintenanceReady() || m_engine->cropMode()) { failed(tr("Wait for the photo to finish rendering and close Crop first.")); return; }
    const QImage image = m_engine->plainCopy().scaled(1600, 1600, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (image.isNull()) { failed(tr("Open a photo first.")); return; }
    cancel();
    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::CacheLocation));
    m_temp = std::make_unique<QTemporaryDir>(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/ai-XXXXXX");
    if (!m_temp->isValid() || !image.save(m_temp->filePath("source.png"))) { failed(tr("Cannot prepare the AI preview.")); return; }
    m_image = m_engine->imageId(); m_revision = m_engine->editRevision(); m_mode = mode;
    m_status = tr("Click the object or draw a box around it. Right-click excludes a point."); emit changed();
}

void AiService::point(double x, double y, bool subtract) {
    if (m_busy || !current() || m_mode.isEmpty() || m_points.size() >= 64 || !std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0 || x > 1 || y > 1) return;
    rememberSelection(); m_points.append(QVariant(QVariantList{x, y, subtract ? 0 : 1})); requestMask();
}
void AiService::selectBox(double x0, double y0, double x1, double y1) {
    if (m_busy || !current() || m_mode.isEmpty()) return;
    for (double v : {x0, y0, x1, y1}) if (!std::isfinite(v) || v < 0 || v > 1) return;
    if (std::abs(x1 - x0) < .002 || std::abs(y1 - y0) < .002) return;
    rememberSelection(); m_box = {qMin(x0, x1), qMin(y0, y1), qMax(x0, x1), qMax(y0, y1)}; requestMask();
}
void AiService::undoPoint() {
    if (m_busy || m_selectionUndo.isEmpty() || !current()) return;
    const auto state = m_selectionUndo.takeLast();
    m_maskPath = state.mask; m_points = state.points; m_box = state.box; m_inverted = state.inverted;
    ++m_request; m_result.clear(); updateOverlay(); m_status = tr("Selection undone."); emit changed();
}
void AiService::clear() {
    if (m_busy) return;
    if (hasSelection() || !m_points.isEmpty() || !m_box.isEmpty()) rememberSelection();
    m_points.clear(); m_box.clear(); m_maskPath.clear(); m_overlay.clear(); m_result.clear(); m_inverted = false;
    m_status = m_brushMode ? (m_mode == "mask" && m_objectBrush
                                ? tr("Paint inside an object and release to select it. Right-drag excludes unwanted areas.")
                                : tr("Paint over the area to select. Right-drag erases."))
                           : tr("Click or draw a box to select an object.");
    emit changed();
}
void AiService::invert() {
    if (m_busy || !hasSelection() || !current()) return;
    QImage mask(m_maskPath); mask.invertPixels();
    rememberSelection(); m_inverted = !m_inverted; writeSelection(mask); emit changed();
}

void AiService::refineEdges() {
    if (m_busy || m_mode != "mask" || !current() || !hasSelection()) return;
    if (!m_engine->maintenanceReady()) { failed(tr("Wait for the photo to finish rendering before refining edges.")); return; }
    // Keep the current draft visible and recoverable if refinement fails.
    rememberSelection(); m_status = tr("Preparing image detail for edge refinement…");
    const QString source = m_temp->filePath("refine-source.png");
    if (QFileInfo::exists(source)) {
        send({{"action", "refine"}, {"image", source}, {"mask", m_maskPath},
              {"output", m_temp->filePath(QString("mask-%1.png").arg(m_request + 1))}});
    } else {
        m_sourcePending = true; ++m_request; setBusy(true);
        m_engine->requestAiRefinementSource(m_request, source);
    }
}

void AiService::requestMask() {
    m_maskPath.clear(); m_overlay.clear(); m_result.clear(); m_status = tr("Finding the object…");
    send({{"action", "mask"}, {"image", m_temp->filePath("source.png")}, {"points", m_points}, {"box", m_box},
          {"invert", m_inverted}, {"output", m_temp->filePath(QString("mask-%1.png").arg(m_request + 1))}});
}

void AiService::send(const QVariantMap &request) {
    m_idle.stop();
    if (m_process.state() == QProcess::NotRunning) {
        if (!prepareScripts()) { failed(tr("Cannot prepare local AI tools.")); return; }
        m_process.start(home() + "/venv/bin/python", {"-u", home() + "/scripts/worker.py", "--home", home(), "--backend", m_gpu ? "vulkan" : "cpu"});
    }
    QVariantMap message = request; message["id"] = ++m_request; m_action = message.value("action").toString();
    setBusy(true); m_timeout.start(5 * 60 * 1000);
    m_process.write(QJsonDocument::fromVariant(message).toJson(QJsonDocument::Compact) + '\n');
}

void AiService::readOutput() {
    if (m_stopping) { m_process.readAllStandardOutput(); return; }
    m_output += m_process.readAllStandardOutput();
    if (m_output.size() > 65536) { stopProcess(); failed(tr("Invalid response from local AI tools.")); return; }
    while (m_output.contains('\n')) {
        const int end = m_output.indexOf('\n'); const auto line = m_output.left(end); m_output.remove(0, end + 1);
        const auto response = QJsonDocument::fromJson(line).object();
        if (m_installing) {
            if (response.value("event") == "progress") { m_status = response.value("message").toString(); emit changed(); }
            if (response.contains("ok") && !response.value("ok").toBool()) m_errors += response.value("error").toString().toUtf8();
            continue;
        }
        if (response.value("id").toInt(-1) != m_request || !current() || m_mode.isEmpty()) continue;
        if (!response.value("ok").toBool()) {
            if (response.value("repair_required").toBool()) m_repairRequired = true;
            failed(response.value("error").toString(tr("AI processing failed."))); continue;
        }
        if (m_action == "mask" || m_action == "refine") {
            m_maskPath = response.value("path").toString();
            if (!updateOverlay()) continue;
            m_status = m_action == "refine"
                ? (response.value("changed_pixels").toInt() > 0
                    ? tr("Edges refined. Use Undo selection to compare.")
                    : tr("No edge changes found. Use Paint for further corrections."))
                : tr("Selection ready · %1. Add points or right-click to exclude.").arg(response.value("backend").toString());
        } else if (m_action == "repair") {
            m_sourcePending=true; m_status=tr("Rendering the repair with your adjustments…");
            m_engine->previewAiRepair(m_request,response.value("path").toString()); emit changed(); continue;
        } else if(m_action=="render_copy") {
            m_sourcePending=true; m_status=tr("Preserving photo metadata…");
            m_engine->prepareAiCopy(m_request,m_engine->imagePath(),response.value("path").toString()); emit changed(); continue;
        } else if (m_action == "save") {
            const QString path = m_savedPath;
            // End the draft before opening its result; that switch is success,
            // not a stale-selection error caused by a changed photo.
            cancel();
            m_status = tr("Rendered DNG copy saved. The original and its editable repairs remain available.");
            emit changed(); emit saved(path); return;
        }
        setBusy(false); m_idle.start();
    }
}

void AiService::acceptMask() {
    if (m_busy || m_mode != "mask" || !current() || !hasSelection()) return;
    if (!m_engine->maintenanceReady()) { failed(tr("Wait for the photo to finish rendering before accepting the mask.")); return; }
    const QImage mask(m_maskPath);
    if (mask.isNull()) { failed(tr("The selection could not be read. Try Undo selection or select the object again.")); return; }
    stopProcess();
    m_acceptingMask = true;
    m_status = tr("Creating editable mask…"); setBusy(true);
    m_engine->addAiSelection(mask);
}
void AiService::remove() {
    if (m_busy || m_mode != "remove" || !current() || !hasSelection()) return;
    if (m_engine->sourceKind() == "smart-preview") { failed(tr("Reconnect the original photo before removing an object.")); return; }
    if (!m_sourcePath.isEmpty()) { QFile::remove(m_sourcePath); m_sourcePath.clear(); }
    m_status = tr("Preparing the full-resolution photo…"); setBusy(true);
    m_sourcePending = true; ++m_request;
    if(!m_repairKey.isEmpty()) { m_engine->discardAiRepair(m_repairKey); m_repairKey.clear(); }
    m_engine->requestAiRepairSource(m_request, m_temp->filePath("source"), QImage(m_maskPath));
}
void AiService::apply() {
    if(m_busy || m_result.isEmpty() || m_repairKey.isEmpty() || !current() || !m_engine->maintenanceReady()) return;
    const QString key=m_repairKey; m_repairKey.clear();
    cancel();
    m_engine->applyAiRepair(key);
    m_status=tr("Applying removal…"); emit changed();
    setBusy(true); m_timeout.start(120000); m_resume.start();
}

void AiService::saveCopy(const QString &fileOrUrl) {
    if (m_busy || m_result.isEmpty() || !current()) return;
    const QUrl url(fileOrUrl); const QString path = url.isLocalFile() ? url.toLocalFile() : fileOrUrl;
    if (QFileInfo(path).suffix().compare("dng", Qt::CaseInsensitive)) { failed(tr("Choose a .dng filename.")); return; }
    if (QFileInfo::exists(path) || QFileInfo(path).isSymLink()) { failed(tr("That file already exists. Choose a new filename.")); return; }
    for (const QString &sidecar : Metadata::sidecarNames(path))
        if (QFileInfo::exists(sidecar) || QFileInfo(sidecar).isSymLink()) { failed(tr("A sidecar already uses that name. Choose a new filename.")); return; }
    m_savedPath=QFileInfo(path).absoluteFilePath(); m_status=tr("Rendering the DNG copy…");
    m_savingCopy=true; m_sourcePending=true; ++m_request; setBusy(true);
    m_engine->exportAiRepair(m_request,m_repairKey,m_temp->filePath("copy"));
}
void AiService::cancel() {
    m_acceptingMask = false;
    m_resume.stop(); m_savingCopy=false;
    if(!m_repairKey.isEmpty()) { m_engine->discardAiRepair(m_repairKey); m_repairKey.clear(); }
    ++m_request; m_installing = false; stopProcess(); setBusy(false);
    m_mode.clear(); m_points.clear(); m_box.clear(); m_maskPath.clear(); m_overlay.clear(); m_result.clear(); m_inverted = false;
    m_selectionUndo.clear(); m_brushMode = false; m_objectBrush = true;
    { QMutexLocker lock(&m_mutex); m_preview = {}; }
    if (!m_sourcePending) m_temp.reset();
    emit changed();
}
