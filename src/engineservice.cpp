#include <functional>
#include <limits>
#include "engineservice.h"
#include "colourrange.h"
#include "exposuremeter.h"
#include "presetstate.h"
#include "resourcebudget.h"
#include "aiservice.h"
#include "denoiseservice.h"
#include "imagematch.h"
#include "imagematchservice.h"
#include "xmppreset.h"
#include "labcolour.h"
#include "cameraprofiles.h"
#include "camerastyles.h"
#include "developtools.h"
#include "developscopes.h"
#include "falsecolour.h"
#include "toneregions.h"
#include <QScopeGuard>
#include <QSettings>
#include "maskpreview.h"
#include "backend.h"
#include "metadata.h"
#include "finishing.h"
#include "proof.h"
#include "colourpipeline.h"
#include "exportstate.h"
#include "offlinestore.h"
#include <QColorSpace>

#include <QElapsedTimer>
#include <algorithm>
#include <cmath>
#include <array>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QFileInfo>
#include <QMetaObject>
#include <QUrl>
#include <QRegularExpression>
#include <QDebug>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QImageReader>
#include <QCryptographicHash>
#include <QSaveFile>
#include <QTemporaryDir>

#ifdef OMARAW_ENGINE
#include "engine/bridge.h"
#include "psdwriter.h"
#endif

// ── worker (lives on m_thread) ──────────────────────────────────────────
namespace {
// The local's adjustment stack beyond the anchor's own exposure and
// black: each row is a UI key and the module field it drives. A member
// instance is made by the bridge the first time its field is set, on
// the same mask as the anchor.
struct StackField { const char *key; const char *op; const char *field; };
const StackField *stackFields(int *count) {
    static const StackField fields[] = {
        { "contrast",   "colorbalancergb", "contrast" },
        { "highlights", "colorbalancergb", "highlights_Y" },
        { "shadows",    "colorbalancergb", "shadows_Y" },
        { "saturation", "colorbalancergb", "saturation_global" },
        { "vibrance",   "colorbalancergb", "vibrance" },
        { "hue",        "colorbalancergb", "hue_angle" },
        { "lightness",  "colorbalancergb", "brilliance_global" },
        { "warmth",     "colorbalancergb", "omaraw_warmth" },
        { "tint",       "colorbalancergb", "omaraw_tint" },
        { "clarity",    "bilat",           "detail" },
        { "sharpness",  "sharpen",         "amount" },
        { "moire",      "lowpass",         "radius" },
    };
    *count = int(sizeof fields / sizeof fields[0]);
    return fields;
}
const StackField *stackField(const QString &key) {
    int n = 0; const StackField *f = stackFields(&n);
    for (int i = 0; i < n; ++i) if (key == QLatin1String(f[i].key)) return &f[i];
    return nullptr;
}
}

#ifdef OMARAW_ENGINE
static QImage takeLinear(float *pixels, int w, int h) {
    QImage image(reinterpret_cast<uchar *>(pixels), w, h, w * 4 * int(sizeof(float)),
                 QImage::Format_RGBA32FPx4, [](void *p) { oma_engine_free(p); }, pixels);
    image.setColorSpace(QColorSpace::SRgbLinear);
    return image;
}

class PreviewRequest {
public:
    PreviewRequest(const std::atomic<int> *generation, int ticket) : m_generation(generation), m_ticket(ticket) {
        oma_engine_preview_begin([](void *request) { return int(static_cast<PreviewRequest *>(request)->cancelled()); }, this);
    }
    ~PreviewRequest() { oma_engine_preview_end(); }
    bool cancelled() const { return m_generation && m_generation->load() != m_ticket; }
    PreviewRequest(const PreviewRequest &) = delete;
    PreviewRequest &operator=(const PreviewRequest &) = delete;
private:
    const std::atomic<int> *m_generation;
    const int m_ticket;
};
#endif

// The parametric curve's region nodes and their reach live in toneregions.h.
static constexpr const double *kRegionX = ToneRegions::kX;
static constexpr double kRegionScale = ToneRegions::kScale;

// The tone-mapper rules for a batch of values (paste, preset, snapshot, sync),
// shared by the open photo and the Library's photos. A print stock that is
// (or ends up) on keeps every tone mapper parked: rows that do not carry the
// print itself (a pasted "Basic", one module's settings) cannot switch one on
// under it, and a print switched on here parks whichever was on. Without a
// print, a tone mapper switched on takes its rival off. The print's rows go
// first so the history reads "print on, tone mapper off", as a stock chosen
// from its dropdown does. `parked` gets the tone mappers this batch parked.
// Where a nudge starts on an adjustment that is off: the value that does
// nothing, not the one it lists. Exposure lists +0.7 and shadows and
// highlights ±50 while off, none of which is in the picture. Other
// adjustments start from their own default, which is what switching them
// on gives. offSibling: the other half of a pair that must start neutral
// too, or switching the module on brings its hidden default in with it.
static bool zeroEffect(const QString &op, const QString &field) {
    static const QSet<QString> fields{
        "shadhi.shadows", "shadhi.highlights", "hazeremoval.strength", "bilat.detail",
        "sharpen.amount", "grain.strength", "bloom.strength", "hotpixels.strength"
    };
    return fields.contains(op + QLatin1Char('.') + field);
}
static double controlReset(const QString &op, const QString &field, double def) {
    return zeroEffect(op, field) ? 0.0 : def;
}
static double offStart(const QString &op, const QString &field, double def) {
    if ((op == QLatin1String("exposure") && field == QLatin1String("exposure")) || op == QLatin1String("shadhi")) return 0;
    return controlReset(op, field, def);
}
static QString offSibling(const QString &op, const QString &field) {
    if (op != QLatin1String("shadhi")) return {};
    return field == QLatin1String("shadows") ? QStringLiteral("highlights") : field == QLatin1String("highlights") ? QStringLiteral("shadows") : QString();
}

static QVariantList withToneMapperRules(const QVariantList &given, const std::function<bool(const QString &)> &isOn, QStringList *parked) {
    // A preset restores its predecessor on the worker first. Its final mapper
    // must be chosen against that restored state, never a stale UI snapshot.
    if (given.size()==1 && given.first().toMap().contains("preset")) return given;
    static const QStringList all{QStringLiteral("sigmoid"), QStringLiteral("filmicrgb"), QStringLiteral("basecurve")};
    const QString print = QStringLiteral("omarawprint");
    const bool printWasOn = isOn(print);
    QStringList toneMappers;
    QHash<QString, bool> explicitStates;
    QSet<QString> parametersSet;
    QVariantList values, rest;
    for (const QVariant &v : given) {
        const QVariantMap m = v.toMap();
        const QString op = m.value(QStringLiteral("op")).toString();
        if (m.contains(QStringLiteral("enabled"))) explicitStates[op] = m.value(QStringLiteral("enabled")).toBool();
        if (m.contains(QStringLiteral("value")) || m.contains(QStringLiteral("text"))) parametersSet.insert(op);
        if (op == print) {
            values << v;
        } else rest << v;
        if (all.contains(op)) {
            if (!toneMappers.contains(op)) toneMappers << op;
        }
    }
    values << rest;
    const auto off = [](const QString &op) { return QVariantMap{{QStringLiteral("op"), op}, {QStringLiteral("enabled"), false}}; };
    // setParams applies the last explicit state after all parameter writes.
    // A saved disabled tool stays off even if its value rows follow its eye.
    const auto endsOn = [&](const QString &op) {
        return explicitStates.value(op, parametersSet.contains(op) || isOn(op));
    };
    const bool printOn = endsOn(print);
    if (!printOn) {
        for (const QString &op : std::as_const(toneMappers)) {
            const QString rival = op == QLatin1String("sigmoid") ? QStringLiteral("filmicrgb")
                                : op == QLatin1String("filmicrgb") ? QStringLiteral("sigmoid") : QString();
            if (endsOn(op) && !rival.isEmpty() && !toneMappers.contains(rival) && isOn(rival)) values << off(rival);
        }
        return values;
    }
    // Every enabled print owns tone mapping, including complete saved looks.
    for (const QString &op : std::as_const(toneMappers)) values << off(op);
    if (!printWasOn)
        for (const QString &op : all)
            if (!toneMappers.contains(op) && isOn(op)) { values << off(op); if (parked) *parked << op; }
    return values;
}

// How a unitless control reads (the figure only; history, presets and
// the command line keep the engine's own values). "strength": 0 at the
// control's default, -100..+100 at its ends, as the source editor's sliders.
// "amount": 0 for none up to 100 for the most. Controls with a real
// unit (EV, K, degrees, px, %) and choices keep their own figures.
static QString figureDisplay(const QString &op, const QString &field) {
    static const QHash<QString, QString> display = [] {
        QHash<QString, QString> d;
        for (const char *f : {"exposure.black", "sigmoid.middle_grey_contrast", "sigmoid.contrast_skewness", "hazeremoval.strength",
                              "colorbalancergb.saturation_global", "colorbalancergb.vibrance", "colorbalancergb.contrast",
                              "colorbalancergb.brilliance_global", "monochrome.highlights", "bilat.detail",
                              "omarawprint.trim_r", "omarawprint.trim_g", "omarawprint.trim_b", "omarawprint.gain_r", "omarawprint.gain_g",
                              "omarawprint.gain_b", "vignette.brightness", "vignette.saturation", "ashift.lensshift_v", "ashift.lensshift_h",
                              "colorbalancergb.chroma_global"})
            d.insert(QString::fromLatin1(f), QStringLiteral("strength"));
        for (const char *f : {"hazeremoval.distance", "monochrome.size", "sharpen.amount",
                              "denoiseprofile.strength", "hotpixels.strength"})
            d.insert(QString::fromLatin1(f), QStringLiteral("amount"));
        return d;
    }();
    return display.value(op + QLatin1Char('.') + field);
}
// The figure for an engine value and back (AdjustmentRow.qml does the same).
static double toFigure(const QString &how, double v, double lo, double def, double hi) {
    if (how == QLatin1String("amount")) return hi > lo ? (v - lo) / (hi - lo) * 100 : 0;
    return v >= def ? (hi > def ? (v - def) / (hi - def) * 100 : 0) : (def > lo ? (v - def) / (def - lo) * 100 : 0);
}
static double fromFigure(const QString &how, double f, double lo, double def, double hi) {
    if (how == QLatin1String("amount")) return lo + qBound(0.0, f, 100.0) / 100 * (hi - lo);
    f = qBound(-100.0, f, 100.0);
    return f >= 0 ? def + f / 100 * (hi - def) : def + f / 100 * (def - lo);
}

class EngineWorker : public QObject {
    Q_OBJECT
public:
    int memoryBudgetMiB = 0;
    bool initialized = false;
    QString maskDirectory;
    QSet<QString> draftRepairs;
    QString resolveManagedMask(const QString &path) const {
        static const QRegularExpression name(QStringLiteral("^[0-9a-f]{64}\\.png$"));
        const QString file = QFileInfo(path).fileName();
        const QString local = QDir(maskDirectory).filePath(file);
        return name.match(file).hasMatch() && QFileInfo(local).isFile() ? local : path;
    }
    QString retainManagedMask(const QString &path) const {
        const QString resolved = resolveManagedMask(path);
        const QFileInfo sourceInfo(resolved);
        const QString local = QDir(maskDirectory).filePath(sourceInfo.fileName());
        if (resolved.isEmpty() || sourceInfo.absoluteFilePath() == QFileInfo(local).absoluteFilePath()) return resolved;
        // A preset from another catalogue must bring its asset into this
        // catalogue's recovery boundary. Only OmaRAW-managed PNGs are accepted.
        static const QRegularExpression name(QStringLiteral("^[0-9a-f]{64}\\.png$"));
        QImageReader reader(resolved);
        const QSize size = reader.size();
        if (!name.match(sourceInfo.fileName()).hasMatch() || !size.isValid()
            || size.width() > 4096 || size.height() > 4096 || sourceInfo.size() > 64 * 1024 * 1024) return {};
        QFile source(resolved);
        if (!source.open(QIODevice::ReadOnly) || !QDir().mkpath(maskDirectory)) return {};
        QSaveFile destination(local);
        const QByteArray bytes = source.readAll();
        if (source.error() != QFile::NoError || !destination.open(QIODevice::WriteOnly)
            || destination.write(bytes) != bytes.size() || !destination.commit()) return {};
        return local;
    }
    static QStringList rasterTargets() { return {"exposure", "toneequal", "colorbalancergb", "contrastntexture", "bilat", "sharpen", "atrous", "primaries"}; }
    QString cameraStylePrefix;
    QHash<QString, QVariantMap> cameraStyleCache;
    QVariantMap readToolState(int imgid) {
        QVariantMap out;
#ifdef OMARAW_ENGINE
        out["sceneReferred"] = oma_engine_is_scene_referred(imgid) > 0;
        // A picked/custom illuminant cannot be reconstructed exactly from the
        // temperature/tint controls. Denoise copies and previews retain its xy.
        QVariantList whiteBalanceParams;
        for (const char *field : {"illuminant", "illum_fluo", "illum_led", "adaptation", "x", "y"}) {
            oma_param_info info{};
            if (!oma_engine_param_get(imgid, "channelmixerrgb", field, &info) && info.found)
                whiteBalanceParams << QVariantMap{{"op", "channelmixerrgb"}, {"field", QString::fromLatin1(field)},
                                                  {"value", double(info.value)}, {"enabled", bool(info.enabled)}};
        }
        out["whiteBalanceParams"] = whiteBalanceParams;
        QVariantList exposureParams;
        for (const char *field : {"compensate_exposure_bias", "compensate_hilite_pres"}) {
            oma_param_info info{};
            if (!oma_engine_param_get(imgid, "exposure", field, &info) && info.found)
                exposureParams << QVariantMap{{"op", "exposure"}, {"field", QString::fromLatin1(field)},
                                               {"value", double(info.value)}, {"enabled", bool(info.enabled)}};
        }
        out["denoiseExposureParams"] = exposureParams;
        float cameraMatrix[9]{}, cameraWhite = 0, highlightBias = 0;
        int sensorGeometry[6]{};
        if (!oma_engine_raw_calibration(imgid, cameraMatrix, &cameraWhite, &highlightBias, sensorGeometry)) {
            QVariantList matrix; for (float value : cameraMatrix) matrix << double(value);
            QVariantList geometry; for (int value : sensorGeometry) geometry << value;
            out["denoiseCalibration"] = QVariantMap{{"matrix", matrix}, {"white", double(cameraWhite)}, {"highlightBias", double(highlightBias)}, {"geometry", geometry}};
        }
        QVariantList cameraLookParams;
        for (const char *op : {"sigmoid", "filmicrgb", "basecurve", "colorbalancergb"}) {
            char fields[16384] = {};
            if (oma_engine_camera_look_fields(imgid, op, fields, sizeof fields)) continue;
            for (const auto &field : QByteArray(fields).split('\n')) {
                if (field.isEmpty()) continue;
                oma_param_info info{};
                if (!oma_engine_param_get(imgid, op, field.constData(), &info) && info.found)
                    cameraLookParams << QVariantMap{{"op", QString::fromLatin1(op)}, {"field", QString::fromLatin1(field)},
                                                    {"value", double(info.value)}, {"enabled", bool(info.enabled)}};
            }
        }
        out["cameraLookParams"] = cameraLookParams;
        oma_param_info printBase;
        if (!oma_engine_param_get(imgid, "omarawprint", "base_tone", &printBase) && printBase.found)
            out["printBaseTone"] = int(printBase.value);
        if (printBase.found && printBase.enabled) {
            QVariantList tones;
            for (const char *op : {"sigmoid", "filmicrgb", "basecurve"}) {
                char fields[16384] = {};
                if (oma_engine_tone_fields(imgid, op, fields, sizeof fields)) continue;
                for (const QByteArray &field : QByteArray(fields).split('\n')) {
                    if (field.isEmpty()) continue;
                    oma_param_info info{};
                    if (!oma_engine_param_get(imgid, op, field.constData(), &info) && info.found)
                        tones << QVariantMap{{"op", QString::fromLatin1(op)}, {"field", QString::fromLatin1(field)},
                                             {"value", double(info.value)}, {"enabled", false}};
                }
            }
            out["printToneParams"] = tones;
        }
        float values[800]; int enabled=0;
        const int n=oma_engine_liquify_get(imgid,values,100,&enabled);
        QVariantList warps;
        for(int i=0;i<n && i<100;++i) { QVariantList point; for(int k=0;k<8;++k) point<<double(values[i*8+k]); warps<<QVariant(point); }
        out["warps"]=warps; out["liquifyAvailable"]=n>=0; out["liquifyEnabled"]=enabled!=0;
        char folder[2048]={0},file[2048]={0};
        oma_engine_param_get_string(imgid,"rasterfile","path",folder,sizeof folder);
        oma_engine_param_get_string(imgid,"rasterfile","file",file,sizeof file);
        out["rasterFile"]=file[0]?resolveManagedMask(QDir(QString::fromUtf8(folder)).filePath(QString::fromUtf8(file))):QString();
        QVariantList targets;
        for(const auto &op:rasterTargets()) {int inverse=0;float opacity=1;
            if(oma_engine_raster_get(imgid,op.toLatin1().constData(),&inverse,&opacity)==1)
                targets<<QVariantMap{{"op",op},{"inverse",inverse!=0},{"opacity",double(opacity)}};
        }
        out["rasterTargets"]=targets;
#else
        Q_UNUSED(imgid)
#endif
        return out;
    }
    bool restoreWarps(int imgid, const QVariantList &points, bool enabled) {
#ifdef OMARAW_ENGINE
        if(points.size()>100) return false;
        QVector<float> values;
        for(const auto &point:points) {const auto p=point.toList();if(p.size()!=8)return false;for(const auto &v:p) { bool ok=false;double d=v.toDouble(&ok);if(!ok||!std::isfinite(d))return false;values<<float(d);}}
        return oma_engine_liquify_set(imgid,values.constData(),points.size(),enabled)==0;
#else
        Q_UNUSED(imgid) Q_UNUSED(points) Q_UNUSED(enabled) return false;
#endif
    }
    std::atomic<int> *generation = nullptr;
    // Reduced-quality fitted previews, owned by the service and read here.
    const std::atomic_bool *reducedPreviews = nullptr;
    const std::atomic_bool *exportFullResolution = nullptr;
    void applyExportResolution() {
#ifdef OMARAW_ENGINE
        if (exportFullResolution) oma_engine_set_export_full_resolution(exportFullResolution->load() ? 1 : 0);
#endif
    }
    std::atomic<int> *detailGeneration = nullptr;
    std::atomic<int> *snapshotGeneration = nullptr;
    int snapshotImage = -1, snapshotSourceImage = -1, snapshotId = 0;
    std::atomic_bool *offlineCancel = nullptr;
    QString proofPath; int proofIntent = 1; bool proofGamut = false;
#include "imagematchworker.inc"
public slots:
    void retouchPreview(int imgid,int gen,int serial,int scale,int width,int height) {
        QImage image;QString error;
#ifdef OMARAW_ENGINE
        PreviewRequest request(generation,gen);float *pixels=nullptr;int w=0,h=0;
        const int rc=request.cancelled()?OMA_ENGINE_CANCELLED:oma_engine_retouch_preview(imgid,scale,width,height,&pixels,&w,&h);
        if(!rc)image=ColourPipeline::srgb8(takeLinear(pixels,w,h));
        else {oma_engine_free(pixels);if(rc!=OMA_ENGINE_CANCELLED)error=QString::fromUtf8(oma_engine_error());}
#else
        Q_UNUSED(scale) Q_UNUSED(width) Q_UNUSED(height)
#endif
        emit retouchPreviewRead(imgid,gen,serial,image,error);
    }
    void toolAction(int imgid, const QString &operation, const QString &action, const QVariantMap &v) {
#ifdef OMARAW_ENGINE
        int rc=-1;
        if(operation=="liquify") {
            if(action=="add") rc=oma_engine_liquify_add(imgid,v.value("type").toInt(),v.value("x").toFloat(),v.value("y").toFloat(),v.value("dx").toFloat(),v.value("dy").toFloat(),v.value("radius",.05).toFloat(),v.value("strength",.5).toFloat());
            else if(action=="clear" || action=="remove") {
                auto state=readToolState(imgid); auto points=state.value("warps").toList();
                if(action=="clear") points.clear();
                else {int i=v.value("index",points.size()-1).toInt();if(i<0||i>=points.size()){emit failed(tr("That Liquify warp is no longer there."));emit toolStateRead(imgid,readToolState(imgid));return;}points.removeAt(i);}
                oma_engine_begin_edit(imgid); rc=restoreWarps(imgid,points,true)?0:-1;
            } else {emit failed(tr("Liquify cannot %1.").arg(action));return;}
        } else if(operation=="rasterfile") {
            const QString target=v.value("target").toString();
            if(!rasterTargets().contains(target)) {emit failed(tr("Choose a supported mask target."));return;}
            QString path=v.value("path").toString();
            if(action=="import") {
                if(path.startsWith("file:")) path=QUrl(path).toLocalFile();
                QImageReader reader(path); reader.setAutoTransform(false);
                const QSize original=reader.size();
                if(!original.isValid() || qint64(original.width())*original.height()>250000000) {emit failed(tr("Cannot read this mask image."));return;}
                if(original.width()>4096 || original.height()>4096) reader.setScaledSize(original.scaled(4096,4096,Qt::KeepAspectRatio));
                QImage mask=reader.read();
                if(mask.isNull()) {emit failed(reader.errorString());return;}
                // Flatten transparent pixels to black: alpha can only reduce
                // selection. Store a managed greyscale PNG so source moves are safe.
                QImage grey(mask.size(),QImage::Format_Grayscale8);
                mask=mask.convertToFormat(QImage::Format_ARGB32);
                for(int y=0;y<mask.height();++y) {const auto *in=reinterpret_cast<const QRgb *>(mask.constScanLine(y));auto *out=grey.scanLine(y);for(int x=0;x<mask.width();++x)out[x]=qGray(in[x])*qAlpha(in[x])/255;}
                QCryptographicHash hash(QCryptographicHash::Sha256);
                for(int y=0;y<grey.height();++y)hash.addData(QByteArrayView(reinterpret_cast<const char *>(grey.constScanLine(y)),grey.width()));
                hash.addData(QByteArray::number(grey.width())+"x"+QByteArray::number(grey.height()));
                QDir().mkpath(maskDirectory);path=QDir(maskDirectory).filePath(QString::fromLatin1(hash.result().toHex())+".png");
                if(!QFileInfo::exists(path)) {QSaveFile output(path);if(!output.open(QIODevice::WriteOnly)||!grey.save(&output,"PNG")||!output.commit()){emit failed(tr("Could not save the imported mask."));return;}}
            } else if(action=="detach") path.clear();
            else if(action!="attach") {emit failed(tr("A mask file cannot %1.").arg(action));return;}
            if (!path.isEmpty()) {
                path = retainManagedMask(path);
                if (path.isEmpty()) { emit failed(tr("Could not retain the preset's mask. Import the mask image again.")); return; }
            }
            rc=oma_engine_raster_set(imgid,path.toUtf8().constData(),target.toLatin1().constData(),v.value("inverse").toBool(),v.value("opacity",1).toFloat());
        } else if(operation=="retouch") {
            if(action=="draw") {
                const auto nodes=v.value("points").toList();if(nodes.isEmpty()||nodes.size()>2048)return;
                QVector<float> xy;
                for(const auto &node:nodes){const auto p=node.toMap();xy<<p.value("x").toFloat()<<p.value("y").toFloat();}
                xy<<v.value("sx").toFloat()<<v.value("sy").toFloat();
                if(oma_engine_mask_coordinates(imgid,xy.data(),nodes.size()+1,0))return;
                oma_param_info scale;oma_engine_param_get(imgid,"retouch","curr_scale",&scale);
                const int index=oma_engine_spot_add_drawn(imgid,v.value("algorithm",2).toInt(),v.value("shape",3).toInt(),xy.constData(),nodes.size(),v.value("radius",.03).toFloat(),v.value("border",.01).toFloat(),xy[nodes.size()*2],xy[nodes.size()*2+1],int(scale.value));
                rc=index<0?-1:0;if(index>=0)emit spotAdded(imgid,index);
            } else if(action=="scale") rc=oma_engine_spot_set_scale(imgid,v.value("index").toInt(),v.value("scale").toInt());
            else if(action=="move") {
                const int index=v.value("index").toInt();oma_spot_info spot;
                if(oma_engine_spot_get(imgid,index,&spot)||!spot.found)return;
                float xy[4]={v.value("x").toFloat(),v.value("y").toFloat(),v.value("sx").toFloat(),v.value("sy").toFloat()};
                if(oma_engine_mask_coordinates(imgid,xy,2,0))return;
                if(oma_engine_begin_edit(imgid))return;
                rc=oma_engine_spot_set(imgid,index,xy[0],xy[1],spot.radius,spot.border,xy[2],xy[3],spot.opacity);
            } else {emit failed(tr("Retouch cannot %1.").arg(action));return;}
        } else if(operation=="negadoctor" && action=="prepare") {
            oma_engine_begin_edit(imgid);
            for(const char *op:{"sigmoid","filmicrgb","basecurve"}) oma_engine_module_set_enabled(imgid,op,0);
            rc=oma_engine_module_set_enabled(imgid,"negadoctor",1);
        } else {emit failed(tr("There is no %1 action \"%2\".").arg(operation,action));return;}
        if(rc) emit failed(QString::fromUtf8(oma_engine_error()));
#else
        Q_UNUSED(imgid) Q_UNUSED(operation) Q_UNUSED(action) Q_UNUSED(v)
#endif
    }
    void rawClipping(int imgid, int gen, int serial, int width, int height, bool uncropped) {
        QImage image; QString status;
#ifdef OMARAW_ENGINE
        PreviewRequest request(generation, gen);
        uint8_t *mask = nullptr;
        int result = request.cancelled() ? OMA_ENGINE_CANCELLED : oma_engine_raw_clipping(imgid, uncropped, width, height, &mask);
        if (!result && mask) {
            image=QImage(width,height,QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent);
            const QRgb colour=qPremultiply(qRgba(255,50,190,210));
            for (int y=0;y<height;++y) { auto *row=reinterpret_cast<QRgb *>(image.scanLine(y)); for(int x=0;x<width;++x) if(mask[y*width+x]) row[x]=colour; }
            status=tr("Magenta: sensor clipping (sampled preview)");
        } else if (result==2) status=oma_engine_using_smart_preview(imgid)
            ? tr("Reconnect the original for sensor clipping.") : tr("RAW clipping requires a 16-bit Bayer or X-Trans source.");
        else if (result!=OMA_ENGINE_CANCELLED) status=QString::fromUtf8(oma_engine_error());
        oma_engine_free(mask);
#else
        Q_UNUSED(width) Q_UNUSED(height) Q_UNUSED(uncropped)
#endif
        emit rawClippingRead(imgid,gen,serial,image,status);
    }
    void offlineCopies(const QStringList &paths, bool keep, bool smart) {
        int done = 0, failed = 0;
        QString lastError;
        QElapsedTimer timer; timer.start();
        for (int index = 0; index < paths.size(); ++index) {
            if (offlineCancel && offlineCancel->load()) break;
            const QString path = paths.at(index);
            QString error;
            emit offlineStep(path, index, paths.size(), 0);
            bool ok = false;
#ifdef OMARAW_ENGINE
            const int id = keep ? oma_engine_open(path.toLocal8Bit().constData()) : -1;
            if (keep && id < 0) error = QString::fromUtf8(oma_engine_error());
            else if (smart) ok = keep ? OfflineStore::previews().createPreview(path, id, offlineCancel, &error) : OfflineStore::previews().remove(path, &error);
            else ok = keep ? OfflineStore::instance().create(path, offlineCancel, &error, [&](qint64 bytes, qint64 total) {
                if (timer.elapsed() < 100) return;
                timer.restart();
                emit offlineStep(path, index, paths.size(), total > 0 ? double(bytes) / total : 0);
            }) : OfflineStore::instance().remove(path, &error);
#else
            error = QStringLiteral("No engine");
#endif
            if (offlineCancel && offlineCancel->load() && !ok) break;
            if (ok) ++done;
            else {
                ++failed; lastError = QFileInfo(path).fileName() + ": " + error;
                qWarning().noquote() << (smart ? "Smart Preview failed:" : "Offline copy failed:") << path << error;
            }
        }
        emit offlineDone(done, failed, offlineCancel && offlineCancel->load(), lastError);
    }
    void renderDetail(int imgid, int ticket, const QString &key, const QRect &rect, const QRect &block, double scale, int original, bool uncropped) {
        QImage image;
        QString error;
#ifdef OMARAW_ENGINE
        PreviewRequest request(detailGeneration, ticket);
        if (!request.cancelled()) {
            float *pixels = nullptr; int w = 0, h = 0;
            oma_engine_region_hint(block.x(), block.y(), block.width(), block.height());
            const int rc = oma_engine_render_region_mode(imgid, original, uncropped, rect.x(), rect.y(), rect.width(), rect.height(), scale, &pixels, &w, &h);
            if (request.cancelled() || rc == OMA_ENGINE_CANCELLED) oma_engine_free(pixels);
            else if (rc == 0) {
                image = takeLinear(pixels, w, h);
                if (!proofPath.isEmpty()) image = Proof::apply(image, proofPath, proofIntent, proofGamut, &error);
            } else error = QString::fromUtf8(oma_engine_error());
        }
#else
        Q_UNUSED(imgid) Q_UNUSED(rect) Q_UNUSED(block) Q_UNUSED(scale) Q_UNUSED(original) Q_UNUSED(uncropped)
        error = QStringLiteral("No engine");
#endif
        emit detailRendered(ticket, key, image, error);
    }
    void clearSnapshot() {
#ifdef OMARAW_ENGINE
        oma_engine_comparison_remove(snapshotImage);
#endif
        snapshotImage = snapshotSourceImage = -1; snapshotId = 0;
    }
    void renderSnapshot(int imgid, int ticket, int id, const QVariantList &values) {
        QImage image; QString error; int fw = 0, fh = 0;
#ifdef OMARAW_ENGINE
        PreviewRequest request(snapshotGeneration, ticket);
        if (!request.cancelled()) {
            if (snapshotImage < 0 || snapshotSourceImage != imgid || snapshotId != id) {
                clearSnapshot();
                snapshotImage = oma_engine_comparison_create(imgid);
                if (snapshotImage >= 0) {
                    // Reads carry the disposable image id, so they cannot
                    // replace the live controls. Reject partial restoration.
                    const auto errors = connect(this, &EngineWorker::failed, this,
                        [&error](const QString &message) { if (error.isEmpty()) error = message; }, Qt::DirectConnection);
                    setParams(snapshotImage, values);
                    disconnect(errors);
                    snapshotSourceImage = imgid; snapshotId = id;
                } else error = QString::fromUtf8(oma_engine_error());
            }
            if (error.isEmpty() && !request.cancelled()) {
                float *pixels = nullptr; int w = 0, h = 0;
                if (oma_engine_preview_dimensions(snapshotImage, 2, 0, &fw, &fh) || fw <= 0 || fh <= 0)
                    error = QString::fromUtf8(oma_engine_error());
                else {
                    const double scale = qMin(1.0, 1024.0 / qMax(fw, fh));
                    const int rc = oma_engine_render_region_mode(snapshotImage, 2, 0, 0, 0,
                        qMax(1, int(std::floor(fw * scale))), qMax(1, int(std::floor(fh * scale))), scale, &pixels, &w, &h);
                    if (!rc) {
                        image = takeLinear(pixels, w, h);
                        if (!proofPath.isEmpty()) image = Proof::apply(image, proofPath, proofIntent, proofGamut, &error);
                    } else { oma_engine_free(pixels); if (rc != OMA_ENGINE_CANCELLED) error = QString::fromUtf8(oma_engine_error()); }
                }
            }
            if (!error.isEmpty() || request.cancelled() || image.isNull()) { image = {}; clearSnapshot(); }
        }
#else
        Q_UNUSED(values)
        error = tr("No processing engine");
#endif
        emit snapshotRendered(imgid, ticket, id, snapshotImage, image, fw, fh, error);
    }
    static QVariantMap denoiseValue(const QVariant &value) {
        auto row = value.toMap();
        // RAW's standard camera matrix is embedded in the linear DNG. The
        // DNG decoder stores it in the embedded-matrix slot, not RAW's camera
        // table slot. Copying type 11 verbatim can fall back to linear Rec.709.
        if (row.value("op") == "colorin" && row.value("field") == "type" && row.value("value").toInt() == 11)
            row["value"] = 10;
        return row;
    }
    void prepareDenoiseCopy(const QString &path, const QVariantList &given) {
        QString error;
#ifdef OMARAW_ENGINE
        const int id = oma_engine_open(path.toLocal8Bit().constData());
        if (id < 0) error = QString::fromUtf8(oma_engine_error());
        else {
            // Demosaicing/sensor corrections are already in the linear DNG.
            // Its own rawprepare calibration must remain the float passthrough.
            const QSet<QString> sensorOps{"rawprepare", "demosaic", "rawdenoise", "hotpixels", "cacorrect", "highlights"};
            QVariantList values; QVariantMap crop;
            for (const auto &value : given) {
                const auto row = denoiseValue(value);
                if (row.contains("denoiseCrop")) crop = row.value("denoiseCrop").toMap();
                else if (row.contains("denoiseLensOverride")) {
                    if (oma_engine_lens_override(id, row.value("denoiseLensOverride").toString().toUtf8().constData()))
                        error = QString::fromUtf8(oma_engine_error());
                } else if (!sensorOps.contains(row.value("op").toString())) values << row;
            }
            const auto errors = connect(this, &EngineWorker::failed, this,
                [&error](const QString &message) { if (error.isEmpty()) error = message; }, Qt::DirectConnection);
            if (error.isEmpty()) setParams(id, values);
            disconnect(errors);
            if (error.isEmpty() && crop.value("found").toBool()) {
                if (oma_engine_crop_set(id, crop.value("cx").toDouble(), crop.value("cy").toDouble(),
                                        crop.value("cw").toDouble(), crop.value("ch").toDouble(),
                                        crop.value("ratioN").toInt(), crop.value("ratioD").toInt()))
                    error = QString::fromUtf8(oma_engine_error());
                else if (!crop.value("enabled").toBool()) oma_engine_module_set_enabled(id, "crop", 0);
            }
        }
#else
        Q_UNUSED(given)
        error = tr("No photo engine is available.");
#endif
        emit denoiseCopyPrepared(path, error);
    }
    void denoisePreview(int ticket, const QString &directory, const QString &source, const QVariantList &given) {
        QImage images[2]; QString error;
#ifdef OMARAW_ENGINE
        // A sensor crop cannot carry whole-frame geometry or spatial masks.
        // Keep the same global colour/tone settings on both linear inputs.
        const QSet<QString> colourOps{"exposure", "temperature", "colorin", "colorout",
            "colorbalancergb", "channelmixerrgb", "shadhi", "hazeremoval", "filmicrgb", "sigmoid", "basecurve", "rgbcurve", "tonecurve",
            "colorzones", "rgblevels", "primaries", "colorequal", "negadoctor",
            "lut3d", "omarawprint", "omarawprofile", "omarawgrade", "monochrome", "toneequal"};
        QVariantList values;
        for (const auto &value : given) {
            const auto row = denoiseValue(value);
            if (colourOps.contains(row.value("op").toString()) || row.value("curveset").toBool()
                || row.value("tonecurveset").toBool() || row.value("zoneset").toBool()) values << row;
        }
        const auto errors = connect(this, &EngineWorker::failed, this,
            [&error](const QString &message) { if (error.isEmpty()) error = message; }, Qt::DirectConnection);
        for (int i = 0; i < 2 && error.isEmpty(); ++i) {
            const QString path = directory + (i ? "/after.dng" : "/before.dng");
            if (!Metadata::copyDenoiseMetadata(source, path, &error)) break;
            const int imported = oma_engine_open(path.toLocal8Bit().constData());
            const int id = imported < 0 ? -1 : oma_engine_comparison_create(imported);
            if (imported >= 0) oma_engine_remove(imported);
            if (id < 0) { error = QString::fromUtf8(oma_engine_error()); break; }
            const auto cleanup = qScopeGuard([id] { oma_engine_comparison_remove(id); });
            setParams(id, values);
            // Prevent automatic detail/lens presets from changing the comparison.
            for (const char *op : {"lens", "ashift", "crop", "clipping", "denoiseprofile", "sharpen",
                                  "atrous", "bilat", "diffuse", "grain", "bloom", "omarawhalation"})
                oma_engine_module_set_enabled(id, op, 0);
            float *pixels = nullptr; int w = 0, h = 0;
            if (error.isEmpty() && oma_engine_render(id, 2048, 2048, &pixels, &w, &h) == 0)
                images[i] = takeLinear(pixels, w, h);
            else { oma_engine_free(pixels); if (error.isEmpty()) error = QString::fromUtf8(oma_engine_error()); }
        }
        disconnect(errors);
#else
        Q_UNUSED(directory) Q_UNUSED(source) Q_UNUSED(given)
        error = tr("No photo engine is available.");
#endif
        emit denoisePreviewReady(ticket, images[0], images[1], error);
    }
    void setProof(const QString &path, int intent, bool gamut) { proofPath = path; proofIntent = intent; proofGamut = gamut; }
    void init(const QString &prefix, const QString &configDir, const QString &cacheDir, const QString &library) {
        maskDirectory=QFileInfo(library).absolutePath()+"/masks";
        cameraStylePrefix = prefix;
        cameraStyleCache.clear();
#ifdef OMARAW_ENGINE
        const int rc = oma_engine_init(prefix.toLocal8Bit().constData(), configDir.toLocal8Bit().constData(),
                                       cacheDir.toLocal8Bit().constData(), library.toLocal8Bit().constData());
        initialized = rc == 0;
        if (initialized && memoryBudgetMiB) setMemoryBudget(memoryBudgetMiB);
        emit inited(rc == 0, rc == 0 ? QString::fromUtf8(oma_engine_version()) : QString::fromUtf8(oma_engine_error()));
#else
        Q_UNUSED(prefix) Q_UNUSED(configDir) Q_UNUSED(cacheDir) Q_UNUSED(library)
        emit inited(false, QStringLiteral("built without the darktable engine"));
#endif
    }
    void setMemoryBudget(int mib) {
        memoryBudgetMiB = mib;
#ifdef OMARAW_ENGINE
        if (!initialized) return;
        const auto budget = ResourceBudget::forMemory(mib);
        oma_engine_set_memory_budget(size_t(budget.working) << 20, size_t(budget.sources) << 20,
                                     size_t(budget.contexts) << 20, size_t(budget.overviews) << 20);
#endif
    }
    void open(const QString &path, int variant) {
#ifdef OMARAW_ENGINE
        const int id = oma_engine_open_version(path.toLocal8Bit().constData(), variant);
        emit opened(path, variant, id, id < 0 ? QString::fromUtf8(oma_engine_error()) : QString());
#else
        emit opened(path, variant, -1, QStringLiteral("no engine"));
#endif
    }
    void duplicate(const QString &path, int variant) {
#ifdef OMARAW_ENGINE
        const int id = oma_engine_open_version(path.toLocal8Bit().constData(), variant);
        if (id < 0) { emit duplicated(path, variant, -1, QString::fromUtf8(oma_engine_error())); return; }
        int version = -1;
        const int nid = oma_engine_duplicate(id, &version);
        emit duplicated(path, variant, nid < 0 ? -1 : version, nid < 0 ? QString::fromUtf8(oma_engine_error()) : QString());
#else
        emit duplicated(path, variant, -1, QStringLiteral("no engine"));
#endif
    }
    void remove(const QString &path, int variant) {
#ifdef OMARAW_ENGINE
        const int id = oma_engine_open_version(path.toLocal8Bit().constData(), variant);
        if (id < 0) { emit removed(path, variant, QString::fromUtf8(oma_engine_error())); return; }
        emit removed(path, variant, oma_engine_remove(id) == 0 ? QString() : QString::fromUtf8(oma_engine_error()));
#else
        emit removed(path, variant, QStringLiteral("no engine"));
#endif
    }
    void relink(const QString &oldDir, const QString &newDir) {
#ifdef OMARAW_ENGINE
        QString error;
        if (!OfflineStore::instance().relink(oldDir, newDir, true, &error)) { emit relinked(oldDir, newDir, error); return; }
        int rc = oma_engine_relink_folder(oldDir.toLocal8Bit().constData(), newDir.toLocal8Bit().constData());
        if (!rc && !OfflineStore::previews().relink(oldDir, newDir, true, &error)) {
            oma_engine_relink_folder(newDir.toLocal8Bit().constData(), oldDir.toLocal8Bit().constData()); rc = -1;
        }
        if (rc) OfflineStore::instance().relink(newDir, oldDir, true, nullptr);
        emit relinked(oldDir, newDir, rc == 0 ? QString() : error.isEmpty() ? QString::fromUtf8(oma_engine_error()) : error);
#else
        emit relinked(oldDir, newDir, QStringLiteral("no engine"));
#endif
    }
    void move(const QString &oldPath, const QString &newPath) {
#ifdef OMARAW_ENGINE
        QString error;
        if (!OfflineStore::instance().relink(oldPath, newPath, false, &error)) { emit moved(oldPath, newPath, error); return; }
        int rc = oma_engine_move_image(oldPath.toLocal8Bit().constData(), newPath.toLocal8Bit().constData());
        if (!rc && !OfflineStore::previews().relink(oldPath, newPath, false, &error)) {
            oma_engine_move_image(newPath.toLocal8Bit().constData(), oldPath.toLocal8Bit().constData()); rc = -1;
        }
        if (rc) OfflineStore::instance().relink(newPath, oldPath, false, nullptr);
        emit moved(oldPath, newPath, rc == 0 ? QString() : error.isEmpty() ? QString::fromUtf8(oma_engine_error()) : error);
#else
        emit moved(oldPath, newPath, QStringLiteral("no engine"));
#endif
    }
    void swap(const QString &path, int a, int b) {
#ifdef OMARAW_ENGINE
        const int ia = oma_engine_open_version(path.toLocal8Bit().constData(), a);
        const int ib = ia < 0 ? -1 : oma_engine_open_version(path.toLocal8Bit().constData(), b);
        if (ib < 0) { emit swapped(path, a, b, QString::fromUtf8(oma_engine_error())); return; }
        emit swapped(path, a, b, oma_engine_swap_versions(ia, ib) == 0 ? QString() : QString::fromUtf8(oma_engine_error()));
#else
        emit swapped(path, a, b, QStringLiteral("no engine"));
#endif
    }
    // What the edit holds, for the Library's card badges.
    int editFlags(int imgid) {
        int f = 0;
#ifdef OMARAW_ENGINE
        oma_crop_info ci;
        if (oma_engine_crop_get(imgid, &ci) == 0 && ci.found && ci.enabled) f |= 1;
        if (oma_engine_local_count(imgid, "exposure") > 0) f |= 2;
        if (oma_engine_spot_count(imgid) > 0) f |= 4;
#else
        Q_UNUSED(imgid)
#endif
        return f;
    }
    void render(int imgid, int w, int h, int gen, bool uncropped) {
#ifdef OMARAW_ENGINE
        // Applied on the worker, where the engine's contexts live. The call
        // is a no-op unless the setting actually changed.
        if (reducedPreviews) oma_engine_set_reduced_previews(reducedPreviews->load() ? 1 : 0);
        PreviewRequest request(generation, gen);
        if (request.cancelled()) { emit dropped(); return; }
        QElapsedTimer t; t.start();
        float *buf = nullptr; int rw = 0, rh = 0;
        const int rc = uncropped ? oma_engine_render_uncropped(imgid, w, h, &buf, &rw, &rh)
            : qEnvironmentVariable("OMARAW_PREVIEW_CACHE") == QLatin1String("0")
                ? oma_engine_render(imgid, w, h, &buf, &rw, &rh)
                : oma_engine_render_cached(imgid, w, h, &buf, &rw, &rh);
        const qint64 engineMs = t.elapsed();
        if (request.cancelled() || rc == OMA_ENGINE_CANCELLED) { oma_engine_free(buf); emit dropped(); return; }
        if (rc == 0) {
            // Retain float linear pixels for OCIO, proofing and native detail.
            QImage img = takeLinear(buf, rw, rh);
            QImage copy = ColourPipeline::srgb8(img);
            if (const QByteArray dump = qgetenv("OMARAW_ENGINE_DUMP"); !dump.isEmpty()) {
                copy.save(QString::fromLocal8Bit(dump));
            }
            // The display encodings are made here, off the interface thread:
            // the picture as shown (the plain one unless soft proofing), and
            // what the meters read (the proof without its gamut paint).
            QImage shown = img, shownEncoded = copy, measured = copy;
            if (!proofPath.isEmpty()) {
                QString err;
                shown = Proof::apply(img, proofPath, proofIntent, proofGamut, &err);
                if (!err.isEmpty()) emit proofFailed(err);
                shownEncoded = ColourPipeline::srgb8(shown);
                measured = proofGamut ? ColourPipeline::srgb8(Proof::apply(img, proofPath, proofIntent, false, nullptr)) : shownEncoded;
            }
            int fw = 0, fh = 0;
            if (oma_engine_preview_dimensions(imgid, 0, uncropped, &fw, &fh) != 0) fw = fh = 0;
            // Where a render's time actually goes. Engine work dominates;
            // see docs/PERFORMANCE.md before optimising anything on this side.
            if (qEnvironmentVariableIsSet("OMARAW_RENDER_SPLIT"))
                qWarning("render split: total %lld ms, engine %lld ms, application %lld ms (%dx%d)",
                         t.elapsed(), engineMs, t.elapsed() - engineMs, rw, rh);
            emit rendered(imgid, gen, shown, img, copy, shownEncoded, measured, fw, fh, int(t.elapsed()), editFlags(imgid));
        } else {
            emit dropped();
            emit renderFailed(imgid, gen);
            emit failed(QString::fromUtf8(oma_engine_error()));
        }
#else
        Q_UNUSED(imgid) Q_UNUSED(w) Q_UNUSED(h) Q_UNUSED(gen) Q_UNUSED(uncropped)
        emit dropped(); emit renderFailed(imgid, gen); emit failed(QStringLiteral("no engine"));
#endif
    }
    QVariantMap readCrop(int imgid) {
        QVariantMap out;
#ifdef OMARAW_ENGINE
        oma_crop_info ci;
        if (oma_engine_crop_get(imgid, &ci) != 0 || !ci.found) return out;
        out[QStringLiteral("found")] = true; out[QStringLiteral("enabled")] = ci.enabled != 0;
        out[QStringLiteral("cx")] = double(ci.cx); out[QStringLiteral("cy")] = double(ci.cy);
        out[QStringLiteral("cw")] = double(ci.cw); out[QStringLiteral("ch")] = double(ci.ch);
        out[QStringLiteral("ratioN")] = ci.ratio_n; out[QStringLiteral("ratioD")] = ci.ratio_d;
        out[QStringLiteral("origW")] = ci.orig_w; out[QStringLiteral("origH")] = ci.orig_h;
        out[QStringLiteral("orientation")] = ci.orientation;
        out[QStringLiteral("geometryAvailable")] = oma_engine_geometry_available() != 0;
        out[QStringLiteral("constrain")] = oma_engine_geometry_crop_mode(imgid);
#else
        Q_UNUSED(imgid)
#endif
        return out;
    }
    void setCrop(int imgid, double cx, double cy, double cw, double ch, int ratioN, int ratioD, bool separateEdit) {
#ifdef OMARAW_ENGINE
        if (separateEdit && oma_engine_begin_edit(imgid)) { emit failed(QString::fromUtf8(oma_engine_error())); return; }
        if (oma_engine_crop_set(imgid, float(cx), float(cy), float(cw), float(ch), ratioN, ratioD) != 0) emit failed(QString::fromUtf8(oma_engine_error()));
        emit cropRead(imgid, readCrop(imgid));
#else
        Q_UNUSED(imgid) Q_UNUSED(cx) Q_UNUSED(cy) Q_UNUSED(cw) Q_UNUSED(ch) Q_UNUSED(ratioN) Q_UNUSED(ratioD) Q_UNUSED(separateEdit)
#endif
    }
    void wbAsShot(int imgid) {
#ifdef OMARAW_ENGINE
        if (oma_engine_wb_as_shot(imgid) != 0) emit failed(QString::fromUtf8(oma_engine_error()));
#else
        Q_UNUSED(imgid)
#endif
    }
    void geometry(int imgid, int gen, int operation, int policy, const QVariantList &guides) {
        QString error;
        bool ok = false;
#ifdef OMARAW_ENGINE
        PreviewRequest request(generation, gen);
        if (!request.cancelled()) {
            float lines[16] = {};
            for (int i = 0; i < guides.size() && i < 4; ++i) {
                const auto line = guides[i].toMap();
                lines[i*4] = float(line.value(QStringLiteral("x0")).toDouble());
                lines[i*4+1] = float(line.value(QStringLiteral("y0")).toDouble());
                lines[i*4+2] = float(line.value(QStringLiteral("x1")).toDouble());
                lines[i*4+3] = float(line.value(QStringLiteral("y1")).toDouble());
            }
            const int rc = oma_engine_geometry(imgid, operation, policy, lines, guides.size());
            ok = rc == 0;
            if (rc && rc != OMA_ENGINE_CANCELLED) error = QString::fromUtf8(oma_engine_error());
        }
#else
        Q_UNUSED(imgid) Q_UNUSED(gen) Q_UNUSED(operation) Q_UNUSED(policy) Q_UNUSED(guides)
        error = QStringLiteral("no engine");
#endif
        emit geometryFinished(imgid, gen, operation, ok, error);
    }
    void exposureMeter(int imgid, int gen, int serial) {
        ExposureMeter::Result result;
        QString error;
#ifdef OMARAW_ENGINE
        float *buf = nullptr; int w = 0, h = 0;
        double black = 0, bias = 0, base = 0;
        if (oma_engine_render_exposure_meter(imgid, &buf, &w, &h, &black, &bias, &base))
            error = QString::fromUtf8(oma_engine_error());
        // Scene whites above 1 are valid RAW highlights. Retain two stops
        // above diffuse white for the tone mapper; rendered files have no
        // such shoulder, so their limit stays below display white.
        else result = ExposureMeter::measure(takeLinear(buf, w, h), black, bias, base,
                                             oma_engine_is_scene_referred(imgid) > 0 ? 4.0 : .98);
#else
        error = tr("No processing engine");
#endif
        emit exposureMeterRead(imgid, gen, serial, result.valid, result.ev, error);
    }
    // The histogram of the picture as `op` receives it: a small render with
    // the module and everything after it switched off for the moment, so the
    // tone mapper is not in it. The curve editor draws it behind the curve
    // so the nodes sit on the tones they act on. The point curve's axis is
    // scene light on a lightness scale — L*/100, mid grey (18%) at 0.5, the
    // engine's "compensate middle grey" — and light past white piles up at
    // the right-hand end, as it does going into the curve.
    void inputHistogram(int imgid, const QString &op, int serial) {
#ifdef OMARAW_ENGINE
        float *buf = nullptr; int rw = 0, rh = 0;
        if (oma_engine_render_input(imgid, op.toLatin1().constData(), 256, 256, &buf, &rw, &rh) != 0) {
            emit failed(QString::fromUtf8(oma_engine_error()));
            emit inputHistogramRead(imgid, op, serial, {});   // always answered, so the next can be asked
            return;
        }
        const QImage img = takeLinear(buf, rw, rh);
        const auto axis = [](float y) {
            if (!(y > 0.f)) return 0;
            const float f = y > 0.008856f ? std::cbrt(y) : 7.787f * y + 16.f / 116.f;
            return std::clamp(int((116.f * f - 16.f) / 100.f * 64.f), 0, 63); };   // 64 bins, as Backend::histogramOf
        QVector<int> bins[3] = {QVector<int>(64, 0), QVector<int>(64, 0), QVector<int>(64, 0)};
        for (int y = 0; y < img.height(); ++y) {
            const float *p = reinterpret_cast<const float *>(img.constScanLine(y));
            for (int x = 0; x < img.width(); ++x, p += 4) for (int c = 0; c < 3; ++c) bins[c][axis(p[c])] += 1;
        }
        QVariantList out;
        for (int c = 0; c < 3; ++c) { QVariantList ch; for (int v : bins[c]) ch << v; out << QVariant(ch); }
        emit inputHistogramRead(imgid, op, serial, out);
#else
        Q_UNUSED(imgid) Q_UNUSED(op) Q_UNUSED(serial)
#endif
    }
    // The neutral's chromaticity as colour calibration will see it: the
    // float render without the module, linear Rec.709 → XYZ (D65) → Bradford to
    // D50 → xy. fx < 0 means the whole picture.
    void wbFromPatch(int imgid, double fx, double fy) {
#ifdef OMARAW_ENGINE
        float *buf = nullptr; int rw = 0, rh = 0;
        if (oma_engine_render_without(imgid, "channelmixerrgb", 320, 240, &buf, &rw, &rh) != 0) { emit failed(QString::fromUtf8(oma_engine_error())); return; }
        QImage img = takeLinear(buf, rw, rh);
        const QImage c = img;
        double R = 0, G = 0, B = 0; int n = 0;
        int x0 = 0, y0 = 0, x1 = c.width(), y1 = c.height();
        if (fx >= 0 && fy >= 0) {
            const int cx = int(fx * c.width()), cy = int(fy * c.height()), r = qMax(2, c.width() / 64);
            x0 = qMax(0, cx - r); x1 = qMin(c.width(), cx + r + 1); y0 = qMax(0, cy - r); y1 = qMin(c.height(), cy + r + 1);
        }
        for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) { const float *p = reinterpret_cast<const float *>(c.constScanLine(y)) + 4*x; R += p[0]; G += p[1]; B += p[2]; ++n; }
        if (n == 0) { emit failed(QStringLiteral("nothing under the picker")); return; }
        R /= n; G /= n; B /= n;
        // sRGB (D65) → XYZ, then Bradford D65 → D50
        const double X65 = 0.4124564 * R + 0.3575761 * G + 0.1804375 * B, Y65 = 0.2126729 * R + 0.7151522 * G + 0.0721750 * B, Z65 = 0.0193339 * R + 0.1191920 * G + 0.9503041 * B;
        const double X = 1.0478112 * X65 + 0.0228866 * Y65 - 0.0501270 * Z65, Y = 0.0295424 * X65 + 0.9904844 * Y65 - 0.0170491 * Z65, Z = -0.0092345 * X65 + 0.0150436 * Y65 + 0.7521316 * Z65;
        const double sum = X + Y + Z;
        if (sum <= 1e-6 || Y < 0.002) { emit failed(QStringLiteral("too dark to read a neutral there")); return; }
        if (oma_engine_wb_set_xy(imgid, float(X / sum), float(Y / sum)) != 0) emit failed(QString::fromUtf8(oma_engine_error()));
#else
        Q_UNUSED(imgid) Q_UNUSED(fx) Q_UNUSED(fy)
#endif
    }
    void renderOriginal(int imgid, int serial, int w, int h) {
#ifdef OMARAW_ENGINE
        float *buf = nullptr; int rw = 0, rh = 0;
        if (oma_engine_render_original(imgid, w, h, &buf, &rw, &rh) == 0) {
            QImage img = takeLinear(buf, rw, rh);
            int fw = 0, fh = 0;
            oma_engine_preview_dimensions(imgid, 1, 0, &fw, &fh);
            QString error;
            if (!proofPath.isEmpty()) img = Proof::apply(img, proofPath, proofIntent, proofGamut, &error);
            emit originalRendered(imgid, serial, img, ColourPipeline::srgb8(img), fw, fh);
            if (!error.isEmpty()) emit failed(error);
        } else { emit dropped(); emit failed(QString::fromUtf8(oma_engine_error())); }
#else
        Q_UNUSED(imgid) Q_UNUSED(serial) Q_UNUSED(w) Q_UNUSED(h)
        emit dropped(); emit failed(QStringLiteral("no engine"));
#endif
    }
    void renderMaskCoverage(int imgid, int priority, int serial, int w, int h) {
#ifdef OMARAW_ENGINE
        float *buf = nullptr; int rw = 0, rh = 0;
        if (oma_engine_render_mask(imgid, "exposure", priority, w, h, &buf, &rw, &rh) == 0) {
            QImage img = takeLinear(buf, rw, rh);
            emit maskCoverageRendered(imgid, priority, serial, img);
        } else {
            // Complete the counted request and release the mask-input guard.
            emit maskCoverageRendered(imgid, priority, serial, QImage());
            emit failed(QString::fromUtf8(oma_engine_error()));
        }
#else
        Q_UNUSED(w) Q_UNUSED(h)
        emit maskCoverageRendered(imgid, priority, serial, QImage());
        emit failed(QStringLiteral("no engine"));
#endif
    }
    void pickRange(int imgid, int priority, int channel, double x, double y) {
#ifdef OMARAW_ENGINE
        float value = 0; oma_range_info range{};
        if (oma_engine_pick_range(imgid, priority, channel, float(x), float(y), &value)
            || oma_engine_range_get(imgid, "exposure", priority, channel, &range)
            || oma_engine_range_set(imgid, "exposure", priority, channel, 1, range.inverse,
                qBound(0.f, value-.15f, 1.f), qBound(0.f, value-.1f, 1.f),
                qBound(0.f, value+.1f, 1.f), qBound(0.f, value+.15f, 1.f)))
            emit failed(QString::fromUtf8(oma_engine_error()));
#else
        Q_UNUSED(imgid) Q_UNUSED(priority) Q_UNUSED(channel) Q_UNUSED(x) Q_UNUSED(y)
#endif
    }
    void pickColourRange(int imgid, int priority, double x, double y, double x2, double y2, bool newLocal) {
#ifdef OMARAW_ENGINE
        if (newLocal) priority = oma_engine_local_add(imgid, "exposure", 1, .5f, .5f, .2f, .1f, 0);
        if (priority < 0) { emit failed(QString::fromUtf8(oma_engine_error())); return; }
        float hue = 0, hueSpread = 0, chroma = 0, chromaSpread = 0;
        QString error;
        if (oma_engine_pick_range_area(imgid, priority, 1, x, y, x2, y2, &hue, &hueSpread)
            || oma_engine_pick_range_area(imgid, priority, 2, x, y, x2, y2, &chroma, &chromaSpread))
            error = QString::fromUtf8(oma_engine_error());
        else if (chroma < .001f) error = tr("That area has very little colour. Sample a coloured midtone, or use a luminance range.");
        if (error.isEmpty()) {
            const auto band = ColourRange::hue(hue, qBound(.05, 2.4*double(hueSpread), .65), .025);
            const double radius = qMax(.035, qMax(double(chroma)*.8, double(chromaSpread)*1.5));
            const double low = qMax(double(chroma)*.15, double(chroma)-radius), high = qMin(1.0, double(chroma)+radius);
            if (oma_engine_range_set(imgid, "exposure", priority, 1, 1, band.inverse, band.p0, band.p1, band.p2, band.p3)
                || oma_engine_range_set(imgid, "exposure", priority, 2, 1, 0, qMax(0.0,low-qMax(.002,low*.5)), low, high, qMin(1.0,high+.04)))
                error = QString::fromUtf8(oma_engine_error());
            if (error.isEmpty() && newLocal) {
                if (oma_engine_shape_remove(imgid, "exposure", priority, 0)
                    || oma_engine_local_rename(imgid, "exposure", priority, "Colour range"))
                    error = QString::fromUtf8(oma_engine_error());
            }
        }
        if (!error.isEmpty()) {
            if (newLocal) oma_engine_local_remove(imgid, "exposure", priority);
            emit failed(error);
        } else if (newLocal) emit localAdded(imgid, priority);
        emit localsRead(imgid, readLocals(imgid));
#else
        Q_UNUSED(imgid) Q_UNUSED(priority) Q_UNUSED(x) Q_UNUSED(y) Q_UNUSED(x2) Q_UNUSED(y2) Q_UNUSED(newLocal)
#endif
    }
    void setColourRange(int imgid, int priority, double width, double softness) {
#ifdef OMARAW_ENGINE
        oma_range_info old{};
        if (oma_engine_range_get(imgid, "exposure", priority, 1, &old)) { emit failed(QString::fromUtf8(oma_engine_error())); return; }
        const double centre = old.inverse ? (old.p0+old.p3)/2.0 + .5 : (old.p1+old.p2)/2.0;
        const auto band = ColourRange::hue(centre, width/360., softness/360.);
        if (oma_engine_range_set(imgid, "exposure", priority, 1, 1, band.inverse, band.p0, band.p1, band.p2, band.p3))
            emit failed(QString::fromUtf8(oma_engine_error()));
        emit localsRead(imgid, readLocals(imgid));
#else
        Q_UNUSED(imgid) Q_UNUSED(priority) Q_UNUSED(width) Q_UNUSED(softness)
#endif
    }
    // The read-back that follows a jump says so first, so the service can
    // tell it from an edit's however many jumps are queued behind it.
    void readParamsAfterJump(int imgid, const QVariantList &curated, int target) {
        emit jumpRead(imgid, target);
        readParams(imgid, curated);
    }
    // The curated controls of one photo as the engine has them.
    QVariantList curatedValues(int imgid, const QVariantList &curated) {
        QVariantList out;
#ifdef OMARAW_ENGINE
        QSet<QString> configured;
        const int end = oma_engine_history_end(imgid);
        for (int i = 0; i < end; ++i)
            configured.insert(QString::fromUtf8(oma_engine_history_op(imgid, i)));
        for (const QVariant &v : curated) {
            QVariantMap m = v.toMap();
            oma_param_info info;
            const QString op = m.value(QStringLiteral("op")).toString(), field = m.value(QStringLiteral("field")).toString();
            if (op == QLatin1String("channelmixerrgb") && field == QLatin1String("tint")) {
                // Not a module field: derived from the custom illuminant's xy.
                float t = 0, tint = 0;
                oma_engine_param_get(imgid, "channelmixerrgb", "temperature", &info);
                if (!info.found || oma_engine_wb_get(imgid, &t, &tint) != 0) continue;
                info.value = tint; info.min = -100; info.max = 100; info.def = 0; info.type = 0;
            } else if (isTexture(op, field)) {
                float a = 0; int on = 0;
                memset(&info, 0, sizeof info);
                if (oma_engine_texture_get(imgid, &a, &on) != 0) continue;
                info.found = 1; info.enabled = on; info.value = a; info.min = -100; info.max = 100; info.def = 0; info.type = 0;
            } else {
                oma_engine_param_get(imgid, op.toLatin1().constData(), field.toLatin1().constData(), &info);
            }
            if (!info.found) continue;
            if (m.value(QStringLiteral("kind")).toString() == QLatin1String("file")) {
                char text[1024] = {0};
                oma_engine_param_get_string(imgid, op.toLatin1().constData(), field.toLatin1().constData(), text, sizeof text);
                m[QStringLiteral("text")] = QString::fromUtf8(text);
            }
            if (info.type == 3) {
                // An enum row carries its choices so the panel can show a dropdown.
                QVariantList options;
                const int n = oma_engine_param_enum_count(imgid, op.toLatin1().constData(), field.toLatin1().constData());
                for (int i = 0; i < n; ++i) {
                    int value = 0;
                    const char *label = oma_engine_param_enum_option(imgid, op.toLatin1().constData(), field.toLatin1().constData(), i, &value);
                    if (!label) continue;
                    // The working space must be linear RGB for the scene-referred
                    // tools (exposure, the tone mapper, the print) to be right:
                    // linear Rec. 709, linear Rec. 2020, and darktable's ProPhoto,
                    // which is linear. XYZ, Lab, display, PQ/HLG and the rest are
                    // not offered; a photo already using one still shows it.
                    if (op == QLatin1String("colorin") && field == QLatin1String("type_work")
                        && value != 3 && value != 4 && value != 21 && value != int(info.value)) continue;
                    // A superseded table stays for the photos already on it; new
                    // work is not offered it. The module marks those itself, so a
                    // later revision needs no numbers changed here.
                    if (op == QLatin1String("omarawprint") && field == QLatin1String("stock")
                        && (QByteArray(label).contains("(first version)") || QByteArray(label).contains("(second version)"))
                        && value != int(info.value)) continue;
                    // A camera profile comes in with a preset; chosen here it
                    // would have no tables.
                    if (op == QLatin1String("omarawprint") && field == QLatin1String("stock") && value == 22 && value != int(info.value)) continue;
                    QVariantMap o; o[QStringLiteral("label")] = QString::fromUtf8(label); o[QStringLiteral("value")] = value;
                    options << o;
                }
                if (op == QLatin1String("flip") && field == QLatin1String("orientation")) {
                    // Introspection includes aliases for the same pixel
                    // transform. Offer each once, with rotations before mirrors.
                    QVariantList orientations;
                    for (int value : {-1, 0, 6, 5, 3, 2, 1, 4, 7})
                        for (const auto &option : options)
                            if (option.toMap().value(QStringLiteral("value")).toInt() == value) {
                                orientations << option; break;
                            }
                    options = orientations;
                }
                m[QStringLiteral("options")] = options;
            }
            m[QStringLiteral("value")] = double(info.value);
            m[QStringLiteral("min")] = m.value("uiMin", double(info.min));
            m[QStringLiteral("max")] = m.value("uiMax", double(info.max));
            m[QStringLiteral("def")] = double(info.def);
            // Keep stored/native values intact for presets and bypass. The
            // handle of an unapplied effect rests at no effect; reset uses
            // the same neutral point, not the engine's suggested strength.
            if (zeroEffect(op, field)) {
                m[QStringLiteral("idle")] = 0.0;
                m[QStringLiteral("reset")] = 0.0;
            }
            m[QStringLiteral("configured")] = configured.contains(op);
            m[QStringLiteral("enabled")] = info.enabled != 0;
            m[QStringLiteral("type")] = info.type;
            m[QStringLiteral("module")] = QString::fromUtf8(oma_engine_module_name(imgid, m.value(QStringLiteral("op")).toString().toLatin1().constData()));
            out << m;
        }
#else
        Q_UNUSED(imgid) Q_UNUSED(curated)
#endif
        return out;
    }
    void readParams(int imgid, const QVariantList &curated) {
#ifdef OMARAW_ENGINE
        const QVariantList out = curatedValues(imgid, curated);
        QVariantList hist;
        const int n = oma_engine_history_count(imgid);
        for (int i = 0; i < n; ++i) {
            QVariantMap h;
            h[QStringLiteral("op")] = QString::fromUtf8(oma_engine_history_op(imgid, i));
            h[QStringLiteral("label")] = QString::fromUtf8(oma_engine_history_label(imgid, i));
            h[QStringLiteral("enabled")] = oma_engine_history_enabled(imgid, i) != 0;
            hist << h;
        }
        // Base curve is no curated row, but a print must still park it.
        oma_param_info bc;
        emit basecurveRead(imgid, oma_engine_param_get(imgid, "basecurve", "exposure_stops", &bc) == 0 && bc.found && bc.enabled);
        emit paramsRead(imgid, out, hist, oma_engine_history_end(imgid));
        emit toolStateRead(imgid,readToolState(imgid));
        emit localsRead(imgid, readLocals(imgid));
        emit spotsRead(imgid, readSpots(imgid));
        emit curveRead(imgid, readCurve(imgid));
        emit zonesRead(imgid, readZones(imgid));
        emit cropRead(imgid, readCrop(imgid));
        emit parametricRead(imgid, readParametric(imgid));
        emit lensRead(imgid, readLens(imgid));
        readCameraProfiles(imgid);
#else
        Q_UNUSED(imgid) Q_UNUSED(curated)
#endif
    }
    // Temperature and tint are one setting on the colour calibration
    // module: change the given half, keep the other.
    bool setWhiteBalance(int imgid, const QString &field, double value) {
#ifdef OMARAW_ENGINE
        float t = 5003.f, tint = 0.f;
        // Without the current pair, writing one half would reset the other.
        if (oma_engine_wb_get(imgid, &t, &tint) != 0) return false;
        if (field == QLatin1String("temperature")) t = float(value); else tint = float(value);
        return oma_engine_wb_set(imgid, t, tint) == 0;
#else
        Q_UNUSED(imgid) Q_UNUSED(field) Q_UNUSED(value)
        return false;
#endif
    }
    static bool isWhiteBalance(const QString &op, const QString &field) {
        return op == QLatin1String("channelmixerrgb") && (field == QLatin1String("temperature") || field == QLatin1String("tint"));
    }
    // Texture is not a field of diffuse: one number drives its whole preset.
    static bool isTexture(const QString &op, const QString &field) {
        return op == QLatin1String("diffuse") && field == QLatin1String("texture");
    }
    // The shapes in one local's mask group, in the order they combine.
#ifdef OMARAW_ENGINE
    QVector<oma_path_node> penNodes(int imgid, const QVariantList &nodes, bool displayed) {
        if (nodes.size() < 3 || nodes.size() > 512) return {};
        QVector<float> xy;
        for (const auto &value : nodes) {
            const auto n = value.toMap();
            for (const char *key : {"x", "y", "inX", "inY", "outX", "outY"}) {
                bool ok = false; const double v = n.value(QLatin1String(key)).toDouble(&ok);
                if (!ok || !std::isfinite(v) || std::abs(v) > 8) return {};
                xy << float(v);
            }
        }
        if (displayed && oma_engine_mask_coordinates(imgid, xy.data(), nodes.size()*3, 0)) return {};
        QVector<oma_path_node> out;
        for (int i = 0; i < xy.size(); i += 6) out << oma_path_node{xy[i],xy[i+1],xy[i+2],xy[i+3],xy[i+4],xy[i+5]};
        return out;
    }
#endif
    QVariantList readShapes(int imgid, int priority) {
        QVariantList out;
#ifdef OMARAW_ENGINE
        const int n = oma_engine_shape_count(imgid, "exposure", priority);
        for (int i = 0; i < n; ++i) {
            oma_shape_info si;
            if (oma_engine_shape_get(imgid, "exposure", priority, i, &si) != 0 || !si.found) continue;
            QVariantMap m;
            m[QStringLiteral("index")] = i;
            m[QStringLiteral("shape")] = si.shape;
            m[QStringLiteral("combine")] = si.combine;
            m[QStringLiteral("inverse")] = si.inverse != 0;
            m[QStringLiteral("cx")] = double(si.cx); m[QStringLiteral("cy")] = double(si.cy);
            m[QStringLiteral("radius")] = double(si.radius); m[QStringLiteral("border")] = double(si.border);
            m[QStringLiteral("rotation")] = double(si.rotation); m[QStringLiteral("compression")] = double(si.compression);
            m[QStringLiteral("opacity")] = double(si.opacity);
            m[QStringLiteral("size")] = double(si.size); m[QStringLiteral("hardness")] = double(si.hardness);
            m[QStringLiteral("flow")] = double(si.density);
            m[QStringLiteral("enabled")] = si.enabled != 0;
            m[QStringLiteral("name")] = QString::fromUtf8(si.name);
            if (si.shape == OMA_SHAPE_BRUSH) {
                // The overlay draws the stroke, so the nodes ride along.
                QVector<float> buf(si.points * 2);
                const int got = oma_engine_shape_brush_points(imgid, "exposure", priority, i, buf.data(), si.points);
                QVariantList pts;
                for (int k = 0; k < got; ++k) pts << QVariantMap{{QStringLiteral("x"), double(buf[k * 2])}, {QStringLiteral("y"), double(buf[k * 2 + 1])}};
                m[QStringLiteral("points")] = pts;
            } else if (si.shape == OMA_SHAPE_PATH) {
                QVector<oma_path_node> nodes(qBound(0, si.points, 512));
                const int got = oma_engine_shape_path_nodes(imgid, "exposure", priority, i, nodes.data(), nodes.size());
                QVariantList raw, shown; QVector<float> xy;
                for (int k = 0; k < got; ++k) {
                    const auto &p = nodes[k];
                    raw << QVariantMap{{"x",p.x},{"y",p.y},{"inX",p.in_x},{"inY",p.in_y},{"outX",p.out_x},{"outY",p.out_y}};
                    xy << p.x << p.y << p.in_x << p.in_y << p.out_x << p.out_y;
                }
                if (got > 0 && !oma_engine_mask_coordinates(imgid, xy.data(), got*3, 1))
                    for (int k = 0; k < xy.size(); k += 6)
                        shown << QVariantMap{{"x",xy[k]},{"y",xy[k+1]},{"inX",xy[k+2]},{"inY",xy[k+3]},{"outX",xy[k+4]},{"outY",xy[k+5]}};
                m["points"] = raw; m["displayPoints"] = shown;
            }
            out << m;
        }
#else
        Q_UNUSED(imgid) Q_UNUSED(priority)
#endif
        return out;
    }
    // The three parametric range channels of one local, in UI order.
    QVariantList readRanges(int imgid, int priority) {
        QVariantList out;
#ifdef OMARAW_ENGINE
        static const char *names[] = { "luma", "hue", "chroma" };
        for (int ch = 0; ch < 3; ++ch) {
            oma_range_info ri;
            if (oma_engine_range_get(imgid, "exposure", priority, ch, &ri) != 0) continue;
            QVariantMap m;
            m[QStringLiteral("channel")] = ch;
            m[QStringLiteral("name")] = QString::fromLatin1(names[ch]);
            m[QStringLiteral("active")] = ri.active != 0;
            m[QStringLiteral("inverse")] = ri.inverse != 0;
            m[QStringLiteral("p0")] = double(ri.p0); m[QStringLiteral("p1")] = double(ri.p1);
            m[QStringLiteral("p2")] = double(ri.p2); m[QStringLiteral("p3")] = double(ri.p3);
            out << m;
        }
#else
        Q_UNUSED(imgid) Q_UNUSED(priority)
#endif
        return out;
    }
    QVariantList readSpots(int imgid) {
        QVariantList out;
#ifdef OMARAW_ENGINE
        const int n = oma_engine_spot_count(imgid);
        for (int i = 0; i < n; ++i) {
            oma_spot_info si;
            if (oma_engine_spot_get(imgid, i, &si) != 0 || !si.found) continue;
            QVariantMap m;
            m[QStringLiteral("index")] = i;
            m[QStringLiteral("algorithm")] = si.algorithm;
            m[QStringLiteral("cx")] = double(si.cx); m[QStringLiteral("cy")] = double(si.cy);
            m[QStringLiteral("radius")] = double(si.radius); m[QStringLiteral("border")] = double(si.border);
            m[QStringLiteral("sx")] = double(si.sx); m[QStringLiteral("sy")] = double(si.sy);
            m[QStringLiteral("opacity")] = double(si.opacity);
            m[QStringLiteral("blurRadius")] = double(si.blur_radius); m[QStringLiteral("fillBrightness")] = double(si.fill_brightness);
            m["shape"]=si.shape; m["scale"]=si.scale;
            float view[4]={si.cx,si.cy,si.sx,si.sy};
            if(!oma_engine_mask_coordinates(imgid,view,2,1)){m["displayCx"]=view[0];m["displayCy"]=view[1];m["displaySx"]=view[2];m["displaySy"]=view[3];}
            if(si.shape==3 || si.shape==4) {
                QVector<float> points(qMin(si.points,2048)*2);
                const int got=oma_engine_spot_points(imgid,i,points.data(),points.size()/2);
                QVariantList raw,shown;
                for(int k=0;k<got && k<points.size()/2;++k)raw<<QVariantMap{{"x",points[k*2]},{"y",points[k*2+1]}};
                m["points"]=raw;
                if(got>0 && got<=points.size()/2 && !oma_engine_mask_coordinates(imgid,points.data(),got,1))
                    for(int k=0;k<got;++k)shown<<QVariantMap{{"x",points[k*2]},{"y",points[k*2+1]}};
                m["displayPoints"]=shown;
            }
            out << m;
        }
#else
        Q_UNUSED(imgid)
#endif
        return out;
    }
    QVariantMap readCurve(int imgid) {
        QVariantMap out;
#ifdef OMARAW_ENGINE
        QVariantList channels;
        for (int ch = 0; ch < 3; ++ch) {
            oma_curve_info ci;
            if (oma_engine_curve_get(imgid, ch, &ci) != 0 || !ci.found) return out;
            QVariantMap c;
            QVariantList xs, ys, samples;
            for (int i = 0; i < ci.count; ++i) { xs << double(ci.x[i]); ys << double(ci.y[i]); }
            float y[65];
            if (oma_engine_curve_sample(imgid, ch, y, 65) == 0) for (float v : y) samples << double(v);
            c[QStringLiteral("count")] = ci.count; c[QStringLiteral("type")] = ci.type;
            c[QStringLiteral("xs")] = xs; c[QStringLiteral("ys")] = ys; c[QStringLiteral("samples")] = samples;
            channels << c;
            if (ch == 0) { out[QStringLiteral("enabled")] = ci.enabled != 0; out[QStringLiteral("linked")] = ci.linked != 0; }
        }
        out[QStringLiteral("found")] = true;
        out[QStringLiteral("channels")] = channels;
#else
        Q_UNUSED(imgid)
#endif
        return out;
    }
    // Band value from a channel's nodes: linear between the two nodes around
    // the band's hue, periodic in hue. Exact when the panel wrote them.
    static double zoneValueAt(const QVariantList &xs, const QVariantList &ys, double hue) {
        const int n = int(xs.size());
        if (n == 0) return 0.5;
        if (n == 1) return ys[0].toDouble();
        int i = 0;
        while (i < n && xs[i].toDouble() < hue) ++i;
        double x0, y0, x1, y1;
        if (i == 0) { x0 = xs[n - 1].toDouble() - 1.0; y0 = ys[n - 1].toDouble(); x1 = xs[0].toDouble(); y1 = ys[0].toDouble(); }
        else if (i == n) { x0 = xs[n - 1].toDouble(); y0 = ys[n - 1].toDouble(); x1 = xs[0].toDouble() + 1.0; y1 = ys[0].toDouble(); }
        else { x0 = xs[i - 1].toDouble(); y0 = ys[i - 1].toDouble(); x1 = xs[i].toDouble(); y1 = ys[i].toDouble(); }
        const double t = x1 > x0 ? (hue - x0) / (x1 - x0) : 0.0;
        return y0 + (y1 - y0) * t;
    }
    QVariantMap readZones(int imgid) {
        QVariantMap out;
#ifdef OMARAW_ENGINE
        oma_zones_info zi;
        if (oma_engine_zones_get(imgid, &zi) != 0 || !zi.found) return out;
        QVariantList channels;
        for (int ch = 0; ch < 3; ++ch) {
            QVariantList xs, ys;
            for (int i = 0; i < zi.count[ch]; ++i) { xs << double(zi.x[ch][i]); ys << double(zi.y[ch][i]); }
            channels << QVariantMap{{QStringLiteral("xs"), xs}, {QStringLiteral("ys"), ys}};
        }
        QVariantList bands;
        for (const QVariant &bv : EngineService::zoneBands()) {
            QVariantMap b = bv.toMap();
            const double hue = b.value(QStringLiteral("hue")).toDouble();
            const QVariantMap c0 = channels[0].toMap(), c1 = channels[1].toMap(), c2 = channels[2].toMap();
            // Rounded to a hundredth: the module keeps single-precision nodes.
            auto v = [](double d) { return std::round(d * 100.0) / 100.0; };
            b[QStringLiteral("lum")] = v((zoneValueAt(c0.value(QStringLiteral("xs")).toList(), c0.value(QStringLiteral("ys")).toList(), hue) - 0.5) * 400.0);
            b[QStringLiteral("sat")] = v((zoneValueAt(c1.value(QStringLiteral("xs")).toList(), c1.value(QStringLiteral("ys")).toList(), hue) - 0.5) * 200.0);
            b[QStringLiteral("shift")] = v((zoneValueAt(c2.value(QStringLiteral("xs")).toList(), c2.value(QStringLiteral("ys")).toList(), hue) - 0.5) * 1200.0);
            bands << b;
        }
        out[QStringLiteral("found")] = true;
        out[QStringLiteral("enabled")] = zi.enabled != 0;
        out[QStringLiteral("bands")] = bands;
        out[QStringLiteral("channels")] = channels;
#else
        Q_UNUSED(imgid)
#endif
        return out;
    }
    // An unknown node layout reads by interpolation at the region nodes.
    QVariantMap readParametric(int imgid) {
        QVariantMap out;
#ifdef OMARAW_ENGINE
        oma_curve_info ci;
        if (oma_engine_lcurve_get(imgid, &ci) != 0 || !ci.found) return out;
        QVariantList xs, ys;
        for (int i = 0; i < ci.count; ++i) { xs << double(ci.x[i]); ys << double(ci.y[i]); }
        static const char *keys[] = {"highlights", "lights", "darks", "shadows"};
        for (int r = 0; r < 4; ++r) {
            const double y = ci.count >= 2 ? zoneValueAt(xs, ys, kRegionX[r]) : kRegionX[r];
            out[QString::fromLatin1(keys[r])] = std::round((y - kRegionX[r]) / kRegionScale * 100.0 * 100.0) / 100.0;
        }
        out[QStringLiteral("found")] = true; out[QStringLiteral("enabled")] = ci.enabled != 0;
        out[QStringLiteral("xs")] = xs; out[QStringLiteral("ys")] = ys;
        // The module's colour curves travel with it: Lab a, then b.
        QVariantList ab;
        for (int ch = 1; ch <= 2; ++ch) {
            oma_curve_info lab;
            if (oma_engine_labcurve_get(imgid, ch, &lab) != 0 || !lab.found) { ab.clear(); break; }
            QVariantList lx, ly, samples;
            for (int i = 0; i < lab.count; ++i) { lx << double(lab.x[i]); ly << double(lab.y[i]); }
            float y[65];
            if (oma_engine_labcurve_sample(imgid, ch, y, 65) == 0) for (float v : y) samples << double(v);
            ab << QVariantMap{{QStringLiteral("xs"), lx}, {QStringLiteral("ys"), ly}, {QStringLiteral("samples"), samples}, {QStringLiteral("type"), lab.type}};
            out[QStringLiteral("labMode")] = lab.linked == 0;
        }
        out[QStringLiteral("ab")] = ab;
#else
        Q_UNUSED(imgid)
#endif
        return out;
    }
    void setLabCurve(int imgid, int channel, const QVariantList &xs, const QVariantList &ys, int type) {
#ifdef OMARAW_ENGINE
        float x[OMA_CURVE_MAX], y[OMA_CURVE_MAX];
        const int n = qMin(int(qMin(xs.size(), ys.size())), int(OMA_CURVE_MAX));
        for (int i = 0; i < n; ++i) { x[i] = float(xs[i].toDouble()); y[i] = float(ys[i].toDouble()); }
        if (oma_engine_labcurve_set(imgid, channel, x, y, n, type) != 0) emit failed(QString::fromUtf8(oma_engine_error()));
        emit parametricRead(imgid, readParametric(imgid));
#else
        Q_UNUSED(imgid) Q_UNUSED(channel) Q_UNUSED(xs) Q_UNUSED(ys) Q_UNUSED(type)
#endif
    }
    void setLabCurves(int imgid, const QVariantList &xa, const QVariantList &ya, const QVariantList &xb, const QVariantList &yb, int type) {
#ifdef OMARAW_ENGINE
        float ax[OMA_CURVE_MAX], ay[OMA_CURVE_MAX], bx[OMA_CURVE_MAX], by[OMA_CURVE_MAX];
        const int na = qMin(int(qMin(xa.size(), ya.size())), int(OMA_CURVE_MAX)), nb = qMin(int(qMin(xb.size(), yb.size())), int(OMA_CURVE_MAX));
        for (int i = 0; i < na; ++i) { ax[i] = float(xa[i].toDouble()); ay[i] = float(ya[i].toDouble()); }
        for (int i = 0; i < nb; ++i) { bx[i] = float(xb[i].toDouble()); by[i] = float(yb[i].toDouble()); }
        if (oma_engine_labcurves_set(imgid, ax, ay, na, bx, by, nb, type) != 0) emit failed(QString::fromUtf8(oma_engine_error()));
        emit parametricRead(imgid, readParametric(imgid));
#else
        Q_UNUSED(imgid) Q_UNUSED(xa) Q_UNUSED(ya) Q_UNUSED(xb) Q_UNUSED(yb) Q_UNUSED(type)
#endif
    }
    void setLCurve(int imgid, const QVariantList &xs, const QVariantList &ys) {
#ifdef OMARAW_ENGINE
        float x[OMA_CURVE_MAX], y[OMA_CURVE_MAX];
        const int n = qMin(int(qMin(xs.size(), ys.size())), int(OMA_CURVE_MAX));
        for (int i = 0; i < n; ++i) { x[i] = float(xs[i].toDouble()); y[i] = float(ys[i].toDouble()); }
        if (oma_engine_lcurve_set(imgid, x, y, n) != 0) emit failed(QString::fromUtf8(oma_engine_error()));
        emit parametricRead(imgid, readParametric(imgid));
#else
        Q_UNUSED(imgid) Q_UNUSED(xs) Q_UNUSED(ys)
#endif
    }
    void setZones(int imgid, int channel, const QVariantList &xs, const QVariantList &ys) {
#ifdef OMARAW_ENGINE
        float x[OMA_CURVE_MAX], y[OMA_CURVE_MAX];
        const int n = qMin(int(qMin(xs.size(), ys.size())), int(OMA_CURVE_MAX));
        for (int i = 0; i < n; ++i) { x[i] = float(xs[i].toDouble()); y[i] = float(ys[i].toDouble()); }
        if (oma_engine_zones_set(imgid, channel, x, y, n) != 0) emit failed(QString::fromUtf8(oma_engine_error()));
        emit zonesRead(imgid, readZones(imgid));
#else
        Q_UNUSED(imgid) Q_UNUSED(channel) Q_UNUSED(xs) Q_UNUSED(ys)
#endif
    }
    void setCurve(int imgid, int channel, const QVariantList &xs, const QVariantList &ys, int type) {
#ifdef OMARAW_ENGINE
        float x[OMA_CURVE_MAX], y[OMA_CURVE_MAX];
        const int n = qMin(int(qMin(xs.size(), ys.size())), int(OMA_CURVE_MAX));
        for (int i = 0; i < n; ++i) { x[i] = float(xs[i].toDouble()); y[i] = float(ys[i].toDouble()); }
        if (oma_engine_curve_set(imgid, channel, x, y, n, type) != 0) emit failed(QString::fromUtf8(oma_engine_error()));
        emit curveRead(imgid, readCurve(imgid));
#else
        Q_UNUSED(imgid) Q_UNUSED(channel) Q_UNUSED(xs) Q_UNUSED(ys) Q_UNUSED(type)
#endif
    }
    void setCurveLinked(int imgid, bool linked) {
#ifdef OMARAW_ENGINE
        if (oma_engine_curve_set_linked(imgid, linked ? 1 : 0) != 0) emit failed(QString::fromUtf8(oma_engine_error()));
        emit curveRead(imgid, readCurve(imgid));
#else
        Q_UNUSED(imgid) Q_UNUSED(linked)
#endif
    }
    void addSpot(int imgid, int algorithm, double cx, double cy, double radius, double border, double sx, double sy) {
#ifdef OMARAW_ENGINE
        float xy[4]={float(cx),float(cy),float(sx),float(sy)};
        if(oma_engine_mask_coordinates(imgid,xy,2,0))return;
        oma_param_info scale;oma_engine_param_get(imgid,"retouch","curr_scale",&scale);
        const int i = oma_engine_spot_add_drawn(imgid, algorithm, 1, xy, 1, float(radius), float(border), xy[2], xy[3],int(scale.value));
        if (i < 0) emit failed(QString::fromUtf8(oma_engine_error()));
        else emit spotAdded(imgid, i);
        emit spotsRead(imgid, readSpots(imgid));
#else
        Q_UNUSED(imgid) Q_UNUSED(algorithm) Q_UNUSED(cx) Q_UNUSED(cy) Q_UNUSED(radius) Q_UNUSED(border) Q_UNUSED(sx) Q_UNUSED(sy)
#endif
    }
    void setSpot(int imgid, int index, double cx, double cy, double radius, double border, double sx, double sy, double opacity) {
#ifdef OMARAW_ENGINE
        if (oma_engine_spot_set(imgid, index, float(cx), float(cy), float(radius), float(border), float(sx), float(sy), float(opacity)) != 0)
            emit failed(QString::fromUtf8(oma_engine_error()));
        emit spotsRead(imgid, readSpots(imgid));
#else
        Q_UNUSED(imgid) Q_UNUSED(index) Q_UNUSED(cx) Q_UNUSED(cy) Q_UNUSED(radius) Q_UNUSED(border) Q_UNUSED(sx) Q_UNUSED(sy) Q_UNUSED(opacity)
#endif
    }
    void setSpotAlgorithm(int imgid, int index, int algorithm, double blurRadius, double fillBrightness) {
#ifdef OMARAW_ENGINE
        if (oma_engine_spot_set_algorithm(imgid, index, algorithm, float(blurRadius), float(fillBrightness)) != 0)
            emit failed(QString::fromUtf8(oma_engine_error()));
        emit spotsRead(imgid, readSpots(imgid));
#else
        Q_UNUSED(imgid) Q_UNUSED(index) Q_UNUSED(algorithm) Q_UNUSED(blurRadius) Q_UNUSED(fillBrightness)
#endif
    }
    void removeSpot(int imgid, int index) {
#ifdef OMARAW_ENGINE
        if (oma_engine_spot_remove(imgid, index) != 0) emit failed(QString::fromUtf8(oma_engine_error()));
        emit spotsRead(imgid, readSpots(imgid));
#else
        Q_UNUSED(imgid) Q_UNUSED(index)
#endif
    }
    QVariantList readLocals(int imgid) {
        QVariantList out;
#ifdef OMARAW_ENGINE
        const int n = oma_engine_local_count(imgid, "exposure");
        for (int i = 0; i < n; ++i) {
            oma_local_info li;
            if (oma_engine_local_get(imgid, "exposure", i, &li) != 0 || !li.found) continue;
            QVariantMap m;
            m[QStringLiteral("priority")] = li.priority;
            m[QStringLiteral("name")] = QString::fromUtf8(li.name);
            m[QStringLiteral("shape")] = li.shape;
            m[QStringLiteral("cx")] = double(li.cx); m[QStringLiteral("cy")] = double(li.cy);
            m[QStringLiteral("radius")] = double(li.radius); m[QStringLiteral("border")] = double(li.border);
            m[QStringLiteral("rotation")] = double(li.rotation); m[QStringLiteral("compression")] = double(li.compression);
            m[QStringLiteral("opacity")] = double(li.opacity);
            m[QStringLiteral("enabled")] = li.enabled != 0;
            m[QStringLiteral("shapes")] = readShapes(imgid, li.priority);
            m[QStringLiteral("ranges")] = readRanges(imgid, li.priority);
            m[QStringLiteral("maskInverted")] = oma_engine_mask_get_invert(imgid, "exposure", li.priority) == 1;
            oma_refine_info rf;
            if (oma_engine_refine_get(imgid, "exposure", li.priority, &rf) == 0) {
                m[QStringLiteral("blur")] = double(rf.blur); m[QStringLiteral("feather")] = double(rf.feather);
                m[QStringLiteral("guide")] = rf.guide;
                m[QStringLiteral("maskContrast")] = double(rf.contrast); m[QStringLiteral("maskBrightness")] = double(rf.brightness);
            }
            oma_param_info pi;
            for (const char *f : {"exposure", "black"}) {
                if (oma_engine_param_get_instance(imgid, "exposure", li.priority, f, &pi) == 0 && pi.found) m[QString::fromLatin1(f)] = double(pi.value);
            }
            int nf = 0; const StackField *sf = stackFields(&nf);
            for (int k = 0; k < nf; ++k) {
                const bool found = oma_engine_stack_param_get(imgid, "exposure", li.priority, sf[k].op, sf[k].field, &pi) == 0 && pi.found;
                m[QString::fromLatin1(sf[k].key)] = found ? double(pi.value) : 0.0;
            }
            out << m;
        }
#else
        Q_UNUSED(imgid)
#endif
        return out;
    }
    void addLocal(int imgid, int shape) {
#ifdef OMARAW_ENGINE
        const int p = shape == 1 ? oma_engine_local_add(imgid, "exposure", 1, 0.5f, 0.5f, 0.2f, 0.1f, 0.f)
                                 : oma_engine_local_add(imgid, "exposure", 2, 0.5f, 0.3f, 0.5f, 0.f, 0.f);
        if (p < 0) emit failed(QString::fromUtf8(oma_engine_error()));
        emit localAdded(imgid, p);
        emit localsRead(imgid, readLocals(imgid));
#else
        Q_UNUSED(imgid) Q_UNUSED(shape)
#endif
    }
    void setLocalMask(int imgid, int priority, double cx, double cy, double a, double b, double rotation, double opacity) {
#ifdef OMARAW_ENGINE
        if (oma_engine_local_set_mask(imgid, "exposure", priority, float(cx), float(cy), float(a), float(b), float(rotation), float(opacity)) != 0)
            emit failed(QString::fromUtf8(oma_engine_error()));
        emit localsRead(imgid, readLocals(imgid));
#else
        Q_UNUSED(imgid) Q_UNUSED(priority) Q_UNUSED(cx) Q_UNUSED(cy) Q_UNUSED(a) Q_UNUSED(b) Q_UNUSED(rotation) Q_UNUSED(opacity)
#endif
    }
    void setLocalParam(int imgid, int priority, const QString &field, double value) {
#ifdef OMARAW_ENGINE
        oma_param_info anchor{};
        if (oma_engine_param_get_instance(imgid, "exposure", priority, "exposure", &anchor) == 0
            && anchor.found && !anchor.enabled)
            oma_engine_local_set_enabled(imgid, "exposure", priority, 1);
        const StackField *sf = stackField(field);
        const int rc = sf ? oma_engine_stack_param_set(imgid, "exposure", priority, sf->op, sf->field, float(value))
                          : oma_engine_param_set_instance(imgid, "exposure", priority, field.toLatin1().constData(), float(value));
        if (rc != 0) emit failed(QString::fromUtf8(oma_engine_error()));
        emit localsRead(imgid, readLocals(imgid));
#else
        Q_UNUSED(imgid) Q_UNUSED(priority) Q_UNUSED(field) Q_UNUSED(value)
#endif
    }
    void setLocalColour(int imgid, int priority, double warmth, double tint) {
#ifdef OMARAW_ENGINE
        oma_param_info anchor{};
        if (oma_engine_param_get_instance(imgid, "exposure", priority, "exposure", &anchor) == 0
            && anchor.found && !anchor.enabled)
            oma_engine_local_set_enabled(imgid, "exposure", priority, 1);
        // Both axes land before read-back or rendering. The service groups
        // the pair into one gesture, including the first stack creation.
        if (oma_engine_stack_param_set(imgid, "exposure", priority, "colorbalancergb", "omaraw_warmth", float(warmth))
            || oma_engine_stack_param_set(imgid, "exposure", priority, "colorbalancergb", "omaraw_tint", float(tint)))
            emit failed(QString::fromUtf8(oma_engine_error()));
        emit localsRead(imgid, readLocals(imgid));
#else
        Q_UNUSED(imgid) Q_UNUSED(priority) Q_UNUSED(warmth) Q_UNUSED(tint)
#endif
    }
    void setLocalEnabled(int imgid, int priority, bool on) {
#ifdef OMARAW_ENGINE
        if (oma_engine_local_set_enabled(imgid, "exposure", priority, on ? 1 : 0) != 0) emit failed(QString::fromUtf8(oma_engine_error()));
        emit localsRead(imgid, readLocals(imgid));
#else
        Q_UNUSED(imgid) Q_UNUSED(priority) Q_UNUSED(on)
#endif
    }
    void removeLocal(int imgid, int priority) {
#ifdef OMARAW_ENGINE
        // A step on top (the local goes off and lets go of its mask), so Undo
        // brings it back.
        if (oma_engine_local_remove(imgid, "exposure", priority) != 0) emit failed(QString::fromUtf8(oma_engine_error()));
        emit localsRead(imgid, readLocals(imgid));
#else
        Q_UNUSED(imgid) Q_UNUSED(priority)
#endif
    }
    void addShape(int imgid, int priority, int shape) {
#ifdef OMARAW_ENGINE
        const int i = shape == OMA_SHAPE_CIRCLE ? oma_engine_shape_add(imgid, "exposure", priority, shape, 0.5f, 0.5f, 0.2f, 0.1f, 0.f)
                                                : oma_engine_shape_add(imgid, "exposure", priority, shape, 0.5f, 0.3f, 0.5f, 0.f, 0.f);
        if (i < 0) emit failed(QString::fromUtf8(oma_engine_error()));
        else emit shapeAdded(imgid, priority, i);
        emit localsRead(imgid, readLocals(imgid));
#else
        Q_UNUSED(imgid) Q_UNUSED(priority) Q_UNUSED(shape)
#endif
    }
    void addBrushStroke(int imgid, int priority, const QVariantList &nodes, double size, double hardness, double flow) {
#ifdef OMARAW_ENGINE
        QVector<float> pts;
        for (const QVariant &v : nodes) { const QVariantMap m = v.toMap(); pts << float(m.value(QStringLiteral("x")).toDouble()) << float(m.value(QStringLiteral("y")).toDouble()); }
        if (pts.isEmpty()) return;
        const int i = oma_engine_shape_add_brush(imgid, "exposure", priority, pts.constData(), pts.size() / 2, float(size), float(hardness), float(flow));
        if (i < 0) emit failed(QString::fromUtf8(oma_engine_error()));
        else emit shapeAdded(imgid, priority, i);
        emit localsRead(imgid, readLocals(imgid));
#else
        Q_UNUSED(imgid) Q_UNUSED(priority) Q_UNUSED(nodes) Q_UNUSED(size) Q_UNUSED(hardness) Q_UNUSED(flow)
#endif
    }
    void addPenPath(int imgid, int priority, const QVariantList &nodes, double feather, bool newLocal) {
#ifdef OMARAW_ENGINE
        const auto path = penNodes(imgid, nodes, true);
        if (path.isEmpty() || !std::isfinite(feather) || feather < 0 || feather > 1) { emit failed(tr("A pen mask needs at least three valid points.")); return; }
        int index = 0;
        if (newLocal) priority = oma_engine_local_add_path(imgid, "exposure", path.constData(), path.size(), float(feather));
        else index = oma_engine_shape_add_path(imgid, "exposure", priority, path.constData(), path.size(), float(feather));
        if (priority < 0 || index < 0) emit failed(QString::fromUtf8(oma_engine_error()));
        else {
            if (newLocal) emit localAdded(imgid, priority);
            emit shapeAdded(imgid, priority, index);
        }
        emit localsRead(imgid, readLocals(imgid));
#else
        Q_UNUSED(imgid) Q_UNUSED(priority) Q_UNUSED(nodes) Q_UNUSED(feather) Q_UNUSED(newLocal)
#endif
    }
    void addAiSelection(int imgid, const QImage &selection) {
#ifdef OMARAW_ENGINE
        const QImage mask = selection.convertToFormat(QImage::Format_Grayscale8);
        const int priority = oma_engine_local_add_selection(imgid, mask.constBits(), mask.width(), mask.height(), mask.bytesPerLine());
        const QString error = priority < 0 ? QString::fromUtf8(oma_engine_error()) : QString();
        if (priority >= 0) { emit localAdded(imgid, priority); emit shapeAdded(imgid, priority, 0); }
        emit localsRead(imgid, readLocals(imgid)); emit aiMaskAccepted(imgid, priority >= 0, error);
#else
        Q_UNUSED(imgid) Q_UNUSED(selection)
        emit aiMaskAccepted(imgid, false, tr("No photo engine is available."));
#endif
    }
    void aiRepairSource(int imgid, int gen, int ticket, const QString &stem, const QImage &selection) {
        QString error;
#ifdef OMARAW_ENGINE
        const QImage mask=selection.convertToFormat(QImage::Format_Grayscale8);
        if(generation && gen!=generation->load()) error=tr("The photo changed.");
        else if(oma_engine_repair_source(imgid,(stem+".ors").toUtf8().constData(),(stem+".pgm").toUtf8().constData(),mask.constBits(),mask.width(),mask.height(),mask.bytesPerLine()))
            error=QString::fromUtf8(oma_engine_error());
#else
        Q_UNUSED(imgid) Q_UNUSED(gen) Q_UNUSED(selection)
        error=tr("No photo engine is available.");
#endif
        emit aiRepairSourceReady(ticket,stem+".ors",error);
    }
    QString repairDirectory() const { return QFileInfo(maskDirectory).absolutePath()+"/repairs"; }
    void discardRepair(const QString &key) {
        if(draftRepairs.remove(key)) QFile::remove(repairDirectory()+"/"+key+".orp");
    }
    void aiRepairPreview(int imgid,int gen,int ticket,const QString &patch) {
        QString error,key; QImage image;
#ifdef OMARAW_ENGINE
        QFile input(patch); QCryptographicHash hash(QCryptographicHash::Sha256);
        if(generation && gen!=generation->load()) error=tr("The photo changed.");
        else if(!input.open(QIODevice::ReadOnly) || !hash.addData(&input)) error=tr("Cannot read the generated repair.");
        else {
            key=QString::fromLatin1(hash.result().toHex());
            const QString destination=repairDirectory()+"/"+key+".orp";
            if(!QFileInfo::exists(destination)) {
                QDir().mkpath(repairDirectory()); QSaveFile out(destination); input.seek(0);
                bool ok=out.open(QIODevice::WriteOnly);
                while(ok && !input.atEnd()) { const auto bytes=input.read(1024*1024); ok=!bytes.isEmpty() && out.write(bytes)==bytes.size(); }
                if(!ok || !out.commit()) error=tr("Cannot save the generated repair.");
                else draftRepairs.insert(key);
            }
            if(error.isEmpty()) {
                const int copy=oma_engine_comparison_create(imgid);
                if(copy<0 || oma_engine_param_set_string(copy,"omarawrepair","file",key.toUtf8().constData()) || oma_engine_repair_check(copy))
                    error=QString::fromUtf8(oma_engine_error());
                else {
                    float *pixels=nullptr; int w=0,h=0;
                    if(oma_engine_render(copy,1600,1600,&pixels,&w,&h)) error=QString::fromUtf8(oma_engine_error());
                    else image=takeLinear(pixels,w,h);
                }
                if(copy>=0) oma_engine_comparison_remove(copy);
            }
        }
#else
        Q_UNUSED(imgid) Q_UNUSED(gen) Q_UNUSED(patch)
        error=tr("No photo engine is available.");
#endif
        if(!error.isEmpty()) discardRepair(key);
        emit aiRepairReady(ticket,key,image,error);
    }
    void aiRepairExport(int imgid,int ticket,const QString &key,const QString &stem) {
        QString error,path;
#ifdef OMARAW_ENGINE
        const int copy=oma_engine_comparison_create(imgid);
        if(copy<0 || oma_engine_param_set_string(copy,"omarawrepair","file",key.toUtf8().constData()) || oma_engine_repair_check(copy))
            error=QString::fromUtf8(oma_engine_error());
        else {
            char output[4096]={}; oma_engine_set_export_full_resolution(1);
            if(oma_engine_export_profile(copy,stem.toUtf8().constData(),"tiff",0,0,100,16,0,0,nullptr,0,1,output,sizeof output)) error=QString::fromUtf8(oma_engine_error());
            else path=QString::fromUtf8(output);
            applyExportResolution();
        }
        if(copy>=0) oma_engine_comparison_remove(copy);
#else
        Q_UNUSED(imgid) Q_UNUSED(key) Q_UNUSED(stem)
        error=tr("No photo engine is available.");
#endif
        emit aiSourceReady(ticket,path,error);
    }
    void aiSource(int imgid, int gen, int ticket, const QString &stem) {
        QString path, error;
#ifdef OMARAW_ENGINE
        if (generation && gen != generation->load()) error = tr("The photo changed.");
        else {
            oma_engine_set_export_full_resolution(1);
            char out[4096] = {0};
            const int rc = oma_engine_export_profile(imgid, stem.toLocal8Bit().constData(), "tiff", 0, 0, 100, 16,
                                                      OMA_META_EXIF | OMA_META_METADATA | OMA_META_GEOTAG | OMA_META_TAG, 0, nullptr, 0, 1, out, sizeof out);
            applyExportResolution();
            if (rc) error = QString::fromUtf8(oma_engine_error()); else path = QString::fromLocal8Bit(out);
        }
#else
        Q_UNUSED(imgid) Q_UNUSED(gen) Q_UNUSED(stem)
        error = tr("No photo engine is available.");
#endif
        emit aiSourceReady(ticket, path, error);
    }
    void aiRefinementSource(int imgid, int gen, int ticket, const QString &path) {
        QString error;
#ifdef OMARAW_ENGINE
        float *pixels = nullptr; int width = 0, height = 0;
        if (generation && gen != generation->load()) error = tr("The photo changed.");
        else {
            // Refinement needs real image detail, independent of the fitted
            // viewport and its reduced-preview setting. Keep memory bounded.
            oma_engine_set_reduced_previews(0);
            const int rc = oma_engine_render(imgid, 4096, 4096, &pixels, &width, &height);
            if (reducedPreviews) oma_engine_set_reduced_previews(reducedPreviews->load() ? 1 : 0);
            if (rc) { oma_engine_free(pixels); error = QString::fromUtf8(oma_engine_error()); }
            else {
                const QImage source = ColourPipeline::srgb8(takeLinear(pixels, width, height));
                QSaveFile file(path);
                if (!file.open(QIODevice::WriteOnly) || !source.save(&file, "PNG") || !file.commit())
                    error = tr("Cannot save the detailed refinement source.");
            }
        }
#else
        Q_UNUSED(imgid) Q_UNUSED(gen)
        error = tr("No photo engine is available.");
#endif
        emit aiRefinementSourceReady(ticket, path, error);
    }
    void setPenPath(int imgid, int priority, int index, const QVariantList &nodes, double feather) {
#ifdef OMARAW_ENGINE
        const auto path = penNodes(imgid, nodes, true);
        if (path.isEmpty() || !std::isfinite(feather) || feather < 0 || feather > 1) { emit failed(tr("A pen mask needs at least three valid points.")); return; }
        if (oma_engine_begin_edit(imgid) || oma_engine_shape_set_path(imgid, "exposure", priority, index, path.constData(), path.size(), float(feather)))
            emit failed(QString::fromUtf8(oma_engine_error()));
        emit localsRead(imgid, readLocals(imgid));
#else
        Q_UNUSED(imgid) Q_UNUSED(priority) Q_UNUSED(index) Q_UNUSED(nodes) Q_UNUSED(feather)
#endif
    }
    void setShape(int imgid, int priority, int index, double cx, double cy, double a, double b, double rotation, double opacity, bool separateEdit) {
#ifdef OMARAW_ENGINE
        if (separateEdit && oma_engine_begin_edit(imgid)) { emit failed(QString::fromUtf8(oma_engine_error())); return; }
        if (oma_engine_shape_set(imgid, "exposure", priority, index, float(cx), float(cy), float(a), float(b), float(rotation), float(opacity)) != 0)
            emit failed(QString::fromUtf8(oma_engine_error()));
        emit localsRead(imgid, readLocals(imgid));
#else
        Q_UNUSED(imgid) Q_UNUSED(priority) Q_UNUSED(index) Q_UNUSED(cx) Q_UNUSED(cy) Q_UNUSED(a) Q_UNUSED(b) Q_UNUSED(rotation) Q_UNUSED(opacity) Q_UNUSED(separateEdit)
#endif
    }
    void setShapeCombine(int imgid, int priority, int index, int combine, bool inverse) {
#ifdef OMARAW_ENGINE
        if (oma_engine_shape_set_combine(imgid, "exposure", priority, index, combine, inverse ? 1 : 0) != 0)
            emit failed(QString::fromUtf8(oma_engine_error()));
        emit localsRead(imgid, readLocals(imgid));
#else
        Q_UNUSED(imgid) Q_UNUSED(priority) Q_UNUSED(index) Q_UNUSED(combine) Q_UNUSED(inverse)
#endif
    }
    void setRange(int imgid, int priority, int channel, bool active, bool inverse, double p0, double p1, double p2, double p3) {
#ifdef OMARAW_ENGINE
        if (oma_engine_range_set(imgid, "exposure", priority, channel, active ? 1 : 0, inverse ? 1 : 0,
                                 float(p0), float(p1), float(p2), float(p3)) != 0)
            emit failed(QString::fromUtf8(oma_engine_error()));
        emit localsRead(imgid, readLocals(imgid));
#else
        Q_UNUSED(imgid) Q_UNUSED(priority) Q_UNUSED(channel) Q_UNUSED(active) Q_UNUSED(inverse) Q_UNUSED(p0) Q_UNUSED(p1) Q_UNUSED(p2) Q_UNUSED(p3)
#endif
    }
    void setMaskRefine(int imgid, int priority, double blur, double feather, int guide, double contrast, double brightness) {
#ifdef OMARAW_ENGINE
        oma_refine_info rf; rf.blur = float(blur); rf.feather = float(feather); rf.guide = guide; rf.contrast = float(contrast); rf.brightness = float(brightness);
        if (oma_engine_refine_set(imgid, "exposure", priority, &rf) != 0) emit failed(QString::fromUtf8(oma_engine_error()));
        emit localsRead(imgid, readLocals(imgid));
#else
        Q_UNUSED(imgid) Q_UNUSED(priority) Q_UNUSED(blur) Q_UNUSED(feather) Q_UNUSED(guide) Q_UNUSED(contrast) Q_UNUSED(brightness)
#endif
    }
    void setMaskInverted(int imgid, int priority, bool on) {
#ifdef OMARAW_ENGINE
        if (oma_engine_mask_set_invert(imgid, "exposure", priority, on ? 1 : 0) != 0) emit failed(QString::fromUtf8(oma_engine_error()));
        emit localsRead(imgid, readLocals(imgid));
#else
        Q_UNUSED(imgid) Q_UNUSED(priority) Q_UNUSED(on)
#endif
    }
    void removeShape(int imgid, int priority, int index) {
#ifdef OMARAW_ENGINE
        if (oma_engine_shape_remove(imgid, "exposure", priority, index) != 0) emit failed(QString::fromUtf8(oma_engine_error()));
        emit localsRead(imgid, readLocals(imgid));
#else
        Q_UNUSED(imgid) Q_UNUSED(priority) Q_UNUSED(index)
#endif
    }
    void duplicateShape(int imgid, int priority, int index) {
#ifdef OMARAW_ENGINE
        const int i = oma_engine_shape_duplicate(imgid, "exposure", priority, index);
        if (i < 0) emit failed(QString::fromUtf8(oma_engine_error()));
        else emit shapeAdded(imgid, priority, i);
        emit localsRead(imgid, readLocals(imgid));
#else
        Q_UNUSED(imgid) Q_UNUSED(priority) Q_UNUSED(index)
#endif
    }
    void moveShape(int imgid, int priority, int from, int to) {
#ifdef OMARAW_ENGINE
        if (oma_engine_shape_move(imgid, "exposure", priority, from, to) != 0) emit failed(QString::fromUtf8(oma_engine_error()));
        else emit shapeAdded(imgid, priority, to);
        emit localsRead(imgid, readLocals(imgid));
#else
        Q_UNUSED(imgid) Q_UNUSED(priority) Q_UNUSED(from) Q_UNUSED(to)
#endif
    }
    void setShapeEnabled(int imgid, int priority, int index, bool on) {
#ifdef OMARAW_ENGINE
        if (oma_engine_shape_set_enabled(imgid, "exposure", priority, index, on ? 1 : 0) != 0) emit failed(QString::fromUtf8(oma_engine_error()));
        emit localsRead(imgid, readLocals(imgid));
#else
        Q_UNUSED(imgid) Q_UNUSED(priority) Q_UNUSED(index) Q_UNUSED(on)
#endif
    }
    void renameShape(int imgid, int priority, int index, const QString &name) {
#ifdef OMARAW_ENGINE
        if (oma_engine_shape_rename(imgid, "exposure", priority, index, name.toUtf8().constData()) != 0) emit failed(QString::fromUtf8(oma_engine_error()));
        emit localsRead(imgid, readLocals(imgid));
#else
        Q_UNUSED(imgid) Q_UNUSED(priority) Q_UNUSED(index) Q_UNUSED(name)
#endif
    }
    void setBrush(int imgid, int priority, int index, double size, double hardness, double flow) {
#ifdef OMARAW_ENGINE
        if (oma_engine_shape_set_brush(imgid, "exposure", priority, index, float(size), float(hardness), float(flow)) != 0) emit failed(QString::fromUtf8(oma_engine_error()));
        emit localsRead(imgid, readLocals(imgid));
#else
        Q_UNUSED(imgid) Q_UNUSED(priority) Q_UNUSED(index) Q_UNUSED(size) Q_UNUSED(hardness) Q_UNUSED(flow)
#endif
    }
    void renameLocal(int imgid, int priority, const QString &name) {
#ifdef OMARAW_ENGINE
        if (oma_engine_local_rename(imgid, "exposure", priority, name.toUtf8().constData()) != 0) emit failed(QString::fromUtf8(oma_engine_error()));
        emit localsRead(imgid, readLocals(imgid));
#else
        Q_UNUSED(imgid) Q_UNUSED(priority) Q_UNUSED(name)
#endif
    }
    // A saved or copied local row, rebuilt as a new local on `imgid`.
    void pasteLocal(int imgid, const QVariantMap &row) {
#ifdef OMARAW_ENGINE
        const int p = buildLocal(imgid, row);
        if (p > 0) emit localAdded(imgid, p);
        emit localsRead(imgid, readLocals(imgid));
#else
        Q_UNUSED(imgid) Q_UNUSED(row)
#endif
    }
    void setParamString(int imgid, const QString &op, const QString &field, const QString &value) {
#ifdef OMARAW_ENGINE
        if (oma_engine_param_set_string(imgid, op.toLatin1().constData(), field.toLatin1().constData(), value.toUtf8().constData()) != 0)
            emit failed(QString::fromUtf8(oma_engine_error()));
#else
        Q_UNUSED(imgid) Q_UNUSED(op) Q_UNUSED(field) Q_UNUSED(value)
#endif
    }
    void setParam(int imgid, const QString &op, const QString &field, double value) {
#ifdef OMARAW_ENGINE
        if (isWhiteBalance(op, field)) { if (!setWhiteBalance(imgid, field, value)) emit failed(QString::fromUtf8(oma_engine_error())); return; }
        if (isTexture(op, field)) { if (oma_engine_texture_set(imgid, float(value)) != 0) emit failed(QString::fromUtf8(oma_engine_error())); return; }
        if (oma_engine_param_set(imgid, op.toLatin1().constData(), field.toLatin1().constData(), float(value)) != 0)
            emit failed(QString::fromUtf8(oma_engine_error()));
#else
        Q_UNUSED(imgid) Q_UNUSED(op) Q_UNUSED(field) Q_UNUSED(value)
#endif
    }
#ifdef OMARAW_ENGINE
    // One local from a saved row: the first shape with the instance, the
    // rest appended in order, then operators, ranges, switches, values and
    // the name. Returns the new instance's priority, or -1.
    int buildLocal(int imgid, const QVariantMap &m) {
        const QVariantList saved = m.value(QStringLiteral("shapes")).toList();
        const bool pen = !saved.isEmpty() && saved.first().toMap().value("shape").toInt() == OMA_SHAPE_PATH;
        const auto firstPath = pen ? penNodes(imgid, saved.first().toMap().value("points").toList(), false) : QVector<oma_path_node>();
        if (pen && firstPath.isEmpty()) { emit failed(tr("The saved pen mask has invalid points.")); return -1; }
        const int shape = pen ? OMA_SHAPE_PATH : m.value(QStringLiteral("shape")).toInt() == 2 ? 2 : 1;
        const int p = pen ? oma_engine_local_add_path(imgid, "exposure", firstPath.constData(), firstPath.size(), saved.first().toMap().value("border").toFloat()) : shape == 1
            ? oma_engine_local_add(imgid, "exposure", 1, float(m.value(QStringLiteral("cx")).toDouble()), float(m.value(QStringLiteral("cy")).toDouble()),
                                   float(m.value(QStringLiteral("radius")).toDouble()), float(m.value(QStringLiteral("border")).toDouble()), 0.f)
            : oma_engine_local_add(imgid, "exposure", 2, float(m.value(QStringLiteral("cx")).toDouble()), float(m.value(QStringLiteral("cy")).toDouble()),
                                   float(m.value(QStringLiteral("compression")).toDouble()), 0.f, float(m.value(QStringLiteral("rotation")).toDouble()));
        if (p < 0) { emit failed(QString::fromUtf8(oma_engine_error())); return -1; }
        if (pen) {
            const auto sm = saved.first().toMap();
            oma_engine_shape_set(imgid, "exposure", p, 0, firstPath[0].x, firstPath[0].y, sm.value("border").toFloat(), 0, 0, sm.value("opacity", 1.0).toFloat());
        } else oma_engine_local_set_mask(imgid, "exposure", p, float(m.value(QStringLiteral("cx")).toDouble()), float(m.value(QStringLiteral("cy")).toDouble()),
                                  float(m.value(shape == 1 ? QStringLiteral("radius") : QStringLiteral("compression")).toDouble()),
                                  float(m.value(QStringLiteral("border")).toDouble()), float(m.value(QStringLiteral("rotation")).toDouble()),
                                  float(m.value(QStringLiteral("opacity"), 1.0).toDouble()));
        // Shapes beyond the first, in their saved order, then every
        // shape's combine and inversion (shape 0 keeps its union).
        for (int k = 1; k < saved.size(); ++k) {
            const QVariantMap sm = saved[k].toMap();
            const int st = sm.value(QStringLiteral("shape")).toInt();
            int idx = -1;
            if (st == OMA_SHAPE_PATH) {
                const auto path = penNodes(imgid, sm.value("points").toList(), false);
                if (path.isEmpty()) { emit failed(tr("The saved pen mask has invalid points.")); continue; }
                idx = oma_engine_shape_add_path(imgid, "exposure", p, path.constData(), path.size(), sm.value("border").toFloat());
            } else if (st == OMA_SHAPE_BRUSH) {
                QVector<float> pts;
                for (const QVariant &nv : sm.value(QStringLiteral("points")).toList()) {
                    const QVariantMap n = nv.toMap();
                    pts << float(n.value(QStringLiteral("x")).toDouble()) << float(n.value(QStringLiteral("y")).toDouble());
                }
                if (pts.isEmpty()) continue;
                idx = oma_engine_shape_add_brush(imgid, "exposure", p, pts.constData(), pts.size() / 2,
                                                 float(sm.value(QStringLiteral("size")).toDouble()),
                                                 float(sm.value(QStringLiteral("hardness"), 0.6).toDouble()),
                                                 float(sm.value(QStringLiteral("flow"), 1.0).toDouble()));
            } else if (st == OMA_SHAPE_GRADIENT) {
                idx = oma_engine_shape_add(imgid, "exposure", p, st, float(sm.value(QStringLiteral("cx")).toDouble()), float(sm.value(QStringLiteral("cy")).toDouble()),
                                           float(sm.value(QStringLiteral("compression")).toDouble()), 0.f, float(sm.value(QStringLiteral("rotation")).toDouble()));
            } else {
                idx = oma_engine_shape_add(imgid, "exposure", p, OMA_SHAPE_CIRCLE, float(sm.value(QStringLiteral("cx")).toDouble()), float(sm.value(QStringLiteral("cy")).toDouble()),
                                           float(sm.value(QStringLiteral("radius")).toDouble()), float(sm.value(QStringLiteral("border")).toDouble()), 0.f);
            }
            if (idx < 0) { emit failed(QString::fromUtf8(oma_engine_error())); continue; }
            if (st != OMA_SHAPE_BRUSH)
                oma_engine_shape_set(imgid, "exposure", p, idx, float(sm.value(QStringLiteral("cx")).toDouble()), float(sm.value(QStringLiteral("cy")).toDouble()),
                                     float(sm.value(st == OMA_SHAPE_PATH ? QStringLiteral("border") : st == OMA_SHAPE_GRADIENT ? QStringLiteral("compression") : QStringLiteral("radius")).toDouble()),
                                     float(sm.value(QStringLiteral("border")).toDouble()), float(sm.value(QStringLiteral("rotation")).toDouble()),
                                     float(sm.value(QStringLiteral("opacity"), 1.0).toDouble()));
        }
        for (int k = 0; k < saved.size(); ++k) {
            const QVariantMap sm = saved[k].toMap();
            oma_engine_shape_set_combine(imgid, "exposure", p, k, sm.value(QStringLiteral("combine")).toInt(),
                                         sm.value(QStringLiteral("inverse")).toBool() ? 1 : 0);
            if (sm.contains(QStringLiteral("enabled")) && !sm.value(QStringLiteral("enabled")).toBool())
                oma_engine_shape_set_enabled(imgid, "exposure", p, k, 0);
            if (!sm.value(QStringLiteral("name")).toString().isEmpty())
                oma_engine_shape_rename(imgid, "exposure", p, k, sm.value(QStringLiteral("name")).toString().toUtf8().constData());
        }
        // Ranges before any shape drop: a mask with no drawn shape is
        // only legal while a range is holding it.
        for (const QVariant &rv : m.value(QStringLiteral("ranges")).toList()) {
            const QVariantMap r = rv.toMap();
            oma_engine_range_set(imgid, "exposure", p, r.value(QStringLiteral("channel")).toInt(),
                                 r.value(QStringLiteral("active")).toBool() ? 1 : 0,
                                 r.value(QStringLiteral("inverse")).toBool() ? 1 : 0,
                                 float(r.value(QStringLiteral("p0")).toDouble()), float(r.value(QStringLiteral("p1")).toDouble()),
                                 float(r.value(QStringLiteral("p2"), 1.0).toDouble()), float(r.value(QStringLiteral("p3"), 1.0).toDouble()));
        }
        if (m.contains(QStringLiteral("shapes")) && saved.isEmpty()) oma_engine_shape_remove(imgid, "exposure", p, 0);
        if (m.value(QStringLiteral("maskInverted")).toBool()) oma_engine_mask_set_invert(imgid, "exposure", p, 1);
        if (m.value(QStringLiteral("blur")).toDouble() > 0 || m.value(QStringLiteral("feather")).toDouble() > 0
            || m.value(QStringLiteral("maskContrast")).toDouble() != 0 || m.value(QStringLiteral("maskBrightness")).toDouble() != 0) {
            oma_refine_info rf;
            rf.blur = float(m.value(QStringLiteral("blur")).toDouble()); rf.feather = float(m.value(QStringLiteral("feather")).toDouble());
            rf.guide = m.value(QStringLiteral("guide")).toInt();
            rf.contrast = float(m.value(QStringLiteral("maskContrast")).toDouble()); rf.brightness = float(m.value(QStringLiteral("maskBrightness")).toDouble());
            oma_engine_refine_set(imgid, "exposure", p, &rf);
        }
        for (const char *f : {"exposure", "black"})
            if (m.contains(QString::fromLatin1(f))) oma_engine_param_set_instance(imgid, "exposure", p, f, float(m.value(QString::fromLatin1(f)).toDouble()));
        // Stack members only where the saved value is not the neutral one,
        // so a plain local does not come back with seven empty instances.
        int nf = 0; const StackField *sf = stackFields(&nf);
        for (int k = 0; k < nf; ++k) {
            const QString key = QString::fromLatin1(sf[k].key);
            if (m.contains(key) && qAbs(m.value(key).toDouble()) > 1e-6)
                oma_engine_stack_param_set(imgid, "exposure", p, sf[k].op, sf[k].field, float(m.value(key).toDouble()));
        }
        if (m.contains(QStringLiteral("enabled")) && !m.value(QStringLiteral("enabled")).toBool()) oma_engine_local_set_enabled(imgid, "exposure", p, 0);
        if (!m.value(QStringLiteral("name")).toString().isEmpty())
            oma_engine_local_rename(imgid, "exposure", p, m.value(QStringLiteral("name")).toString().toUtf8().constData());
        return p;
    }
#endif
    void setPrimaryGrade(int imgid, const QString &zone, double hue, double chroma,
                          double luminance, bool separateEdit) {
#ifdef OMARAW_ENGINE
        if (separateEdit && oma_engine_begin_edit(imgid)) { emit failed(QString::fromUtf8(oma_engine_error())); return; }
        const char *suffixes[] = { "_H", "_C", "_Y" };
        const double values[] = { hue, chroma, luminance };
        for (int i = 0; i < 3; ++i)
            if (oma_engine_param_set(imgid, "omarawgrade", (zone + suffixes[i]).toLatin1().constData(), float(values[i])))
                emit failed(QString::fromUtf8(oma_engine_error()));
#else
        Q_UNUSED(imgid) Q_UNUSED(zone) Q_UNUSED(hue) Q_UNUSED(chroma) Q_UNUSED(luminance) Q_UNUSED(separateEdit)
#endif
    }
    // A preset's camera profile (a XMP film look's DCP) names the
    // profile, not a file: the one for this photo's camera is found in the
    // person's profiles folder and baked into print tables, then shown the
    // way a print stock is, in place of the tone mapper. Not found, the
    // rest of the preset still applies and the status line says why.
    QVariantList withCameraProfile(int imgid, const QVariantList &given) {
        const auto isProfile = [](const QVariantMap &m) {
            return m.value(QStringLiteral("op")) == QLatin1String("omarawprint") && m.value(QStringLiteral("field")) == QLatin1String("cameraprofile");
        };
        if (std::none_of(given.cbegin(), given.cend(), [&](const QVariant &v) { return isProfile(v.toMap()); })) return given;
        // The converted preset's curves follow the profile, on its display
        // values: they are baked into the profile's tables, and the photo's
        // own curves are left straight so they are not applied twice.
        QVariantList curves;
        for (const QVariant &v : given)
            if (v.toMap().value(QStringLiteral("curveset")).toBool() || v.toMap().value(QStringLiteral("tonecurveset")).toBool()) curves << v;
        QVariantList front, out, back;
        bool baked = false;
        for (const QVariant &v : given) {
            const QVariantMap m = v.toMap();
            if (!isProfile(m)) { out << v; continue; }
            const QString profile = m.value(QStringLiteral("name")).toString();
            QString camera, exif, error;
            bool scene = false;
#ifdef OMARAW_ENGINE
            char normalised[256] = {0}, fromExif[256] = {0};
            oma_engine_camera(imgid, normalised, sizeof normalised, fromExif, sizeof fromExif);
            camera = QString::fromUtf8(normalised).simplified(); exif = QString::fromUtf8(fromExif).simplified();
            scene = oma_engine_is_scene_referred(imgid) > 0;
#endif
            if (!scene) {
                emit notice(tr("Camera profiles require a RAW photo. The current rendering was kept."));
                continue;
            }
            const QString source = m.value(QStringLiteral("source")).toString();
            const QString tables = source.isEmpty() ? CameraProfiles::tablesFor(camera, exif, profile, curves, &error)
                                                    : CameraProfiles::tablesFromFile(source, camera, exif, curves, &error);
            if (tables.isEmpty()) { emit notice(tr("Camera profile \"%1\" not applied: %2").arg(profile, error)); continue; }
            baked = true;
            const auto row = [](const char *field, const QVariant &value) {
                return QVariantMap{{QStringLiteral("op"), QStringLiteral("omarawprint")}, {QStringLiteral("field"), QString::fromLatin1(field)}, {QStringLiteral("value"), value}};
            };
            // The print first, so it is switched on while the tone mapper it
            // replaces is still on and remembers it; then the tone mappers off.
            front << QVariantMap{{QStringLiteral("op"), QStringLiteral("omarawprint")}, {QStringLiteral("field"), QStringLiteral("profile")}, {QStringLiteral("text"), tables}}
                  << row("stock", 22) << row("strength", 100.0) << row("white", 0) << row("paper", 1)
                  << row("trim_r", 0.0) << row("trim_g", 0.0) << row("trim_b", 0.0) << row("gain_r", 1.0) << row("gain_g", 1.0) << row("gain_b", 1.0)
                  << QVariantMap{{QStringLiteral("op"), QStringLiteral("omarawprint")}, {QStringLiteral("enabled"), true}};
            for (const char *tone : {"sigmoid", "filmicrgb", "basecurve"})
                back << QVariantMap{{QStringLiteral("op"), QString::fromLatin1(tone)}, {QStringLiteral("enabled"), false}};
        }
        if (baked && !curves.isEmpty()) {
            const QVariantList straight{0.0, 1.0};
            const QVariantMap line{{QStringLiteral("xs"), straight}, {QStringLiteral("ys"), straight}, {QStringLiteral("type"), 2}};
            QVariantList kept;
            for (const QVariant &v : std::as_const(out)) {
                const QVariantMap m = v.toMap();
                if (m.value(QStringLiteral("curveset")).toBool())
                    kept << QVariantMap{{QStringLiteral("curveset"), true}, {QStringLiteral("enabled"), false}, {QStringLiteral("linked"), true}, {QStringLiteral("channels"), QVariantList{line, line, line}}};
                else if (m.value(QStringLiteral("tonecurveset")).toBool())
                    kept << QVariantMap{{QStringLiteral("tonecurveset"), true}, {QStringLiteral("enabled"), false}, {QStringLiteral("xs"), straight}, {QStringLiteral("ys"), straight}};
                else kept << v;
            }
            out = kept;
        }
        return front + out + back;
    }

    QVariantList presetSnapshot(int imgid) {
        return PresetState::snapshot(curatedValues(imgid, EngineService::curatedParams()), readToolState(imgid),
                                    readLocals(imgid), readSpots(imgid), readCurve(imgid),
                                    readParametric(imgid), readZones(imgid), true);
    }
    void replacePreset(int imgid, const QVariantList &recipe) {
#ifdef OMARAW_ENGINE
        if (recipe.isEmpty()) return;
        // Nested wrappers are not preset parameters and must never recurse.
        for (const auto &v : recipe) if (v.toMap().contains("preset")) {
            emit failed(tr("Invalid nested preset.")); return;
        }
        char *json=nullptr;
        const int found=oma_engine_preset_state_get(imgid, &json);
        if (found<0) { emit failed(tr("Could not read the photo's previous preset.")); return; }
        const auto previous=QJsonDocument::fromJson(json ? QByteArray(json) : QByteArray()).toVariant().toMap();
        oma_engine_free(json);
        const auto equal=[](const QVariant &a, const QVariant &b) {
            return QJsonDocument::fromVariant(a).toJson(QJsonDocument::Compact)==QJsonDocument::fromVariant(b).toJson(QJsonDocument::Compact);
        };
        const auto owned=PresetState::owners(recipe);
        const auto before=presetSnapshot(imgid);
        if (equal(previous.value("recipe"), recipe) && equal(previous.value("applied"), PresetState::select(before, owned))) return;
        const int from=oma_engine_history_end(imgid);
        QString error;
        const auto errors=connect(this, &EngineWorker::failed, this, [&error](const QString &m) { if (error.isEmpty()) error=m; }, Qt::DirectConnection);
        const auto disconnected=qScopeGuard([&] { disconnect(errors); });
        const auto rollback=qScopeGuard([&] {
            if (!error.isEmpty() && oma_engine_history_set_end(imgid, from)) emit failed(QString::fromUtf8(oma_engine_error()));
        });
        const auto previousBaseline=previous.value("baseline").toList();
        // Do not rewrite unchanged tools: setters can enable a disabled tool
        // temporarily and otherwise add redundant history on every switch.
        QSet<QString> changed;
        for (const auto &v : previousBaseline) {
            const auto op=PresetState::owner(v.toMap());
            if (!equal(PresetState::select(previousBaseline,{op}),PresetState::select(before,{op}))) changed.insert(op);
        }
        const auto restore=PresetState::select(previousBaseline,changed);
        if (!restore.isEmpty()) setParams(imgid, restore);
        if (!error.isEmpty()) return;
        const auto restored=presetSnapshot(imgid);
        const auto baseline=PresetState::select(restored, owned);
        // Clicking History can stop inside the restoration/application group.
        // That partial state owns both sides until the complete recipe lands.
        auto transitionOwners=owned;
        transitionOwners.unite(PresetState::owners(previousBaseline));
        const QVariantMap transition{{"version",1},{"baseline",PresetState::select(restored,transitionOwners)}};
        const auto transitionJson=QJsonDocument::fromVariant(transition).toJson(QJsonDocument::Compact);
        const auto isOn=[imgid](const QString &op) {
            const char *field=op=="sigmoid" ? "middle_grey_contrast" : op=="filmicrgb" ? "contrast" : op=="basecurve" ? "exposure_stops" : "stock";
            oma_param_info info={};
            return !oma_engine_param_get(imgid,op.toLatin1().constData(),field,&info) && info.found && info.enabled;
        };
        setParams(imgid, withToneMapperRules(recipe, isOn, nullptr));
        if (!error.isEmpty()) return;
        const QVariantMap state{{"version",1},{"recipe",recipe},{"baseline",baseline},{"applied",PresetState::select(presetSnapshot(imgid),owned)}};
        const auto saved=QJsonDocument::fromVariant(state).toJson(QJsonDocument::Compact);
        if (oma_engine_preset_state_put(imgid,from,saved.constData(),transitionJson.constData())) emit failed(tr("Could not save the photo's preset state."));
#else
        Q_UNUSED(imgid) Q_UNUSED(recipe)
#endif
    }

    void setParams(int imgid, const QVariantList &given) {
        if (given.size()==1 && given.first().toMap().contains("preset")) {
            replacePreset(imgid,given.first().toMap().value("preset").toList()); return;
        }
        QVariantList values;
#ifdef OMARAW_ENGINE
        // Saved camera looks must never silently carry another body's tables.
        oma_param_info requestedStock = {};
        oma_engine_param_get(imgid, "omarawprint", "stock", &requestedStock);
        bool cameraLook = int(requestedStock.value) == 22, printOn = requestedStock.enabled;
        for (const auto &value : given) {
            const auto row = value.toMap();
            if (row.value("op") != "omarawprint") continue;
            if (row.value("field") == "stock") cameraLook = row.value("value").toInt() == 22;
            if (row.contains("enabled")) printOn = row.value("enabled").toBool();
        }
        for (const auto &value : given) {
            const auto row = value.toMap();
            if (!cameraLook || !printOn || row.value("op") != "omarawprint" || row.value("field") != "profile" || row.value("text").toString().isEmpty()) continue;
            const auto details = CameraProfiles::tableDetails(row.value("text").toString());
            if (details.isEmpty()) continue; // Older tables did not have identification sidecars.
            char camera[256] = {}, exif[256] = {};
            oma_engine_camera(imgid, camera, sizeof camera, exif, sizeof exif);
            if (oma_engine_is_scene_referred(imgid) <= 0 || !CameraProfiles::matchesCamera(details.value("camera").toString(), QString::fromUtf8(camera), QString::fromUtf8(exif))) {
                emit failed(tr("These camera-profile settings belong to %1. Choose a profile for this photo's camera instead.").arg(details.value("camera").toString()));
                return;
            }
        }
#endif
        // Camera-linear DNGs need the same starting exposure as mosaic RAWs,
        // even though they have already been demosaiced.
        int raw = -1;
        const auto isRaw = [&] {
#ifdef OMARAW_ENGINE
            if (raw < 0) raw = oma_engine_is_scene_referred(imgid) > 0;
#endif
            return raw > 0;
        };
        for (const QVariant &v : withCameraProfile(imgid, given)) {
            // A XMP preset's exposure is relative to the starting
            // exposure, which is +0.7 on a RAW and 0 on anything else.
            if (v.toMap().value(QStringLiteral("fromStart")).toBool()) {
                QVariantMap row = v.toMap();
                row.remove(QStringLiteral("fromStart"));
                if (isRaw()) row[QStringLiteral("value")] = row.value(QStringLiteral("value")).toDouble() + 0.7;
                values << row;
                continue;
            }
            values << v;
            const QVariantMap row = v.toMap();
            if (row.value("op") == "omarawprint" && row.value("field") == "base_tone" && row.value("enabled").toBool())
                for (const QVariant &tone : row.value("toneParams").toList()) {
                    QVariantMap m = tone.toMap();
                    const QString op = m.value("op").toString();
                    if (op == "sigmoid" || op == "filmicrgb" || op == "basecurve") {
                        m["enabled"] = false; values << m;
                    }
                }
        }
#ifdef OMARAW_ENGINE
        if (oma_engine_begin_edit(imgid)) { emit failed(QString::fromUtf8(oma_engine_error())); return; }
        for(const auto &value:values) {
            const auto row=value.toMap();
            if(row.value("liquifyset").toBool() && !restoreWarps(imgid,row.value("warps").toList(),row.value("enabled").toBool())) emit failed(tr("Invalid Liquify settings."));
            if(row.value("rasterset").toBool()) {
                const auto targets=row.value("targets").toList();
                const QString path=targets.isEmpty()?QString():retainManagedMask(row.value("path").toString());
                if(!targets.isEmpty() && !QFileInfo::exists(path)) {emit failed(tr("The preset's external mask file is unavailable."));continue;}
                for(const auto &op:rasterTargets()) {
                    QVariantMap target;
                    for(const auto &t:targets)if(t.toMap().value("op")==op)target=t.toMap();
                    if(!target.isEmpty() || oma_engine_raster_get(imgid,op.toLatin1().constData(),nullptr,nullptr)==1)
                        if(oma_engine_raster_set(imgid,target.isEmpty()?"":path.toUtf8().constData(),op.toLatin1().constData(),target.value("inverse").toBool(),target.value("opacity",1).toFloat())) emit failed(QString::fromUtf8(oma_engine_error()));
                }
            }
        }
        // Apply each RGB levels triple atomically so preset order cannot create
        // a zero-width interval or clamp away a valid black/white point.
        for(int ch=0;ch<3;++ch) {
            float next[3]; bool changed=false;
            for(int i=0;i<3;++i) {
                const QString field=QString("levels[%1][%2]").arg(ch).arg(i); oma_param_info info;
                oma_engine_param_get(imgid,"rgblevels",field.toLatin1().constData(),&info);next[i]=info.value;
                for(const auto &v:values){const auto p=v.toMap();if(p.value("op")=="rgblevels"&&p.value("field")==field){next[i]=p.value("value").toFloat();changed=true;}}
            }
            if(changed && oma_engine_levels_set(imgid,ch,next[0],next[1],next[2])) emit failed(tr("RGB levels require black < midpoint < white."));
        }
        // A spotset row replaces the retouch spots wholesale, like the locals.
        for (const QVariant &v : values) {
            const QVariantMap m = v.toMap();
            if (!m.value(QStringLiteral("spotset")).toBool()) continue;
            while (oma_engine_spot_count(imgid) > 0) if (oma_engine_spot_remove(imgid, 0) != 0) break;
            for (const QVariant &sv : m.value(QStringLiteral("spots")).toList()) {
                const QVariantMap sp = sv.toMap();
                const int shape=sp.value("shape",1).toInt(); QVector<float> xy;
                if(shape==1) xy<<sp.value("cx").toFloat()<<sp.value("cy").toFloat();
                else for(const auto &p:sp.value("points").toList()) xy<<p.toMap().value("x").toFloat()<<p.toMap().value("y").toFloat();
                const int i=oma_engine_spot_add_drawn(imgid,sp.value("algorithm",2).toInt(),shape,xy.constData(),xy.size()/2,
                    sp.value("radius",.03).toFloat(),sp.value("border",.01).toFloat(),sp.value("sx").toFloat(),sp.value("sy").toFloat(),sp.value("scale",0).toInt());
                if (i < 0) { emit failed(QString::fromUtf8(oma_engine_error())); continue; }
                oma_engine_spot_set_algorithm(imgid, i, sp.value(QStringLiteral("algorithm"), 2).toInt(),
                                              float(sp.value(QStringLiteral("blurRadius")).toDouble()), float(sp.value(QStringLiteral("fillBrightness")).toDouble()));
                if (sp.contains(QStringLiteral("opacity")))
                    oma_engine_spot_set(imgid, i, float(sp.value(QStringLiteral("cx")).toDouble()), float(sp.value(QStringLiteral("cy")).toDouble()),
                                        float(sp.value(QStringLiteral("radius")).toDouble()), float(sp.value(QStringLiteral("border")).toDouble()),
                                        float(sp.value(QStringLiteral("sx")).toDouble()), float(sp.value(QStringLiteral("sy")).toDouble()),
                                        float(sp.value(QStringLiteral("opacity"), 1.0).toDouble()));
            }
            emit spotsRead(imgid, readSpots(imgid));
        }
        // A curveset row replaces the tone curve wholesale.
        for (const QVariant &v : values) {
            const QVariantMap m = v.toMap();
            if (!m.value(QStringLiteral("curveset")).toBool()) continue;
            const QVariantList channels = m.value(QStringLiteral("channels")).toList();
            oma_engine_curve_set_linked(imgid, m.value(QStringLiteral("linked"), true).toBool() ? 1 : 0);
            for (int ch = 0; ch < channels.size() && ch < 3; ++ch) {
                const QVariantMap c = channels[ch].toMap();
                const QVariantList xs = c.value(QStringLiteral("xs")).toList(), ys = c.value(QStringLiteral("ys")).toList();
                float x[OMA_CURVE_MAX], y[OMA_CURVE_MAX];
                const int n = qMin(int(qMin(xs.size(), ys.size())), int(OMA_CURVE_MAX));
                if (n < 2) continue;
                for (int i = 0; i < n; ++i) { x[i] = float(xs[i].toDouble()); y[i] = float(ys[i].toDouble()); }
                if (oma_engine_curve_set(imgid, ch, x, y, n, c.value(QStringLiteral("type"), 2).toInt()) != 0) emit failed(QString::fromUtf8(oma_engine_error()));
            }
            if (m.contains(QStringLiteral("enabled")) && !m.value(QStringLiteral("enabled")).toBool()) oma_engine_module_set_enabled(imgid, "rgbcurve", 0);
            emit curveRead(imgid, readCurve(imgid));
        }
        // A tonecurveset row replaces the parametric curve wholesale.
        for (const QVariant &v : values) {
            const QVariantMap m = v.toMap();
            if (!m.value(QStringLiteral("tonecurveset")).toBool()) continue;
            const QVariantList xs = m.value(QStringLiteral("xs")).toList(), ys = m.value(QStringLiteral("ys")).toList();
            float x[OMA_CURVE_MAX], y[OMA_CURVE_MAX];
            const int n = qMin(int(qMin(xs.size(), ys.size())), int(OMA_CURVE_MAX));
            if (n >= 2) {
                for (int i = 0; i < n; ++i) { x[i] = float(xs[i].toDouble()); y[i] = float(ys[i].toDouble()); }
                if (oma_engine_lcurve_set(imgid, x, y, n) != 0) emit failed(QString::fromUtf8(oma_engine_error()));
            }
            // Rows saved before the colour curves existed leave them alone.
            const QVariantList ab = m.value(QStringLiteral("ab")).toList();
            for (int ch = 1; ch <= 2 && ab.size() == 2; ++ch) {
                const QVariantMap c = ab[ch - 1].toMap();
                const QVariantList lx = c.value(QStringLiteral("xs")).toList(), ly = c.value(QStringLiteral("ys")).toList();
                const int ln = qMin(int(qMin(lx.size(), ly.size())), int(OMA_CURVE_MAX));
                if (ln < 2) continue;
                for (int i = 0; i < ln; ++i) { x[i] = float(lx[i].toDouble()); y[i] = float(ly[i].toDouble()); }
                if (oma_engine_labcurve_set(imgid, ch, x, y, ln, c.value(QStringLiteral("type"), 2).toInt()) != 0) emit failed(QString::fromUtf8(oma_engine_error()));
            }
            if (m.contains(QStringLiteral("enabled")) && !m.value(QStringLiteral("enabled")).toBool()) oma_engine_module_set_enabled(imgid, "tonecurve", 0);
            emit parametricRead(imgid, readParametric(imgid));
        }
        // A zoneset row replaces the colour zones wholesale.
        for (const QVariant &v : values) {
            const QVariantMap m = v.toMap();
            if (!m.value(QStringLiteral("zoneset")).toBool()) continue;
            const QVariantList channels = m.value(QStringLiteral("channels")).toList();
            for (int ch = 0; ch < channels.size() && ch < 3; ++ch) {
                const QVariantMap c = channels[ch].toMap();
                const QVariantList xs = c.value(QStringLiteral("xs")).toList(), ys = c.value(QStringLiteral("ys")).toList();
                float x[OMA_CURVE_MAX], y[OMA_CURVE_MAX];
                const int n = qMin(int(qMin(xs.size(), ys.size())), int(OMA_CURVE_MAX));
                if (n < 2) continue;
                for (int i = 0; i < n; ++i) { x[i] = float(xs[i].toDouble()); y[i] = float(ys[i].toDouble()); }
                if (oma_engine_zones_set(imgid, ch, x, y, n) != 0) emit failed(QString::fromUtf8(oma_engine_error()));
            }
            if (m.contains(QStringLiteral("enabled")) && !m.value(QStringLiteral("enabled")).toBool()) oma_engine_module_set_enabled(imgid, "colorzones", 0);
            emit zonesRead(imgid, readZones(imgid));
        }
        // Local rows replace the locals wholesale: the saved state is the whole set.
        bool hasLocals = false;
        for (const QVariant &v : values)
            if (v.toMap().value(QStringLiteral("local")).toBool() || v.toMap().value(QStringLiteral("localset")).toBool()) hasLocals = true;
        if (hasLocals) {
            oma_local_info li;
            while (oma_engine_local_count(imgid, "exposure") > 0 && oma_engine_local_get(imgid, "exposure", 0, &li) == 0 && li.found)
                if (oma_engine_local_remove(imgid, "exposure", li.priority) != 0) break;
            for (const QVariant &v : values) {
                const QVariantMap m = v.toMap();
                if (!m.value(QStringLiteral("local")).toBool()) continue;
                buildLocal(imgid, m);
            }
            emit localsRead(imgid, readLocals(imgid));
        }
        // In the order the rows name them (the last state of each wins), so
        // the history reads as asked: a print switched on before the tone
        // mapper it parks, not alphabetically.
        QStringList enabledOrder;
        QHash<QString, bool> enabledStates;
        for (const QVariant &v : values) {
            const QVariantMap m = v.toMap();
            if (m.value(QStringLiteral("local")).toBool() || m.value(QStringLiteral("localset")).toBool() || m.value(QStringLiteral("spotset")).toBool() || m.value(QStringLiteral("curveset")).toBool() || m.value(QStringLiteral("zoneset")).toBool() || m.value(QStringLiteral("tonecurveset")).toBool()) continue;
            const QString op = m.value(QStringLiteral("op")).toString(), field = m.value(QStringLiteral("field")).toString();
            if (op.isEmpty()) continue;
            if (m.contains(QStringLiteral("enabled"))) {
                if (!enabledStates.contains(op)) enabledOrder << op;
                enabledStates[op] = m.value(QStringLiteral("enabled")).toBool();
            }
            if(op=="rgblevels" && field.startsWith("levels[")) continue;
            // A file row (a LUT's, a camera profile's tables) carries its path.
            if (m.contains(QStringLiteral("text"))) {
                if (oma_engine_param_set_string(imgid, op.toLatin1().constData(), field.toLatin1().constData(), m.value(QStringLiteral("text")).toString().toUtf8().constData()) != 0)
                    emit failed(QString::fromUtf8(oma_engine_error()));
                continue;
            }
            // Old snapshots did not carry the LUT's path. Leave those alone;
            // new snapshots preserve an explicitly empty path too.
            if (op == QLatin1String("lut3d") && field == QLatin1String("filepath")) continue;
            if (!m.contains(QStringLiteral("value"))) continue;
            if (isWhiteBalance(op, field)) {
                if (!setWhiteBalance(imgid, field, m.value(QStringLiteral("value")).toDouble())) emit failed(QString::fromUtf8(oma_engine_error()));
                continue;
            }
            if (isTexture(op, field)) {
                if (oma_engine_texture_set(imgid, float(m.value(QStringLiteral("value")).toDouble())) != 0) emit failed(QString::fromUtf8(oma_engine_error()));
                continue;
            }
            if (oma_engine_param_set(imgid, op.toLatin1().constData(), field.toLatin1().constData(),
                                     float(m.value(QStringLiteral("value")).toDouble())) != 0)
                emit failed(QString::fromUtf8(oma_engine_error()));
        }
        // Disabled modules retain their saved values, ready for a later
        // toggle. Apply each final state after all of its parameters.
        for (const QString &op : std::as_const(enabledOrder))
            if (oma_engine_module_set_enabled(imgid, op.toLatin1().constData(), enabledStates.value(op) ? 1 : 0) != 0)
                emit failed(QString::fromUtf8(oma_engine_error()));
        // Capture sharpening switched on with the automatic radius (0): measure
        // it once over the whole frame and keep it, so the fitted preview,
        // the 100% tiles and the export all sharpen with the same radius.
        bool captureOn = false, captureAuto = false;
        for (const QVariant &v : values) {
            const QVariantMap m = v.toMap();
            if (m.value(QStringLiteral("op")) != QLatin1String("demosaic")) continue;
            if (m.value(QStringLiteral("field")) == QLatin1String("cs_enabled")) captureOn = m.value(QStringLiteral("value")).toDouble() > 0.5;
            if (m.value(QStringLiteral("field")) == QLatin1String("cs_radius")) captureAuto = m.value(QStringLiteral("value")).toDouble() <= 0;
        }
        if (captureOn && captureAuto) {
            float radius = 0.f;
            if (oma_engine_measure_capture_radius(imgid, &radius) == 0 && radius > 0.f)
                oma_engine_param_set(imgid, "demosaic", "cs_radius", radius);
        }
#else
        Q_UNUSED(imgid) Q_UNUSED(values)
#endif
    }
    void resetModule(int imgid, const QString &op, const QStringList &fields) {
#ifdef OMARAW_ENGINE
        QList<QByteArray> names; for (const auto &field : fields) names << field.toLatin1();
        QVector<const char *> pointers; for (const auto &name : names) pointers << name.constData();
        if (oma_engine_module_reset(imgid, op.toLatin1().constData(), pointers.constData(), pointers.size()))
            emit failed(QString::fromUtf8(oma_engine_error()));
#else
        Q_UNUSED(imgid) Q_UNUSED(op) Q_UNUSED(fields)
#endif
    }
    void setEnabled(int imgid, const QString &op, bool on) {
#ifdef OMARAW_ENGINE
        if (oma_engine_module_set_enabled(imgid, op.toLatin1().constData(), on ? 1 : 0) != 0)
            emit failed(QString::fromUtf8(oma_engine_error()));
#else
        Q_UNUSED(imgid) Q_UNUSED(op) Q_UNUSED(on)
#endif
    }
    void readCameraProfiles(int imgid) {
        QVariantMap state;
#ifdef OMARAW_ENGINE
        char camera[256] = {}, exif[256] = {}, tables[1024] = {};
        oma_engine_camera(imgid, camera, sizeof camera, exif, sizeof exif);
        const QString body = QString::fromUtf8(camera).simplified(), original = QString::fromUtf8(exif).simplified();
        const bool raw = oma_engine_is_scene_referred(imgid) > 0;
        state = {{"camera", body.isEmpty() ? original : body}, {"exifCamera", original}, {"raw", raw}, {"folder", CameraProfiles::folder()}};
        state["profiles"] = raw ? CameraProfiles::available(body, original) : QVariantList();
        if (raw) {
            const QString key = body + '\n' + original;
            if (!cameraStyleCache.contains(key)) cameraStyleCache.insert(key, CameraStyles::forCamera(imgid, cameraStylePrefix, body, original));
            auto style = cameraStyleCache.value(key);
            bool applied = !style.isEmpty();
            for (const auto &v : style.value("values").toList()) {
                const auto row = v.toMap();
                const QByteArray op = row.value("op").toString().toLatin1();
                if (!row.contains("field")) {
                    if (bool(oma_engine_module_enabled(imgid, op.constData())) != row.value("enabled").toBool()) applied = false;
                    continue;
                }
                oma_param_info info{};
                if (oma_engine_param_get(imgid, op.constData(), row.value("field").toString().toLatin1().constData(), &info)
                    || !info.found || std::abs(info.value - row.value("value").toDouble()) > 1e-5
                    || bool(info.enabled) != row.value("enabled").toBool()) applied = false;
            }
            style["applied"] = applied;
            state["cameraLook"] = style;
        }
        oma_param_info stock = {};
        oma_engine_param_get(imgid, "omarawprint", "stock", &stock);
        oma_engine_param_get_string(imgid, "omarawprint", "profile", tables, sizeof tables);
        const QString path = QString::fromUtf8(tables);
        state["active"] = stock.found && stock.enabled && int(stock.value) == 22;
        state["filmActive"] = stock.found && stock.enabled && int(stock.value) != 22;
        state["tables"] = path;
        state["details"] = path.isEmpty() ? QVariantMap() : CameraProfiles::tableDetails(path);
        state["missing"] = state.value("active").toBool() && !QFileInfo::exists(path);
#else
        Q_UNUSED(imgid)
#endif
        emit cameraProfilesRead(imgid, state);
    }
    QVariantMap readLens(int imgid) {
        QVariantMap out;
#ifdef OMARAW_ENGINE
        oma_lens_info li;
        if (oma_engine_lens_info(imgid, &li) != 0 || !li.found) return out;
        out[QStringLiteral("found")] = true;
        out[QStringLiteral("cameraFound")] = li.camera_found != 0; out[QStringLiteral("lensFound")] = li.lens_found != 0;
        out[QStringLiteral("overridden")] = li.overridden != 0;
        out[QStringLiteral("camera")] = QString::fromUtf8(li.camera); out[QStringLiteral("lens")] = QString::fromUtf8(li.lens);
        out[QStringLiteral("exifCamera")] = QString::fromUtf8(li.exif_camera).trimmed(); out[QStringLiteral("exifLens")] = QString::fromUtf8(li.exif_lens).trimmed();
        char body[256] = {0};
        if (oma_engine_camera(imgid, body, sizeof body, nullptr, 0) == 0) out[QStringLiteral("body")] = QString::fromUtf8(body).simplified();
#else
        Q_UNUSED(imgid)
#endif
        return out;
    }
    void lensCandidates(int imgid) {
        QStringList names;
#ifdef OMARAW_ENGINE
        const int n = oma_engine_lens_candidate_count(imgid);
        if (n < 0) emit failed(QString::fromUtf8(oma_engine_error()));
        char name[256];
        for (int i = 0; i < n; ++i) if (oma_engine_lens_candidate(imgid, i, name, sizeof name) == 0) names << QString::fromUtf8(name);
#endif
        emit lensCandidatesRead(imgid, names);
    }
    void setLens(int imgid, const QString &lens) {
#ifdef OMARAW_ENGINE
        if (oma_engine_lens_override(imgid, lens.toUtf8().constData()) != 0) emit failed(QString::fromUtf8(oma_engine_error()));
        emit lensRead(imgid, readLens(imgid));
#else
        Q_UNUSED(imgid) Q_UNUSED(lens)
#endif
    }
    // Lay saved-state rows on a photo that is not the open one (sync / paste
    // onto a selection), then render it small so its thumbnail follows. The
    // reads setParams emits carry that photo's engine id, which the service
    // ignores for anything but the open image.
    void applyTo(const QString &path, int variant, const QVariantList &values, int edge) {
#ifdef OMARAW_ENGINE
        const int id = oma_engine_open_version(path.toLocal8Bit().constData(), variant);
        if (id < 0) { emit applied(path, variant, false, QString::fromUtf8(oma_engine_error()), QImage(), 0); return; }
        const auto isOn = [id](const QString &op) {
            const char *probe = op == QLatin1String("sigmoid") ? "middle_grey_contrast" : op == QLatin1String("filmicrgb") ? "contrast"
                              : op == QLatin1String("basecurve") ? "exposure_stops" : "stock";
            oma_param_info i; return oma_engine_param_get(id, op.toLatin1().constData(), probe, &i) == 0 && i.found && i.enabled;
        };
        QString error;   // a value the engine refused is a failure, not "applied"
        const auto errors = connect(this, &EngineWorker::failed, this, [&error](const QString &m) { if (error.isEmpty()) error = m; }, Qt::DirectConnection);
        setParams(id, withToneMapperRules(values, isOn, nullptr));
        disconnect(errors);
        float *buf = nullptr; int rw = 0, rh = 0;
        QImage render;
        if (oma_engine_render(id, edge, edge, &buf, &rw, &rh) == 0) {
            QImage img = takeLinear(buf, rw, rh);
            render = ColourPipeline::srgb8(img);
        }
        emit applied(path, variant, error.isEmpty(), error, render, editFlags(id));
#else
        Q_UNUSED(values) Q_UNUSED(edge)
        emit applied(path, variant, false, QStringLiteral("no engine"), QImage(), 0);
#endif
    }
    // A photo's edit for its sidecar: the engine's own history (exact) and
    // the CRS settings it roughly amounts to. Nothing for a photo the
    // engine has never edited: its sidecar keeps whatever it holds.
    void sidecarEdit(const QString &path, bool raw, const QVariantList &curated) {
#ifdef OMARAW_ENGINE
        const int id = oma_engine_known_version(path.toLocal8Bit().constData(), 0);
        if (id < 0 || oma_engine_history_count(id) <= 0) { emit sidecarEditRead(path, false, QByteArray(), QVariantMap(), QString()); return; }
        char *xmp = oma_engine_edit_xmp(id);
        if (!xmp) { emit sidecarEditRead(path, false, QByteArray(), QVariantMap(), QString::fromUtf8(oma_engine_error())); return; }
        const QByteArray packet(xmp);
        oma_engine_free(xmp);
        emit sidecarEditRead(path, true, packet, XmpPreset::cameraRawFrom(curatedValues(id, curated), readCurve(id), raw), QString());
#else
        Q_UNUSED(raw) Q_UNUSED(curated)
        emit sidecarEditRead(path, false, QByteArray(), QVariantMap(), QStringLiteral("no engine"));
#endif
    }
    // A sidecar's exact edit (OmaRAW's, or darktable's) replaces the photo's.
    void applySidecarEdit(const QString &path, int variant, const QString &sidecar, int edge) {
#ifdef OMARAW_ENGINE
        const int id = oma_engine_open_version(path.toLocal8Bit().constData(), variant);
        if (id < 0) { emit applied(path, variant, false, QString::fromUtf8(oma_engine_error()), QImage(), 0); return; }
        if (oma_engine_apply_edit_xmp(id, sidecar.toLocal8Bit().constData()) != 0) {
            emit applied(path, variant, false, QString::fromUtf8(oma_engine_error()), QImage(), 0); return;
        }
        emit exactHistoryReplaced(path, variant);
        float *buf = nullptr; int rw = 0, rh = 0;
        QImage render;
        if (oma_engine_render(id, edge, edge, &buf, &rw, &rh) == 0) render = ColourPipeline::srgb8(takeLinear(buf, rw, rh));
        emit applied(path, variant, true, QString(), render, editFlags(id));
#else
        Q_UNUSED(sidecar) Q_UNUSED(edge)
        emit applied(path, variant, false, QStringLiteral("no engine"), QImage(), 0);
#endif
    }
    // Move one setting of a photo that is not the open one by `delta`,
    // clamped to [lo, hi] (and to the field's own range), then render it
    // small so its thumbnail follows. The Library's quick adjust.
    // `how` (a figureDisplay kind) makes `delta` a step in the control's
    // figures, as Develop shows them, rather than in engine units.
    void nudge(const QString &path, int variant, const QString &op, const QString &field, double delta, double lo, double hi, int edge, const QString &how) {
#ifdef OMARAW_ENGINE
        const int id = oma_engine_open_version(path.toLocal8Bit().constData(), variant);
        if (id < 0) { emit applied(path, variant, false, QString::fromUtf8(oma_engine_error()), QImage(), 0); return; }
        double cur = 0, def = 0;
        bool figures = false;
        if (isWhiteBalance(op, field)) {
            float t = 5003.f, tint = 0.f;
            oma_engine_wb_get(id, &t, &tint);
            cur = field == QLatin1String("temperature") ? t : tint;
        } else if (isTexture(op, field)) {
            float a = 0; int on = 0;
            oma_engine_texture_get(id, &a, &on);
            cur = on ? a : 0;
        } else {
            oma_param_info info;
            if (oma_engine_param_get(id, op.toLatin1().constData(), field.toLatin1().constData(), &info) != 0 || !info.found) {
                emit applied(path, variant, false, QString::fromUtf8(oma_engine_error()), QImage(), 0);
                return;
            }
            cur = info.enabled ? info.value : offStart(op, field, info.def); def = controlReset(op, field, info.def);
            if (!info.enabled) if (const QString other = offSibling(op, field); !other.isEmpty()) setParam(id, op, other, 0);
            if (info.max > info.min) { lo = qMax(lo, double(info.min)); hi = qMin(hi, double(info.max)); }
            figures = !how.isEmpty() && info.max > info.min;
            if (figures) { lo = info.min; hi = info.max; }
        }
        const double next = figures ? fromFigure(how, toFigure(how, cur, lo, def, hi) + delta, lo, def, hi) : qBound(lo, cur + delta, hi);
        QString error;
        const auto errors = connect(this, &EngineWorker::failed, this, [&error](const QString &m) { if (error.isEmpty()) error = m; }, Qt::DirectConnection);
        if (op == "bloom" && field == "strength" && next <= 0) setEnabled(id, op, false);
        else setParam(id, op, field, next);
        disconnect(errors);
        // Setting a value switches its module on. For a tone mapper that must
        // follow the same rules as Develop: parked under a print stock, and
        // its rival (Tone vs Film tone) taken off, never both at once.
        const auto on = [id](const char *module, const char *probe) {
            oma_param_info i; return oma_engine_param_get(id, module, probe, &i) == 0 && i.found && i.enabled;
        };
        if (op == QLatin1String("sigmoid") || op == QLatin1String("filmicrgb") || op == QLatin1String("basecurve")) {
            if (on("omarawprint", "stock")) setEnabled(id, op, false);
            else if (op == QLatin1String("sigmoid") && on("filmicrgb", "contrast")) setEnabled(id, QStringLiteral("filmicrgb"), false);
            else if (op == QLatin1String("filmicrgb") && on("sigmoid", "middle_grey_contrast")) setEnabled(id, QStringLiteral("sigmoid"), false);
        }
        float *buf = nullptr; int rw = 0, rh = 0;
        QImage render;
        if (oma_engine_render(id, edge, edge, &buf, &rw, &rh) == 0) {
            QImage img = takeLinear(buf, rw, rh);
            render = ColourPipeline::srgb8(img);
        }
        emit applied(path, variant, error.isEmpty(), error, render, editFlags(id));
#else
        Q_UNUSED(op) Q_UNUSED(field) Q_UNUSED(delta) Q_UNUSED(lo) Q_UNUSED(hi) Q_UNUSED(edge) Q_UNUSED(how)
        emit applied(path, variant, false, QStringLiteral("no engine"), QImage(), 0);
#endif
    }
    // Undo groups: the history end where a group of edits starts and ends,
    // read in queue order, however many items the edits between them add.
    void markUndo(int imgid, bool start) {
#ifdef OMARAW_ENGINE
        emit undoMarked(imgid, start, oma_engine_history_end(imgid));
#else
        emit undoMarked(imgid, start, 0);   // always answered: the service counts marks back in
#endif
    }
    void beginEdit(int imgid) {
#ifdef OMARAW_ENGINE
        oma_engine_begin_edit(imgid);
#else
        Q_UNUSED(imgid)
#endif
    }
    void resetHistory(int imgid, bool start) {
#ifdef OMARAW_ENGINE
        if (oma_engine_history_reset(imgid, start ? 1 : 0) < 0) emit failed(QString::fromUtf8(oma_engine_error()));
        else emit historyReset(imgid);
#else
        Q_UNUSED(imgid) Q_UNUSED(start)
#endif
    }
    void truncateHistory(int imgid) {
#ifdef OMARAW_ENGINE
        if (oma_engine_history_truncate(imgid) != 0) emit failed(QString::fromUtf8(oma_engine_error()));
#else
        Q_UNUSED(imgid)
#endif
    }
    void setHistoryEnd(int imgid, int end) {
#ifdef OMARAW_ENGINE
        if (oma_engine_history_set_end(imgid, end) != 0) { emit jumpFailed(imgid); emit failed(QString::fromUtf8(oma_engine_error())); }
#else
        Q_UNUSED(imgid) Q_UNUSED(end)
#endif
    }
    void exportFile(const QString &src, int variant, const QString &stem, const QString &format, int maxW, int maxH, int quality, int bpp, int meta,
                    const QVariantMap &finishing, const QVariantMap &describe, const QVariantMap &colour) {
#ifdef OMARAW_ENGINE
        applyExportResolution();
        const int id = oma_engine_open_version(src.toLocal8Bit().constData(), variant);
        if (id < 0) { emit fileExported(src, stem, false, QString::fromUtf8(oma_engine_error())); return; }
        char out[4096] = {0};
        const QByteArray icc = colour.value(QStringLiteral("icc")).toByteArray();
        const QByteArray description = QJsonDocument(QJsonObject::fromVariantMap(describe)).toJson(QJsonDocument::Compact);
        // Finishing works on the export's float pixels inside the engine,
        // before the writer quantises them: one encode, metadata untouched.
        struct FinishingJob { Finishing::Options options; QColorSpace output; } job;
        job.options = Finishing::Options::fromMap(finishing);
        {
            const int profile = colour.value(QStringLiteral("profile")).toInt();
            job.output = profile == 1 ? QColorSpace(QColorSpace::AdobeRgb)
                       : profile == 2 ? QColorSpace(QColorSpace::Primaries::ProPhotoRgb, QColorSpace::TransferFunction::Linear)
                       : profile == 3 ? QColorSpace::fromIccProfile(icc) : QColorSpace(QColorSpace::SRgb);
        }
        if (job.options.active())
            oma_engine_set_export_hook([](float *rgba, int w, int h, int, void *data) {
                const auto *j = static_cast<const FinishingJob *>(data);
                Finishing::applyFloat(rgba, w, h, j->options, j->output);
            }, &job);
        // A PSD is the engine's own 16-bit TIFF export, written under a spare
        // name and converted by the app; finishing and the profile ride along.
        const bool psd = format == QLatin1String("psd");
        const QString engineStem = psd ? stem + QStringLiteral("-psd-tmp") : stem;   // the engine says the name it really wrote
        const QString engineFormat = psd ? QStringLiteral("tiff") : format;
        int rc = oma_engine_export_described(id, engineStem.toLocal8Bit().constData(), engineFormat.toLatin1().constData(),
                                        maxW, maxH, quality, psd ? 16 : bpp, meta, colour.value(QStringLiteral("profile")).toInt(),
                                        icc.constData(), icc.size(), colour.value(QStringLiteral("intent")).toInt(), description.constData(), out, sizeof out);
        oma_engine_set_export_hook(nullptr, nullptr);
        QString note, psdError;
        if (rc == 0 && psd) {
            const QString tiff = QString::fromLocal8Bit(out);
            QString target = stem + QStringLiteral(".psd");
            for (int n = 1; QFileInfo::exists(target) && n < 1000; ++n) target = stem + QStringLiteral("_%1.psd").arg(n, 2, 10, QLatin1Char('0'));
            const QString err = Psd::fromTiff(tiff, target, bpp, Metadata::describeFromMap(describe).ppi);
            if (err.isEmpty()) qstrncpy(out, target.toLocal8Bit().constData(), sizeof out);
            else { QFile::remove(tiff); rc = -1; psdError = err; }
        }
        if (rc == 0) {
            // Our name on our output; darktable's history namespace only when asked for.
            const bool history = meta < 0 || (meta & OMA_META_HISTORY);
            if (format != "avif" && format != "jpegxl"
                && !Metadata::stampExport(QString::fromLocal8Bit(out), QStringLiteral("OmaRAW ") + QStringLiteral(OMARAW_VERSION), !history, Metadata::describeFromMap(describe), meta))
                note += (note.isEmpty() ? QString() : QStringLiteral("; ")) + tr("Image exported, but metadata could not be written");
        }
        emit fileExported(src, QString::fromLocal8Bit(out), rc == 0, rc ? (psdError.isEmpty() ? QString::fromUtf8(oma_engine_error()) : psdError) : note);
#else
        Q_UNUSED(variant) Q_UNUSED(format) Q_UNUSED(maxW) Q_UNUSED(maxH) Q_UNUSED(quality) Q_UNUSED(bpp) Q_UNUSED(meta) Q_UNUSED(finishing) Q_UNUSED(describe) Q_UNUSED(colour)
        emit fileExported(src, stem, false, QStringLiteral("no engine"));
#endif
    }
    void probeFormats() {
        QStringList have;
#ifdef OMARAW_ENGINE
        for (const char *f : {"jpeg", "tiff", "png", "webp", "avif", "jpegxl"})
            if (oma_engine_format_available(f)) have << QString::fromLatin1(f) + QLatin1Char(':') + QString::fromUtf8(oma_engine_format_extension(f));
        // PSD is written by the app from the engine's 16-bit TIFF export.
        if (oma_engine_format_available("tiff")) have << QStringLiteral("psd:psd");
#endif
        emit formats(have);
    }
    void exportJpeg(int imgid, const QString &path, int maxEdge, int quality) {
#ifdef OMARAW_ENGINE
        applyExportResolution();
        // A quick look at the open photo (the CLI's proofs, the self-test):
        // asked for by its path, so it replaces that file rather than
        // leaving "_01" beside it, and it carries no location or edit history.
        char out[4096] = {0};
        oma_engine_set_export_overwrite(1);
        const int rc = oma_engine_export_meta(imgid, path.toLocal8Bit().constData(), "jpeg", maxEdge, maxEdge, quality, 8,
                                              EngineService::metaFlagsFor(true, false, true, false), out, sizeof out);
        oma_engine_set_export_overwrite(0);
        emit exportDone(rc == 0 ? QString::fromLocal8Bit(out) : path, rc == 0, rc ? QString::fromUtf8(oma_engine_error()) : QString());
#else
        Q_UNUSED(imgid) Q_UNUSED(maxEdge) Q_UNUSED(quality)
        emit exportDone(path, false, QStringLiteral("no engine"));
#endif
    }
signals:
    void exposureMeterRead(int imgid, int gen, int serial, bool valid, double exposure, const QString &error);
    void imageMatchReady(int ticket,const QString &action,const QVariantMap &result,const QString &error);
    void cameraProfilesRead(int imgid, const QVariantMap &state);
    void aiSourceReady(int ticket, const QString &path, const QString &error);
    void aiRefinementSourceReady(int ticket, const QString &path, const QString &error);
    void aiRepairSourceReady(int ticket, const QString &path, const QString &error);
    void aiRepairReady(int ticket, const QString &key, const QImage &preview, const QString &error);
    void aiMaskAccepted(int imgid, bool ok, const QString &error);
    void aiCopyPrepared(int ticket, const QString &error);
    void denoiseCopyPrepared(const QString &path, const QString &error);
    void denoisePreviewReady(int ticket, const QImage &before, const QImage &after, const QString &error);
    void undoMarked(int imgid, bool start, int end);
    void retouchPreviewRead(int imgid,int gen,int serial,const QImage &image,const QString &error);
    void toolStateRead(int imgid, const QVariantMap &state);
    void basecurveRead(int imgid, bool enabled);
    void historyRewritten(int imgid, int end);
    void historyReset(int imgid);
    void sidecarEditRead(const QString &path, bool has, const QByteArray &engineXmp, const QVariantMap &cameraRaw, const QString &error);
    void jumpFailed(int imgid);
    void jumpRead(int imgid, int target);
    void rawClippingRead(int imgid, int gen, int serial, const QImage &image, const QString &status);
    void geometryFinished(int imgid, int gen, int operation, bool ok, const QString &error);
    void offlineStep(const QString &path, int index, int total, double part);
    void offlineDone(int done, int failed, bool cancelled, const QString &error);
    void detailRendered(int ticket, const QString &key, const QImage &image, const QString &error);
    void inited(bool ok, const QString &versionOrError);
    void opened(const QString &path, int variant, int imgid, const QString &error);
    void duplicated(const QString &path, int variant, int newVariant, const QString &error);
    void removed(const QString &path, int variant, const QString &error);
    void swapped(const QString &path, int a, int b, const QString &error);
    void relinked(const QString &oldDir, const QString &newDir, const QString &error);
    void moved(const QString &oldPath, const QString &newPath, const QString &error);
    // `image` is float linear (proofed when proofing); `linear` is unproofed;
    // `plain` is standard sRGB for analysis/cache.
    void rendered(int imgid, int gen, const QImage &image, const QImage &linear, const QImage &plain,
                  const QImage &encoded, const QImage &measured, int fullWidth, int fullHeight, int ms, int flags);
    void proofFailed(const QString &error);
    void paramsRead(int imgid, const QVariantList &params, const QVariantList &history, int historyEnd);
    void localsRead(int imgid, const QVariantList &locals);
    void spotsRead(int imgid, const QVariantList &spots);
    void spotAdded(int imgid, int index);
    void curveRead(int imgid, const QVariantMap &curve);
    void zonesRead(int imgid, const QVariantMap &zones);
    void parametricRead(int imgid, const QVariantMap &parametric);
    void cropRead(int imgid, const QVariantMap &crop);
    void originalRendered(int imgid, int serial, const QImage &image, const QImage &encoded, int width, int height);
    void snapshotRendered(int imgid, int ticket, int id, int comparisonImage, const QImage &image, int width, int height, const QString &error);
    void maskCoverageRendered(int imgid, int priority, int serial, const QImage &image);
    void localAdded(int imgid, int priority);
    void shapeAdded(int imgid, int priority, int index);
    // Errors report diagnostics; only preview completion signals consume
    // the service's pending count. Edits may emit several errors per job.
    void failed(const QString &error);
    void notice(const QString &message);
    void dropped(); // a superseded or failed scheduled preview completed
    void renderFailed(int imgid, int gen); // a failed main preview, not a superseded request
    void exportDone(const QString &path, bool ok, const QString &error);
    void fileExported(const QString &src, const QString &out, bool ok, const QString &error);
    void formats(const QStringList &have);
    void applied(const QString &path, int variant, bool ok, const QString &error, const QImage &render, int flags);
    void exactHistoryReplaced(const QString &path, int variant);
    void lensRead(int imgid, const QVariantMap &lens);
    void inputHistogramRead(int imgid, const QString &op, int serial, const QVariantList &bins);
    void lensCandidatesRead(int imgid, const QStringList &names);
};

// ── service ─────────────────────────────────────────────────────────────
EngineService::EngineService(QObject *parent) : QObject(parent) {
    m_ai = new AiService(this);
    m_denoise = new DenoiseService(this);
    m_imageMatch = new ImageMatchService(this);
    connect(&DisplayColour::instance(), &DisplayColour::changed, this, [this] { emit renderedChanged(); emit originalChanged(); emit snapshotChanged(); emit detailChanged(); });
    m_worker = new EngineWorker;
    connect(m_worker,&EngineWorker::imageMatchReady,this,[this](int ticket,const QString &action,const QVariantMap &result,const QString &error) {
        --m_fileOperations;
        if((action=="apply" || action=="undo") && result.value("committed").toBool()) {
            const QString path=result.value("path").toString();const int variant=result.value("variant").toInt();
            emit imageMatchApplied(path,variant);
            const QImage thumbnail=result.value("thumbnail").value<QImage>();
            if(!thumbnail.isNull()) emit editedRender(path,variant,thumbnail,result.value("flags").toInt());
            if(path==m_path && variant==m_variant) {m_dirty=true;refresh();requestRender();}
        }
        emit imageMatchReady(ticket,action,result,error);
    });
    connect(m_worker, &EngineWorker::aiMaskAccepted, this, [this](int imgid, bool ok, const QString &error) {
        if (imgid == m_imgid) emit aiMaskAccepted(ok, error);
    });
    connect(m_worker,&EngineWorker::aiRepairSourceReady,this,[this](int ticket,const QString &path,const QString &error) {
        --m_fileOperations; emit aiRepairSourceReady(ticket,path,error);
    });
    connect(m_worker,&EngineWorker::aiRepairReady,this,[this](int ticket,const QString &key,const QImage &image,const QString &error) {
        --m_fileOperations; emit aiRepairReady(ticket,key,image,error);
    });
    connect(m_worker, &EngineWorker::aiCopyPrepared, this, [this](int ticket, const QString &error) {
        --m_fileOperations; emit aiCopyPrepared(ticket, error);
    });
    connect(m_worker, &EngineWorker::aiSourceReady, this, [this](int ticket, const QString &path, const QString &error) {
        --m_fileOperations; emit aiSourceReady(ticket, path, error);
    });
    connect(m_worker, &EngineWorker::aiRefinementSourceReady, this, [this](int ticket, const QString &path, const QString &error) {
        --m_fileOperations; emit aiRefinementSourceReady(ticket, path, error);
    });
    connect(m_worker, &EngineWorker::denoiseCopyPrepared, this, [this](const QString &path, const QString &error) {
        --m_fileOperations; emit denoiseCopyPrepared(path, error);
    });
    connect(m_worker, &EngineWorker::denoisePreviewReady, this,
        [this](int ticket, const QImage &before, const QImage &after, const QString &error) {
            --m_fileOperations; emit denoisePreviewReady(ticket, before, after, error);
        });
    m_worker->generation = &m_generation;
    m_worker->reducedPreviews = &m_reducedPreviews;
    m_worker->exportFullResolution = &m_exportFullResolution;
    // A measurement run selects the setting by environment before any
    // preferences are loaded; loadSettings() keeps the same rule.
    if (qgetenv("OMARAW_REDUCED_PREVIEWS") == "1") m_reducedPreviews.store(true);
    if (qgetenv("OMARAW_EXPORT_FULL_RESOLUTION") == "1") m_exportFullResolution.store(true);
    m_worker->detailGeneration = &m_detailGeneration;
    m_worker->snapshotGeneration = &m_snapshotGeneration;
    m_worker->offlineCancel = &m_offlineCancel;
    auto *sourceTimer = new QTimer(this);
    sourceTimer->setInterval(2000);
    connect(sourceTimer, &QTimer::timeout, this, [this] {
        if (m_ready && !m_busy && !m_exporting && !m_fileOperations && !m_path.isEmpty()) load(m_path, m_variant);
        if (m_ready && !m_offlineBusy && !m_pendingSmartPreviews.isEmpty()) {
            const QStringList paths = m_pendingSmartPreviews; m_pendingSmartPreviews.clear(); setSmartPreviews(paths, true);
        }
    });
    sourceTimer->start();
    m_worker->moveToThread(&m_thread);
    connect(m_worker, &EngineWorker::undoMarked, this, [this](int imgid, bool start, int end) {
        --m_marksInFlight;
        // Undo/Redo pressed while this mark was on its way run once it lands.
        const auto release = qScopeGuard([this] { releaseHeldSteps(); });
        if (imgid != m_imgid) return;
        if (start) {
            // What was above the start is replaced only if the group records
            // a step; the read-back that shows it (paramsRead) drops those
            // groups. A group that changes nothing keeps Redo's intact.
            m_groupFrom = end;
        } else if (m_groupFrom >= 0) {
            for (int i = m_groupFrom + 1; i < end; ++i) m_innerSteps.insert(i);
            m_groupFrom = -1;
            storeGroups();
        }
    });
    connect(m_worker, &EngineWorker::offlineStep, this, [this](const QString &path, int index, int total, double part) {
        m_offlineProgress = total > 0 ? (index + part) / total : 0;
        m_offlineStatus = (m_smartJob ? tr("Smart Previews %1/%2 · %3") : tr("Offline copies %1/%2 · %3")).arg(index + 1).arg(total).arg(QFileInfo(path).fileName());
        emit offlineChanged();
    });
    connect(m_worker, &EngineWorker::offlineDone, this, [this](int done, int failed, bool cancelled, const QString &error) {
        --m_fileOperations; m_offlineBusy = false;
        if (!cancelled) m_offlineProgress = 1;
        m_offlineStatus = cancelled ? tr("Offline copies cancelled · %1 completed").arg(done)
            : failed ? tr("Offline copies: %1 completed, %2 failed · %3").arg(done).arg(failed).arg(error)
                     : tr("Offline copies updated for %1 originals").arg(done);
        if (m_smartJob) m_offlineStatus = cancelled ? tr("Smart Preview job cancelled · %1 completed").arg(done)
            : failed ? tr("Smart Previews: %1 completed, %2 failed · %3").arg(done).arg(failed).arg(error)
                     : tr("Smart Previews updated for %1 originals").arg(done);
        setStatus(m_offlineStatus);
        refreshSource();
        emit offlineChanged(); emit offlineFinished(done, failed, cancelled);
        QTimer::singleShot(0, this, &EngineService::renderNextDetail);
    });
    connect(m_worker, &EngineWorker::detailRendered, this, [this](int ticket, const QString &key, const QImage &image, const QString &error) {
        m_detailPending = false;
        m_pendingDetailKey.clear();
        if (ticket == m_detailGeneration.load()) {
            if (!image.isNull()) {
                bool retained;
                {
                    QMutexLocker lock(&m_imageMutex);
                    retained = m_detailCache.insert(key, new QImage(image), qMax(1, int((image.sizeInBytes() + 1023) / 1024)));
                }
                if (!retained) {
                    for (auto &layer : m_detailLayers) layer.wanted.clear();
                    m_detailLimited = true;
                }
            } else {
                // Do not endlessly retry a failing module on the same view.
                for (auto &layer : m_detailLayers) layer.wanted.clear();
                m_detailLimited = false;
                if (!error.isEmpty()) setStatus(tr("Detail preview: %1").arg(error));
            }
        }
        releaseRetainedDetailsIfReady();
        emit detailChanged();
        QTimer::singleShot(0, this, &EngineService::renderNextDetail);
    });
    connect(this, &EngineService::busyChanged, this, &EngineService::renderNextDetail);
    connect(this, &EngineService::exportQueueChanged, this, &EngineService::renderNextDetail);
    connect(this, &EngineService::syncChanged, this, &EngineService::renderNextDetail);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &EngineWorker::inited, this, [this](bool ok, const QString &v) {
        m_available = ok;
        m_ready = ok;
        m_version = ok ? v : QString();
        setStatus(ok ? tr("Ready to develop") : tr("Engine unavailable: %1").arg(v));
        emit availableChanged();
        if (ok) QMetaObject::invokeMethod(m_worker, "probeFormats", Qt::QueuedConnection);
        if (ok && !m_path.isEmpty()) load(m_path);
    });
    connect(m_worker, &EngineWorker::formats, this, [this](const QStringList &have) {
        m_formats = have; notifyExportQueue();
        // Jobs added immediately after startup can arrive before codec
        // discovery. Start those jobs once discovery finishes, while
        // preserving the explicit pause on a recovered queue.
        if (!m_exporting && !m_exportPaused && !m_formats.isEmpty()
            && std::any_of(m_queue.cbegin(), m_queue.cend(), [](const QVariant &v) { return v.toMap().value("status") == "queued"; }))
            exportNext();
    });
    connect(m_worker, &EngineWorker::duplicated, this, [this](const QString &path, int variant, int result, const QString &error) {
        --m_fileOperations;
        if (error.isEmpty() && result >= 0) copyGroups(path, variant, result);
        emit imageDuplicated(path, variant, result, error);
        QTimer::singleShot(0, this, &EngineService::renderNextDetail);
    });
    connect(m_worker, &EngineWorker::relinked, this, [this](const QString &o, const QString &n, const QString &err) {
        --m_fileOperations;
        if (err.isEmpty()) relocateGroups(o, n, true);
        // The loaded image, if it lived there, must be opened again at its new path.
        if (err.isEmpty() && (m_path == o || m_path.startsWith(o + QLatin1Char('/')))) { m_path.clear(); m_imgid = -1; emit imageChanged(); }
        emit folderRelinked(o, n, err);
        QTimer::singleShot(0, this, &EngineService::renderNextDetail);
    });
    connect(m_worker, &EngineWorker::moved, this, [this](const QString &o, const QString &n, const QString &err) {
        --m_fileOperations;
        if (err.isEmpty()) relocateGroups(o, n, false);
        // The open photo keeps its engine id under the new name; the
        // develop was dropped on the worker, so load it again.
        if (err.isEmpty() && m_path == o) { m_path = n; emit imageChanged(); const int v = m_variant; m_imgid = -1; load(n, v); }
        emit imageMoved(o, n, err);
        QTimer::singleShot(0, this, &EngineService::renderNextDetail);
    });
    connect(m_worker, &EngineWorker::removed, this, [this](const QString &path, int variant, const QString &err) {
        --m_fileOperations;
        if (err.isEmpty()) forgetGroups(path, variant);
        if (err.isEmpty() && path == m_path && variant == m_variant) { m_imgid = -1; emit imageChanged(); }
        emit imageRemoved(path, variant, err);
        QTimer::singleShot(0, this, &EngineService::renderNextDetail);
    });
    connect(m_worker, &EngineWorker::applied, this, [this](const QString &path, int variant, bool ok, const QString &err, const QImage &render, int flags) {
        // A partly applied edit still changed the photo: its thumbnail follows.
        if (!render.isNull()) emit editedRender(path, variant, render, flags);
        if (ok) ++m_syncDone;
        else { ++m_syncFailed; setStatus(tr("Could not apply settings to %1: %2").arg(QFileInfo(path).fileName(), err)); }
        --m_syncPending;
        emit syncChanged();
        if (m_syncPending <= 0) {
            m_syncPending = 0;
            if (!m_syncFailed) setStatus(m_syncDone == 1 ? tr("Applied settings to one photo") : tr("Applied settings to %1 photos").arg(m_syncDone));
            emit syncFinished(m_syncDone, m_syncFailed);
        }
    });
    connect(m_worker, &EngineWorker::exactHistoryReplaced, this, [this](const QString &path, int variant) {
        forgetGroups(path, variant);
        if (path == m_path && variant == m_variant && m_imgid >= 0) {
            refresh(); requestRender();
        }
    });
    connect(m_worker, &EngineWorker::swapped, this, [this](const QString &path, int a, int b, const QString &err) {
        --m_fileOperations;
        if (err.isEmpty()) copyGroups(path, a, b, true);
        // The loaded image keeps its engine id; only its version label moved.
        if (err.isEmpty() && path == m_path) { if (m_variant == a) m_variant = b; else if (m_variant == b) m_variant = a; emit imageChanged(); }
        emit versionsSwapped(path, a, b, err);
        QTimer::singleShot(0, this, &EngineService::renderNextDetail);
    });
    connect(m_worker, &EngineWorker::opened, this, [this](const QString &path, int variant, int id, const QString &err) {
        // A superseded open still finished: account for it before deciding
        // whether it matters, or the busy counter never reaches zero.
        --m_pending;
        if (path != m_path || variant != m_variant) { setBusy(m_pending > 0); return; } // user moved on
        if (id < 0) { m_openFailed = true; setStatus(tr("Engine could not open %1: %2").arg(QFileInfo(path).fileName(), err)); setBusy(m_pending > 0); return; }
        m_imgid = id;
        m_curveHistogram.clear(); m_curveHistogramSerial = 0; m_curveHistogramInFlight = false;
        emit curveHistogramChanged();
        emit imageChanged();
        refresh();
        requestRender();
        if (m_snapshotId) requestSnapshot();
    });
    connect(m_worker, &EngineWorker::exposureMeterRead, this,
            [this](int imgid, int gen, int serial, bool valid, double exposure, const QString &error) {
        --m_pending;
        if (serial != m_autoSerial || !m_auto.waiting || imgid != m_imgid || gen != m_generation.load()) {
            if (serial == m_autoSerial) cancelAuto();
            setBusy(m_pending > 0); return;
        }
        const AutoRun run = m_auto;
        m_auto = AutoRun();
        if (!valid) {
            setStatus(error.isEmpty() ? tr("Not enough image detail to calculate exposure") : error);
        } else if (!run.restored && !run.needsExposure && qAbs(exposure - run.from) < 0.1) {
            m_lastAuto = {m_path, m_variant, m_historyEnd, run.from, m_sourceStamp};
            setStatus(tr("Exposure is already set"));
        } else {
            // One absolute setting, one undo group, one published render.
            // The meter never writes trial edits or consumes the redo branch.
            beginUndoGroup();
            QVariantList values{QVariantMap{{"op", "exposure"}, {"field", "exposure"}, {"value", exposure}},
                                QVariantMap{{"op", "exposure"}, {"field", "mode"}, {"value", 0}}};
            if (run.restored) values << QVariantMap{{"op", "sigmoid"}, {"enabled", true}};
            m_autoApplying = true;
            applyValues(values);
            m_autoApplying = false;
            endUndoGroup();
            m_lastAuto = {m_path, m_variant, -1, exposure, m_sourceStamp};
            setStatus(tr("Exposure set to %1 EV").arg(exposure, 0, 'f', 2));
        }
        setBusy(m_pending > 0);
    });
    connect(m_worker, &EngineWorker::proofFailed, this, [this](const QString &e) { setStatus(tr("Soft proof: %1").arg(e)); });
    connect(m_worker, &EngineWorker::geometryFinished, this, [this](int imgid, int gen, int operation, bool ok, const QString &error) {
        --m_pending;
        m_geometryBusy = false;
        const bool current = imgid == m_imgid && gen == m_generation.load();
        if (current) {
            m_geometryStatus = ok ? tr("Correction applied. Undo restores the previous geometry.") : error;
            if (ok) { m_dirty = true; refresh(); }
            // Geometry status and drawing instructions can resize the viewer.
            // Catch up to its final bounds after analysis, including failure.
            requestRender();
        }
        emit geometryChanged();
        setBusy(m_pending > 0);
        if (current && (ok || !error.isEmpty())) emit geometryCompleted(operation, ok);
    });
    connect(m_worker, &EngineWorker::rendered, this, [this](int imgid, int gen, const QImage &img, const QImage &linear, const QImage &plain,
                                                            const QImage &encoded, const QImage &measured, int fullWidth, int fullHeight, int ms, int flags) {
        --m_pending;
        if (imgid != m_imgid || gen != m_generation.load()) { setBusy(m_pending > 0); return; }
        m_renderedGeneration = gen;
        { QMutexLocker l(&m_imageMutex); m_viewImage = img; m_linearImage = linear; m_image = encoded; m_plain = plain; }
        ++m_renderSerial;
        m_renderMs = ms;
        // The histogram and scopes describe the picture as it will come out,
        // the soft proof included — but never the proof's gamut-warning paint,
        // which is a marker, not a colour of the picture.
        { QMutexLocker l(&m_imageMutex); m_measured = measured.isNull() ? m_image : measured; }
        m_histogram = Backend::histogramOf(m_measured.scaled(256, 256, Qt::KeepAspectRatio));
        rebuildScope();
        // Geometry belongs to this accepted frame, including its crop mode.
        // Publishing sensor dimensions during parameter refresh made the fit
        // view breathe and needlessly reset native zoom/detail requests.
        if (fullWidth > 0 && fullHeight > 0 && (fullWidth != m_fullW || fullHeight != m_fullH)) {
            clearRetainedDetails();
            m_fullW = fullWidth; m_fullH = fullHeight;
            emit fullSizeChanged();
        }
        emit renderedChanged();
        rebuildMask();   // the ranges read the render, so it follows every one
        rebuildClipping();
        if (autoRunning()) QTimer::singleShot(0, this, &EngineService::continueAuto);
        requestRawClipping();
        requestRetouchPreview();
        setStatus(tr("Rendered %1 × %2 in %3 ms").arg(img.width()).arg(img.height()).arg(ms));
        if (m_dirty) { m_dirty = false; emit editedRender(m_path, m_variant, plain, flags); }
        setBusy(m_pending > 0);
        rebuildDetails();
    });
    connect(m_worker, &EngineWorker::rawClippingRead, this, [this](int imgid, int gen, int serial, const QImage &image, const QString &status) {
        if (imgid!=m_imgid || gen!=m_generation.load() || serial!=m_rawClippingSerial || !m_rawClippingShown) return;
        { QMutexLocker l(&m_imageMutex); m_rawClippingImage=image; }
        m_rawClippingStatus=status;
        emit rawClippingChanged();
    });
    connect(m_worker,&EngineWorker::retouchPreviewRead,this,[this](int imgid,int gen,int serial,const QImage &image,const QString &error){
        if(imgid!=m_imgid || gen!=m_generation.load() || serial!=m_retouchPreviewSerial)return;
        {QMutexLocker l(&m_imageMutex);m_retouchPreviewImage=image;}
        emit retouchPreviewChanged();if(!error.isEmpty())setStatus(error);
    });
    connect(m_worker, &EngineWorker::paramsRead, this, [this](int imgid, const QVariantList &p, const QVariantList &h, int end) {
        ++m_receivedParams;
        if (imgid != m_imgid) return;
        ++m_paramsSerial;
        // An edit made below the top of history replaces everything above
        // it, and the new steps reuse those numbers: what was inside an undo
        // group up there is gone. A jump (undo/redo) moves the end without
        // replacing anything, so its own read-back is told apart.
        const bool jumped = m_readIsJump;
        m_readIsJump = false;
        if (jumped) { if (m_jumpTarget == m_readJumpTarget) m_jumpTarget = -1; }
        else if (m_historyEnd < m_history.size() && end == h.size())
            for (auto it = m_innerSteps.begin(); it != m_innerSteps.end();) it = *it > m_historyEnd ? m_innerSteps.erase(it) : std::next(it);
        // One edit made after an Undo can add more than one step: the engine
        // brings the always-on modules' placeholder steps back with it. They
        // change nothing, so Undo takes them with the edit, as one step.
        if (!jumped && m_groupFrom < 0 && m_historyEnd >= 0 && m_historyEnd < m_history.size() && end > m_historyEnd + 1 && end == h.size())
            for (int i = m_historyEnd + 1; i < end; ++i) m_innerSteps.insert(i);
        if (!jumped) storeGroups();
        // Past the end of an old branch, the recorded Lab masters above it are gone.
        if (!jumped && m_historyEnd < m_history.size() && end == h.size()) {
            pruneLabMasters(m_historyEnd);
            // A new branch reuses the deleted rows' numbers. The old Auto
            // result belonged to those edits, not to the numeric index.
            if (m_lastAuto.path == m_path && m_lastAuto.variant == m_variant && m_lastAuto.end > m_historyEnd)
                m_lastAuto = {};
        }
        // A Lab slider's step has landed: remember the master it was set with,
        // so Undo, Redo and reopening show the sliders as they were moved.
        if (!jumped && m_labPending && m_labMasterKnown) { recordLabMaster(end, m_labMaster); }
        if (!jumped) m_labPending = false;
        m_params = p;
        m_history = h;
        m_historyEnd = end;
        if (m_lastAuto.end < 0 && m_lastAuto.path == m_path && !m_auto.waiting && m_undoDepth == 0) m_lastAuto.end = end;
        paramsTouched();
        emit historyChanged();
        if (m_heldSteps) QTimer::singleShot(0, this, &EngineService::releaseHeldSteps);
        if (autoRunning()) QTimer::singleShot(0, this, &EngineService::continueAuto);
    });
    connect(m_worker, &EngineWorker::historyReset, this, [this](int imgid) {
        if (imgid != m_imgid) return;
        // Runs after all earlier worker marks and before the reset read-back.
        // Saved grouping/Lab state must not attach to newly numbered edits.
        const QString key = groupKey(m_path, m_variant);
        const bool groups = m_savedGroups.remove(key) > 0;
        const bool lab = m_savedGroups.remove(QStringLiteral("lab|") + key) > 0;
        if (groups || lab) writeGroupsFile();
        m_innerSteps.clear(); m_groupFrom = -1; m_jumpTarget = -1; m_readIsJump = false;
        m_labMasterKnown = false; m_labPending = false;
        // resetTo already cancelled the old gesture and Auto run. Do not
        // cancel a newer edit/Auto request submitted while reset was queued.
        m_history.clear(); m_historyEnd = 0;
    });
    // History rows were deleted with a removed local: every remembered step
    // number above them is stale. A group still open restarts here.
    connect(m_worker, &EngineWorker::historyRewritten, this, [this](int imgid, int end) {
        if (imgid != m_imgid) return;
        m_lastAuto = {};
        m_innerSteps.clear(); m_jumpTarget = -1;
        if (m_groupFrom >= 0) m_groupFrom = end;
        storeGroups();
    });
    // A jump that did not happen must not make a later edit look like one.
    connect(m_worker, &EngineWorker::jumpFailed, this, [this](int imgid) { if (imgid == m_imgid) m_jumpTarget = -1; });
    connect(m_worker, &EngineWorker::jumpRead, this, [this](int imgid, int target) { if (imgid == m_imgid) { m_readIsJump = true; m_readJumpTarget = target; } });
    connect(m_worker, &EngineWorker::sidecarEditRead, this, &EngineService::sidecarEditReady);
    connect(m_worker, &EngineWorker::basecurveRead, this, [this](int imgid, bool on) { if (imgid == m_imgid) m_basecurveOn = on; });
    connect(m_worker,&EngineWorker::toolStateRead,this,[this](int imgid,const QVariantMap &state){if(imgid==m_imgid){m_toolState=state;emit toolStateChanged();}});
    connect(m_worker, &EngineWorker::spotsRead, this, [this](int imgid, const QVariantList &l) {
        if (imgid != m_imgid) return;
        m_spots = l;
        if (m_activeSpot >= l.size()) setActiveSpot(l.size() - 1);
        emit spotsChanged();
    });
    connect(m_worker, &EngineWorker::spotAdded, this, [this](int imgid, int i) { if (imgid == m_imgid) setActiveSpot(i); });
    connect(m_worker, &EngineWorker::curveRead, this, [this](int imgid, const QVariantMap &c) { if (imgid != m_imgid) return; m_curve = c; emit curveChanged(); });
    connect(m_worker, &EngineWorker::zonesRead, this, [this](int imgid, const QVariantMap &z) { if (imgid != m_imgid) return; m_zones = z; emit zonesChanged(); });
    connect(m_worker, &EngineWorker::parametricRead, this, [this](int imgid, const QVariantMap &p) {
        if (imgid != m_imgid) return;
        m_parametric = p;
        if (m_labImage != imgid) { m_labImage = imgid; m_labMasterKnown = false; }
        refreshLabColour();
        emit parametricChanged();
    });
    connect(m_worker, &EngineWorker::cropRead, this, [this](int imgid, const QVariantMap &c) { if (imgid != m_imgid) return; m_crop = c; emit cropChanged(); });
    connect(m_worker, &EngineWorker::inputHistogramRead, this, [this](int imgid, const QString &op, int serial, const QVariantList &bins) {
        if (imgid != m_imgid || op != QLatin1String("rgbcurve")) return;
        m_curveHistogramInFlight = false;
        if (!bins.isEmpty()) { m_curveHistogram = bins; emit curveHistogramChanged(); }
        // Settings moved on while it was being made: one more, for the latest.
        if (serial != m_paramsSerial) loadCurveHistogram();
    });
    connect(m_worker, &EngineWorker::lensRead, this, [this](int imgid, const QVariantMap &l) { if (imgid != m_imgid || l == m_lensProfile) return; m_lensProfile = l; emit lensProfileChanged(); });
    connect(m_worker, &EngineWorker::cameraProfilesRead, this, [this](int imgid, const QVariantMap &state) {
        if (imgid != m_imgid || state == m_cameraProfileState) return;
        m_cameraProfileState = state; emit cameraProfileStateChanged();
    });
    connect(m_worker, &EngineWorker::lensCandidatesRead, this, [this](int imgid, const QStringList &names) { if (imgid != m_imgid) return; m_lensCandidates = names; emit lensCandidatesChanged(); });
    connect(m_worker, &EngineWorker::localsRead, this, [this](int imgid, const QVariantList &l) {
        if (imgid != m_imgid) return;
        m_locals = l;
        bool stillThere = false;
        for (const QVariant &v : l) if (v.toMap().value(QStringLiteral("priority")).toInt() == m_activeLocal) stillThere = true;
        if (!stillThere) { setActiveLocal(l.isEmpty() ? -1 : l.last().toMap().value(QStringLiteral("priority")).toInt()); setActiveShape(0); }
        // A removed shape must not leave the panel and overlay pointing past
        // the end of the group.
        const int n = shapes().size();
        if (m_activeShape >= n) setActiveShape(qMax(0, n - 1));
        emit localsChanged();
        rebuildMask();
    });
    connect(m_worker, &EngineWorker::originalRendered, this, [this](int imgid, int serial, const QImage &img, const QImage &encoded, int width, int height) {
        --m_pending;
        setBusy(m_pending > 0);
        if (imgid != m_imgid || serial != m_originalGeneration) return;
        { QMutexLocker l(&m_imageMutex); m_originalView = img; m_original = encoded; }
        m_originalFullW = width; m_originalFullH = height;
        ++m_originalSerial;
        emit originalChanged();
        rebuildDetails();
    });
    connect(m_worker, &EngineWorker::snapshotRendered, this, [this](int imgid, int ticket, int id, int comparisonImage,
                                                                   const QImage &image, int width, int height, const QString &error) {
        --m_pending;
        if (imgid == m_imgid && ticket == m_snapshotGeneration.load() && id == m_snapshotId) {
            m_snapshotEngineId = image.isNull() ? -1 : comparisonImage;
            m_snapshotFullW = image.isNull() ? 0 : width; m_snapshotFullH = image.isNull() ? 0 : height;
            { QMutexLocker lock(&m_imageMutex); m_snapshotView = image; }
            ++m_snapshotSerial;
            m_snapshotStatus = image.isNull() ? tr("Snapshot detail unavailable. Showing saved preview.%1")
                .arg(error.isEmpty() ? QString() : " " + error) : QString();
            emit snapshotChanged();
            rebuildDetails();
        }
        setBusy(m_pending > 0);
    });
    connect(m_worker, &EngineWorker::maskCoverageRendered, this, [this](int imgid, int priority, int serial, const QImage &img) {
        --m_pending;
        setBusy(m_pending > 0);
        m_maskCoveragePending = false;
        if (imgid != m_imgid || priority != m_activeLocal || serial != m_renderSerial) { rebuildMask(); return; }
        m_maskCoverage = img;
        m_maskCoverageFor = priority;
        m_maskCoverageSerial = serial;
        paintMask();
    });
    connect(m_worker, &EngineWorker::localAdded, this, [this](int imgid, int p) { if (imgid == m_imgid && p > 0) { setActiveLocal(p); setActiveShape(0); } });
    connect(m_worker, &EngineWorker::shapeAdded, this, [this](int imgid, int p, int i) { if (imgid == m_imgid && p == m_activeLocal) setActiveShape(i); });
    connect(m_worker, &EngineWorker::dropped, this, [this] {
        --m_pending;
        setBusy(m_pending > 0);
    });
    connect(m_worker, &EngineWorker::renderFailed, this, [this](int imgid, int gen) {
        if (imgid == m_imgid && gen == m_generation.load()) {
            m_failedGeneration = gen;
            cancelAuto();
        }
    });
    connect(m_worker, &EngineWorker::failed, this, [this](const QString &e) {
        setStatus(tr("Engine error: %1").arg(e));
        emit engineFailed(e);
    });
    connect(m_worker, &EngineWorker::notice, this, [this](const QString &message) { setStatus(message); });
    connect(m_worker, &EngineWorker::exportDone, this, [this](const QString &path, bool ok, const QString &err) {
        setStatus(ok ? tr("Exported %1").arg(path) : tr("Export failed: %1").arg(err));
        emit exported(path, ok);
    });
    connect(m_worker, &EngineWorker::fileExported, this, [this](const QString &src, const QString &out, bool ok, const QString &err) {
        for (int i = 0; i < m_queue.size(); ++i) {
            QVariantMap row = m_queue[i].toMap();
            if (row.value(QStringLiteral("file")) != src || row.value(QStringLiteral("status")) != QLatin1String("rendering")) continue;
            // (one row renders at a time, so file + status is enough even with variants)
            row[QStringLiteral("status")] = ok ? QStringLiteral("done") : QStringLiteral("failed");
            row[QStringLiteral("out")] = out;
            row[QStringLiteral("error")] = err;
            const QVariantMap options = row.value(QStringLiteral("options")).toMap();
            const QString folder = row.value(QStringLiteral("folder")).toString();
            if (ok && (options.value(QStringLiteral("copyOriginal")).toBool() || options.value(QStringLiteral("copySidecar")).toBool())) {
                // The source beside its export, never over anything that is there.
                auto place = [](const QString &from, const QString &folder, const QString &name = QString()) {
                    if (!QFile::exists(from)) return QString();
                    const QFileInfo fi(name.isEmpty() ? from : name);
                    QString to = folder + QLatin1Char('/') + fi.fileName();
                    for (int n = 2; QFile::exists(to) && n < 1000; ++n) to = folder + QLatin1Char('/') + fi.completeBaseName() + QStringLiteral("-%1").arg(n) + (fi.suffix().isEmpty() ? QString() : QLatin1Char('.') + fi.suffix());
                    return QFile::copy(from, to) ? to : QString();
                };
                if (options.value(QStringLiteral("copyOriginal")).toBool()) {
                    const QString c = place(OfflineStore::instance().resolve(src), folder, src);
                    if (c.isEmpty()) row[QStringLiteral("error")] = tr("exported; the original could not be copied");
                    else row[QStringLiteral("original")] = c;
                }
                if (options.value(QStringLiteral("copySidecar")).toBool()) {
                    // Named after the copy beside it, when there is one (it may be "-2").
                    const QString copied = row.value(QStringLiteral("original")).toString();
                    if (!copied.isEmpty()) { for (const auto &sc : Metadata::sidecarsFollowing(src, copied)) if (!QFile::exists(sc.second)) QFile::copy(sc.first, sc.second); }
                    else if (const QString sc = Metadata::existingSidecar(src); !sc.isEmpty()) place(sc, folder);
                }
            }
            m_queue[i] = row;
            break;
        }
        if (ok) ++m_exportDone; else ++m_exportFailed;
        {
            int variant = 0;
            for (const QVariant &v : std::as_const(m_queue)) { const QVariantMap r = v.toMap(); if (r.value(QStringLiteral("file")) == src && r.value(QStringLiteral("out")) == out) { variant = r.value(QStringLiteral("variant")).toInt(); break; } }
            emit sourceExported(src, variant, ok);
        }
        notifyExportQueue();
        exportNext();
    });
    m_thread.start();
}

QObject *EngineService::workerObject() const { return m_worker; }

EngineService::~EngineService() {
    m_imageMatch->cancel();
    m_ai->cancel();
    m_denoise->cancel();
    ColourPipeline::instance().setPrimaryGradeInteractive(this, false);
    m_offlineCancel.store(true);
    invalidatePreview();
    ++m_snapshotGeneration;
    QMetaObject::invokeMethod(m_worker, "clearSnapshot", Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(m_worker,[worker=m_worker]{worker->clearMatchReference();},Qt::BlockingQueuedConnection);
    m_thread.quit();
    // A full-resolution encoder may take longer than ten seconds. Never
    // destroy its thread while it still owns an image or writes an output.
    m_thread.wait();
}

void EngineService::start(const QString &prefix, const QString &configDir, const QString &cacheDir, const QString &library) {
    m_rawCameraDatabase = prefix + QStringLiteral("/share/darktable/rawspeed/cameras.xml");
    QDir().mkpath(QFileInfo(library).absolutePath());
    m_queuePath = library + QStringLiteral(".exports.json");
    m_groupsPath = library + QStringLiteral(".undo-groups.json");
    {
        QFile f(m_groupsPath);
        if (f.open(QIODevice::ReadOnly)) m_savedGroups = QJsonDocument::fromJson(f.readAll()).object().toVariantMap();
    }
    m_queueUnreadable = !ExportState::load(m_queuePath, &m_queue, &m_queueError);
    m_exportPaused = std::any_of(m_queue.cbegin(), m_queue.cend(), [](const QVariant &v) { return v.toMap().value("status") == "queued"; });
    notifyExportQueue();
    setStatus(tr("Starting engine…"));
    QMetaObject::invokeMethod(m_worker, "init", Qt::QueuedConnection,
                              Q_ARG(QString, prefix), Q_ARG(QString, configDir), Q_ARG(QString, cacheDir), Q_ARG(QString, library));
}

void EngineService::setStatus(const QString &s) { if (s == m_status) return; m_status = s; emit statusChanged(); }
void EngineService::setBusy(bool b) { if (b == m_busy) return; m_busy = b; emit busyChanged(); }

QVariantMap EngineService::offlineInfo(const QString &path) const { return OfflineStore::instance().info(path); }

void EngineService::setOfflineCopies(const QStringList &paths, bool keep) {
    if (!m_ready || m_offlineBusy || paths.isEmpty()) return;
    QStringList unique = paths; unique.removeDuplicates();
    invalidateDetails();
    m_smartJob = false;
    ++m_fileOperations; m_offlineBusy = true; m_offlineProgress = 0; m_offlineCancel.store(false);
    m_offlineStatus = keep ? tr("Preparing verified offline originals…") : tr("Checking originals before removing offline copies…");
    emit offlineChanged();
    QMetaObject::invokeMethod(m_worker, "offlineCopies", Qt::QueuedConnection, Q_ARG(QStringList, unique), Q_ARG(bool, keep), Q_ARG(bool, false));
}

QVariantMap EngineService::smartPreviewInfo(const QString &path) const { return OfflineStore::previews().info(path); }

void EngineService::setSmartPreviews(const QStringList &paths, bool keep) {
    if (paths.isEmpty()) return;
    if (!m_ready || m_offlineBusy) {
        if (keep) { m_pendingSmartPreviews.append(paths); m_pendingSmartPreviews.removeDuplicates(); }
        return;
    }
    QStringList unique = paths; unique.removeDuplicates();
    invalidateDetails(); m_smartJob = true;
    ++m_fileOperations; m_offlineBusy = true; m_offlineProgress = 0; m_offlineCancel.store(false);
    m_offlineStatus = keep ? tr("Building Smart Previews…") : tr("Checking originals before discarding Smart Previews…");
    emit offlineChanged();
    QMetaObject::invokeMethod(m_worker, "offlineCopies", Qt::QueuedConnection, Q_ARG(QStringList, unique), Q_ARG(bool, keep), Q_ARG(bool, true));
}

void EngineService::refreshSource() {
    m_sourceStamp.clear();
    if (!m_path.isEmpty()) load(m_path, m_variant);
    emit sourceChanged();
}

void EngineService::cancelOfflineCopies() {
    if (!m_offlineBusy) return;
    m_pendingSmartPreviews.clear();
    m_offlineCancel.store(true); m_offlineStatus = m_smartJob ? tr("Cancelling Smart Previews…") : tr("Cancelling offline copies…"); emit offlineChanged();
}

// What the open photo's source looks like: its kind (original, offline copy,
// smart preview, missing) and the file's size and date. A change reopens it.
QString EngineService::sourceStamp(const QString &path, QString *kindOut) const {
    const QFileInfo original(path);
    const auto full = offlineInfo(path), smart = smartPreviewInfo(path);
    const QString kind = original.isFile() && original.isReadable() ? QStringLiteral("original")
        : full.value("available").toBool() ? QStringLiteral("offline-copy")
        : smart.value("available").toBool() ? QStringLiteral("smart-preview") : QStringLiteral("missing");
    if (kindOut) *kindOut = kind;
    return kind + ':' + QString::number(original.size()) + ':' + QString::number(original.lastModified().toMSecsSinceEpoch())
        + ':' + smart.value("path").toString() + ':' + full.value("path").toString();
}

// OmaRAW itself rewrote the file's metadata (in-file XMP): the pixels are the
// same, so the open photo is not reopened as if something else had changed it.
void EngineService::sourceRewritten(const QString &path) {
    const bool open = path == m_path && !m_sourceStamp.isEmpty();
    if (!open && m_lastAuto.path != path) return;
    const QString stamp = sourceStamp(path);
    if (open) m_sourceStamp = stamp;
    if (m_lastAuto.path == path) m_lastAuto.sourceStamp = stamp;
}

void EngineService::load(const QString &path, int variant) {
    QString kind;
    const QString stamp = sourceStamp(path, &kind);
    // Already open (or opening), or it failed to open and nothing about its
    // file has changed since: trying again every two seconds would only
    // reset the whole view and repeat the same error.
    if (path == m_path && variant == m_variant && (m_imgid >= 0 || m_busy || m_openFailed) && stamp == m_sourceStamp) return;
    // Only a real (re)open forgets what belonged to the photo: the source
    // timer calls load() every two seconds and must not wipe undo groups.
    // Undo groups belong to a photo's history, which outlives this window:
    // the one being left keeps its groups, the one opening gets its own back.
    storeGroups();
    m_innerSteps.clear(); m_groupFrom = -1; m_jumpTarget = -1; m_readIsJump = false;
    for (const QVariant &v : m_savedGroups.value(groupKey(path, variant)).toList()) m_innerSteps.insert(v.toInt());
    m_printHeld.clear();                      // held for the photo it was taken on
    { QMutexLocker l(&m_imageMutex); m_measured = QImage(); }   // no meters from the last photo
    m_openFailed = false;
    m_sourceStamp = stamp;
    if (m_sourceKind != kind) { m_sourceKind = kind; QMetaObject::invokeMethod(this, [this] { emit sourceChanged(); }, Qt::QueuedConnection); }
    if (path == m_path && variant == m_variant && m_snapshotId > 0) {
        // A drive/source change reloads the same photo. Keep its selected
        // snapshot and saved overview while cancelling the old native render;
        // otherwise the source timer dismisses comparison as it goes offline.
        ++m_snapshotGeneration;
        m_snapshotEngineId = -1; m_snapshotFullW = m_snapshotFullH = 0;
        { QMutexLocker lock(&m_imageMutex); m_snapshotView = {}; }
        ++m_snapshotSerial;
        m_snapshotStatus = tr("Snapshot detail unavailable. Showing saved preview.");
        m_detailLayers[1].view = {}; m_detailLayers[1].requestedScale = 0;
        QMetaObject::invokeMethod(m_worker, "clearSnapshot", Qt::QueuedConnection);
        emit snapshotChanged();
    } else clearSnapshotComparison();
    invalidatePreview();
    m_geometryStatus.clear(); emit geometryChanged();
    ColourPipeline::instance().setPrimaryGradeInteractive(this, false);
    for (const auto &connection : std::as_const(m_previewEditors)) disconnect(connection);
    m_previewEditors.clear();
    m_interactivePreviewSized = false;
    m_previewParamsDeferred = false;
    m_editKey.clear();
    m_labMasterKnown = false;
    // An Auto search still under way ends on the photo it started on, and
    // an Undo held for it belonged to that photo.
    cancelAuto();
    m_heldSteps = 0;
    m_path = path;
    m_variant = variant;
    m_imgid = -1;
    m_dirty = false;
    m_toolState.clear(); emit toolStateChanged();
    { QMutexLocker l(&m_imageMutex); m_image = QImage(); m_original = QImage(); m_viewImage = QImage(); m_originalView = QImage(); m_linearImage = QImage(); }
    rebuildScope(); requestRawClipping();
    setRetouchPreviewScale(-1);
    m_originalRequested = false; ++m_originalGeneration;
    m_originalFullW = m_originalFullH = 0;
    m_detailLayers[1].view = {};
    emit originalChanged();
    m_basecurveOn = false;
    m_params.clear(); m_history.clear(); m_historyEnd = 0; m_locals.clear(); m_activeLocal = -1; m_activeShape = 0;
    if (m_fullW || m_fullH) { m_fullW = m_fullH = 0; emit fullSizeChanged(); }
    if (!m_lensProfile.isEmpty()) { m_lensProfile.clear(); emit lensProfileChanged(); }
    if (!m_cameraProfileState.isEmpty()) { m_cameraProfileState.clear(); emit cameraProfileStateChanged(); }
    if (!m_lensCandidates.isEmpty()) { m_lensCandidates.clear(); emit lensCandidatesChanged(); }
    emit imageChanged(); paramsTouched(); emit historyChanged(); emit renderedChanged(); emit localsChanged(); emit activeLocalChanged(); emit activeShapeChanged();
    rebuildMask();
    if (!m_ready || path.isEmpty()) return;
    ++m_pending; setBusy(true);
    QMetaObject::invokeMethod(m_worker, "open", Qt::QueuedConnection, Q_ARG(QString, path), Q_ARG(int, variant));
}

void EngineService::duplicateImage(const QString &path, int variant) {
    if (!m_ready) { emit imageDuplicated(path, variant, -1, tr("engine not ready")); return; }
    ++m_fileOperations;
    QMetaObject::invokeMethod(m_worker, "duplicate", Qt::QueuedConnection, Q_ARG(QString, path), Q_ARG(int, variant));
}

void EngineService::removeImage(const QString &path, int variant) {
    if (!m_ready) { emit imageRemoved(path, variant, tr("engine not ready")); return; }
    ++m_fileOperations;
    QMetaObject::invokeMethod(m_worker, "remove", Qt::QueuedConnection, Q_ARG(QString, path), Q_ARG(int, variant));
}

void EngineService::relinkFolder(const QString &oldDir, const QString &newDir) {
    if (!m_ready) { emit folderRelinked(oldDir, newDir, QString()); return; } // nothing to move yet
    ++m_fileOperations;
    QMetaObject::invokeMethod(m_worker, "relink", Qt::QueuedConnection, Q_ARG(QString, oldDir), Q_ARG(QString, newDir));
}

void EngineService::moveImage(const QString &oldPath, const QString &newPath) {
    if (!m_ready) { emit imageMoved(oldPath, newPath, QString()); return; }   // nothing to carry yet
    ++m_fileOperations;
    QMetaObject::invokeMethod(m_worker, "move", Qt::QueuedConnection, Q_ARG(QString, oldPath), Q_ARG(QString, newPath));
}

void EngineService::swapVersions(const QString &path, int a, int b) {
    if (!m_ready) { emit versionsSwapped(path, a, b, tr("engine not ready")); return; }
    ++m_fileOperations;
    QMetaObject::invokeMethod(m_worker, "swap", Qt::QueuedConnection, Q_ARG(QString, path), Q_ARG(int, a), Q_ARG(int, b));
}

void EngineService::pushProof() {
    QMetaObject::invokeMethod(m_worker, "setProof", Qt::QueuedConnection,
                              // False colour replaces the picture, so the gamut warning's
                              // paint would only be counted as a colour band at 100%.
                              Q_ARG(QString, proofing() ? m_proofProfile : QString()), Q_ARG(int, m_proofIntent), Q_ARG(bool, m_proofGamut && m_scopeMode != 4));
    emit proofChanged();
    requestRender();
    requestSnapshot();
    if (m_originalRequested) requestOriginal();
}

void EngineService::setProofOn(bool on) { if (on == m_proofOn) return; m_proofOn = on; pushProof(); }

void EngineService::setProofProfile(const QString &pathOrUrl) {
    QString p = pathOrUrl;
    if (p.startsWith(QLatin1String("file:"))) p = QUrl(p).toLocalFile();
    if (p == m_proofProfile) return;
    m_proofProfile = p;
    m_proofName = Proof::profileName(p);
    if (!p.isEmpty()) m_proofOn = true;
    pushProof();
}

void EngineService::setProofIntent(int i) { i = qBound(0, i, 3); if (i == m_proofIntent) return; m_proofIntent = i; pushProof(); }
void EngineService::setProofGamutWarning(bool on) { if (on == m_proofGamut) return; m_proofGamut = on; pushProof(); }

QString EngineService::originalSource() const {
    if (m_imgid < 0 || m_original.isNull()) return QString();
    return QStringLiteral("image://engine-original/%1?g=%2&display=%3").arg(m_imgid).arg(m_originalSerial).arg(DisplayColour::instance().revision())
        + (m_scopeMode == 4 ? QStringLiteral("&scope=false") : QString());
}

void EngineService::renderOriginal() {
    if (m_imgid < 0 || !m_ready) return;
    clearSnapshotComparison();
    requestOriginal();
}

void EngineService::requestOriginal() {
    if (m_imgid < 0 || !m_ready) return;
    m_originalRequested = true; ++m_originalGeneration;
    ++m_pending; setBusy(true);
    const double f = m_zoom > 0 ? m_zoom : 1.0;
    int w = qMax(1, int(m_viewW * f)), h = qMax(1, int(m_viewH * f));
    const int cap = 4096;
    if (qMax(w, h) > cap) { const double s = double(cap) / qMax(w, h); w = int(w * s); h = int(h * s); }
    QMetaObject::invokeMethod(m_worker, "renderOriginal", Qt::QueuedConnection, Q_ARG(int, m_imgid), Q_ARG(int, m_originalGeneration), Q_ARG(int, w), Q_ARG(int, h));
}

QString EngineService::snapshotSource() const {
    return m_snapshotId > 0 && !m_snapshotView.isNull()
        ? QStringLiteral("image://engine/_snapshot/%1?g=%2&display=%3").arg(m_snapshotId).arg(m_snapshotSerial).arg(DisplayColour::instance().revision())
              + (m_scopeMode == 4 ? QStringLiteral("&scope=false") : QString()) : QString();
}
void EngineService::compareSnapshot(int id, const QVariantList &values) {
    if (id <= 0 || values.isEmpty() || m_imgid < 0 || !m_ready) { clearSnapshotComparison(); return; }
    if (id == m_snapshotId) return;
    clearSnapshotComparison();
    m_snapshotId = id; m_snapshotValues = values;
    invalidateDetails();
    requestSnapshot();
}
void EngineService::requestSnapshot() {
    if (!m_snapshotId || m_imgid < 0) return;
    ++m_snapshotGeneration;
    m_snapshotStatus = tr("Loading snapshot detail…");
    emit snapshotChanged();
    ++m_pending; setBusy(true);
    QMetaObject::invokeMethod(m_worker, "renderSnapshot", Qt::QueuedConnection,
        Q_ARG(int, m_imgid), Q_ARG(int, m_snapshotGeneration.load()), Q_ARG(int, m_snapshotId), Q_ARG(QVariantList, m_snapshotValues));
}
void EngineService::clearSnapshotComparison() {
    if (!m_snapshotId) return;
    ++m_snapshotGeneration;
    m_snapshotId = 0; m_snapshotEngineId = -1; m_snapshotValues.clear();
    m_snapshotFullW = m_snapshotFullH = 0; m_snapshotStatus.clear();
    { QMutexLocker lock(&m_imageMutex); m_snapshotView = {}; }
    m_detailLayers[1].view = {}; m_detailLayers[1].requestedScale = 0;
    invalidateDetails();
    QMetaObject::invokeMethod(m_worker, "clearSnapshot", Qt::QueuedConnection);
    emit snapshotChanged();
    rebuildDetails();
}

void EngineService::setMaskShown(bool on) {
    if (on == m_maskShown) return;
    m_maskShown = on;
    emit maskChanged();
    rebuildMask();
}

// Native coverage includes range selection, shape composition, opacity,
// refinement and downstream geometry. Colour/overlay strength stay local.
void EngineService::rebuildMask() {
    if (!m_maskShown || m_activeLocal < 0) {
        m_maskCoverage = QImage();
        m_maskCoverageFor = -1;
        QMutexLocker l(&m_imageMutex);
        if (m_mask.isNull()) return;
        m_mask = QImage();
        l.unlock();
        ++m_maskSerial;
        emit maskSourceChanged();
        return;
    }
    if (m_maskCoverageFor != m_activeLocal || m_maskCoverageSerial != m_renderSerial) {
        if (m_maskCoveragePending || m_imgid < 0) return;
        const QImage render = imageCopy();
        if (render.isNull()) return;
        m_maskCoveragePending = true;
        ++m_pending; setBusy(true);
        QMetaObject::invokeMethod(m_worker, "renderMaskCoverage", Qt::QueuedConnection, Q_ARG(int, m_imgid),
                                  Q_ARG(int, m_activeLocal), Q_ARG(int, m_renderSerial), Q_ARG(int, render.width()), Q_ARG(int, render.height()));
        return;   // paintMask runs when it lands
    }
    paintMask();
}

void EngineService::paintMask() {
    const QImage render = imageCopy();
    if (render.isNull() || !m_maskShown || m_activeLocal < 0) return;
    QImage overlay;
    if (m_maskCoverageFor == m_activeLocal && !m_maskCoverage.isNull()) {
        overlay = QImage(m_maskCoverage.size(), QImage::Format_ARGB32_Premultiplied);
        const QColor colour(m_maskColour);
        for (int y = 0; y < overlay.height(); ++y) {
            const float *coverage = reinterpret_cast<const float *>(m_maskCoverage.constScanLine(y));
            QRgb *row = reinterpret_cast<QRgb *>(overlay.scanLine(y));
            for (int x = 0; x < overlay.width(); ++x) {
                const float a = qBound(0.f, coverage[4*x] * float(m_maskStrength), 1.f);
                row[x] = qRgba(int(colour.red()*a), int(colour.green()*a), int(colour.blue()*a), int(255*a));
            }
        }
    }
    { QMutexLocker l(&m_imageMutex); m_mask = overlay; }
    ++m_maskSerial;
    emit maskSourceChanged();
}

QString EngineService::maskSource() const {
    if (!m_maskShown || m_mask.isNull()) return QString();
    return QStringLiteral("image://mask/%1?g=%2").arg(m_imgid).arg(m_maskSerial);
}

void EngineService::setClippingShown(bool on) {
    if (on == m_clippingShown) return;
    m_clippingShown = on;
    rebuildClipping();
}

QString EngineService::clippingSource() const {
    if (!m_clippingShown || m_clipping.isNull()) return QString();
    return QStringLiteral("image://clipping/%1?g=%2").arg(m_imgid).arg(m_clippingSerial);
}

// Highlights: any channel at the top. Shadows: every channel at the
// bottom. The shares come from a small copy every render; the overlay
// itself is built at render size only while it is showing.
// What the meters read: while soft proofing, the proofed picture without the
// gamut paint (as the histogram and scopes do), otherwise the plain render.
QImage EngineService::meteredImage() const {
    QMutexLocker l(&m_imageMutex);
    if (proofing() && !m_measured.isNull()) return m_measured;
    return m_plain.isNull() ? m_image : m_plain;
}

void EngineService::rebuildClipping() {
    const QImage metered = meteredImage();
    if (metered.isNull()) { m_clipHi = 0; m_clipLo = 0; { QMutexLocker l(&m_imageMutex); m_clipping = QImage(); } emit clippingChanged(); return; }
    const QImage small = metered.convertToFormat(QImage::Format_RGB32).scaled(256, 256, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    long hi = 0, lo = 0;
    for (int y = 0; y < small.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(small.constScanLine(y));
        for (int x = 0; x < small.width(); ++x) {
            const int mx = qMax(qRed(line[x]), qMax(qGreen(line[x]), qBlue(line[x])));
            if (mx >= 254) ++hi;
            else if (mx <= 2) ++lo;
        }
    }
    const double n = double(small.width()) * small.height();
    m_clipHi = 100.0 * hi / n; m_clipLo = 100.0 * lo / n;
    QImage overlay;
    if (m_clippingShown) {
        const QImage src = metered.convertToFormat(QImage::Format_RGB32);
        overlay = QImage(src.size(), QImage::Format_ARGB32_Premultiplied);
        overlay.fill(Qt::transparent);
        const QRgb red = qPremultiply(qRgba(255, 40, 40, 220)), blue = qPremultiply(qRgba(40, 90, 255, 220));
        for (int y = 0; y < src.height(); ++y) {
            const QRgb *in = reinterpret_cast<const QRgb *>(src.constScanLine(y));
            QRgb *out = reinterpret_cast<QRgb *>(overlay.scanLine(y));
            for (int x = 0; x < src.width(); ++x) {
                const int mx = qMax(qRed(in[x]), qMax(qGreen(in[x]), qBlue(in[x])));
                if (mx >= 254) out[x] = red;
                else if (mx <= 2) out[x] = blue;
            }
        }
    }
    { QMutexLocker l(&m_imageMutex); m_clipping = overlay; }
    ++m_clippingSerial;
    emit clippingChanged();
}

QImage ClippingImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize) {
    Q_UNUSED(id) Q_UNUSED(requestedSize)
    const QImage img = m_svc->clippingCopy();
    if (size) *size = img.size();
    return img;
}

QImage MaskImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize) {
    Q_UNUSED(id) Q_UNUSED(requestedSize)
    const QImage img = m_svc->maskCopy();
    if (size) *size = img.size();
    return img;
}

QImage EngineOriginalProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize) {
    Q_UNUSED(requestedSize)
    const QImage img = id.contains(QLatin1String("scope=false")) ? EngineService::falseColourOf(m_svc->originalViewCopy()) : m_svc->originalViewCopy();
    if (size) *size = img.size();
    return id.contains(QLatin1String("scope=false")) ? img : DisplayColour::instance().convert(img);
}

QString EngineService::renderSource() const {
    if (m_imgid < 0 || m_image.isNull()) return QString();
    return QStringLiteral("image://engine/%1?g=%2&display=%3").arg(m_imgid).arg(m_renderSerial).arg(DisplayColour::instance().revision())
        + (m_scopeMode == 4 ? QStringLiteral("&scope=false") : QString());
}

QImage EngineImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize) {
    Q_UNUSED(id) Q_UNUSED(requestedSize)
    if(id.startsWith("_match/")) {
        const QImage img=static_cast<ImageMatchService *>(m_svc->imageMatch())->image(id.section('/',1,1));
        if(size) *size=img.size();
        return DisplayColour::instance().convert(ColourPipeline::instance().present(img));
    }
    if (id.startsWith("_denoise/")) {
        const auto img = static_cast<DenoiseService *>(m_svc->denoise())->previewCopy(id.startsWith("_denoise/after/"));
        if (size) *size = img.size();
        return DisplayColour::instance().convert(img);
    }
    if (id.startsWith("_ai/")) {
        const QImage img = static_cast<AiService *>(m_svc->ai())->previewCopy();
        if (size) *size = img.size();
        return DisplayColour::instance().convert(img);
    }
    if (id.startsWith("_snapshot/")) {
        const bool falseColour = id.contains(QLatin1String("scope=false"));
        const QImage img = falseColour ? EngineService::falseColourOf(m_svc->snapshotViewCopy()) : m_svc->snapshotViewCopy();
        if (size) *size = img.size();
        return falseColour ? img : DisplayColour::instance().convert(img);
    }
    if (id.startsWith("_scope/")) {
        const QImage img = m_svc->scopeCopy();
        if (size) *size = img.size();
        return img;
    }
    if (id.startsWith("_rawclip/")) {
        const QImage img = m_svc->rawClippingCopy();
        if (size) *size = img.size();
        return img;
    }
    if(id.startsWith("_retouch/")) {
        const QImage img=m_svc->retouchPreviewCopy();if(size)*size=img.size();return DisplayColour::instance().convert(img);
    }
    if (id.contains(QLatin1String("scope=false"))) { const QImage img = m_svc->falseColourView(); if (size) *size = img.size(); return img; }
    const QImage img = m_svc->viewCopy();
    if (size) *size = img.size();
    return DisplayColour::instance().convert(img);
}

void EngineService::setScopeMode(int mode) {
    mode = qBound(0, mode, 4);
    if (mode == m_scopeMode) return;
    const bool repaint = mode == 4 || m_scopeMode == 4;
    m_scopeMode = mode;
    rebuildScope();
    // False colour changes what the viewer's own sources deliver, and
    // whether the proof carries the gamut warning's paint.
    if (repaint && proofing() && m_proofGamut) pushProof();
    if (repaint) { emit renderedChanged(); emit detailChanged(); emit originalChanged(); emit snapshotChanged(); }
}
void EngineService::toggleFalseColour() {
    if (m_scopeMode == 4) { setScopeMode(m_scopeBefore); return; }
    m_scopeBefore = m_scopeMode;
    setScopeMode(4);
}
void EngineService::setScopeExpanded(bool expanded) {
    if (expanded == m_scopeExpanded) return;
    m_scopeExpanded = expanded;
    rebuildScope();
}
void EngineService::setRetouchPreviewScale(int scale) {
    scale=qBound(-1,scale,16);if(scale==m_retouchPreviewScale)return;
    m_retouchPreviewScale=scale;requestRetouchPreview();
}
QString EngineService::retouchPreviewSource() const {
    return m_retouchPreviewScale>=0 && !m_retouchPreviewImage.isNull()?QString("image://engine/_retouch/%1").arg(m_retouchPreviewSerial):QString();
}
void EngineService::requestRetouchPreview() {
    ++m_retouchPreviewSerial;
    {QMutexLocker l(&m_imageMutex);m_retouchPreviewImage=QImage();}
    if(m_retouchPreviewScale>=0 && m_imgid>=0 && !m_image.isNull()) {
        const QSize size=m_image.size().scaled(1200,1200,Qt::KeepAspectRatio);
        QMetaObject::invokeMethod(m_worker,"retouchPreview",Qt::QueuedConnection,Q_ARG(int,m_imgid),Q_ARG(int,m_generation.load()),Q_ARG(int,m_retouchPreviewSerial),Q_ARG(int,m_retouchPreviewScale),Q_ARG(int,size.width()),Q_ARG(int,size.height()));
    }
    emit retouchPreviewChanged();
}
void EngineService::setRawClippingShown(bool shown) {
    if (shown==m_rawClippingShown) return;
    m_rawClippingShown=shown;
    requestRawClipping();
}
QString EngineService::rawClippingSource() const {
    return m_rawClippingShown && !m_rawClippingImage.isNull() ? QString("image://engine/_rawclip/%1").arg(m_rawClippingSerial) : QString();
}
void EngineService::requestRawClipping() {
    ++m_rawClippingSerial;
    { QMutexLocker l(&m_imageMutex); m_rawClippingImage=QImage(); }
    m_rawClippingStatus.clear();
    if (m_rawClippingShown && m_imgid>=0 && !m_image.isNull()) {
        const QSize size=m_image.size().scaled(1000,1000,Qt::KeepAspectRatio);
        m_rawClippingStatus=tr("Reading sensor clipping…");
        QMetaObject::invokeMethod(m_worker,"rawClipping",Qt::QueuedConnection,Q_ARG(int,m_imgid),Q_ARG(int,m_generation.load()),
            Q_ARG(int,m_rawClippingSerial),Q_ARG(int,size.width()),Q_ARG(int,size.height()),Q_ARG(bool,m_cropMode));
    }
    emit rawClippingChanged();
}
QString EngineService::scopeSource() const {
    return m_scopeMode && !m_scopeImage.isNull() ? QString("image://engine/_scope/%1").arg(m_scopeSerial) : QString();
}
// False colour reads the finished view, including proof but excluding
// gamut-warning paint, at both overview and native detail sizes.
QImage EngineService::falseColourOf(const QImage &base) {
    if (base.isNull()) return base;
    return FalseColour::map(base.format() == QImage::Format_ARGB32 || base.format() == QImage::Format_RGB32 ? base : ColourPipeline::srgb8(base));
}
QImage EngineService::falseColourView() const { return falseColourOf(meteredImage()); }
QImage EngineService::falseColourDetail(const QString &key) const {
    const QImage tile = detailImage(key);
    return tile.isNull() ? tile : FalseColour::map(ColourPipeline::srgb8(tile));
}
void EngineService::rebuildScope() {
    if (m_scopeMode == 4) { const QImage base = meteredImage();
        m_falseColourBands = FalseColour::shares(base.format() == QImage::Format_ARGB32 || base.format() == QImage::Format_RGB32 ? base : ColourPipeline::srgb8(base)); }
    else m_falseColourBands.clear();
    const QImage scope = DevelopScopes::render(m_measured.isNull() ? m_image : m_measured, m_scopeMode, m_scopeExpanded);
    { QMutexLocker l(&m_imageMutex); m_scopeImage = scope; }
    ++m_scopeSerial;
    emit scopeChanged();
}

void EngineService::clearRetainedDetails() {
    QMutexLocker lock(&m_imageMutex);
    m_retainedDetailImages.clear();
    m_retainedDetailTiles[0].clear(); m_retainedDetailTiles[1].clear();
    m_detailCache.setMaxCost(m_detailBudgetKiB);
}
void EngineService::releaseRetainedDetailsIfReady() {
    if (m_retainedDetailImages.isEmpty() || m_busy || m_detailPending) return;
    bool complete = true;
    {
        QMutexLocker lock(&m_imageMutex);
        for (int index = 0; index < 2; ++index)
            for (const auto &rect : m_detailLayers[index].wanted)
                if (!m_detailCache.contains(detailKey(rect, index))) complete = false;
    }
    if (complete) {
        clearRetainedDetails();
        // Refill any outer tiles deferred while both generations shared the
        // budget. The replacement set is already complete and visible.
        QTimer::singleShot(0, this, &EngineService::rebuildDetails);
    }
}
void EngineService::retainVisibleDetails() {
    if (m_retainedDetailImages.isEmpty()) {
        const QVariantList visible[] = {plainDetailTilesFor(0), plainDetailTilesFor(1)};
        QMutexLocker lock(&m_imageMutex);
        int cost = 0;
        for (int index = 0; index < 2; ++index) for (const auto &tile : visible[index]) {
            const QString key = QUrl(tile.toMap().value(QStringLiteral("source")).toString()).path().mid(1);
            const QImage *image = m_detailCache.object(key);
            if (!image) continue;
            const int bytes = int((image->sizeInBytes() + 1023) / 1024);
            if (cost + bytes > m_detailBudgetKiB / 2) continue;
            m_retainedDetailImages.insert(key, *image);
            m_retainedDetailTiles[index] << tile;
            cost += bytes;
        }
        m_detailCache.setMaxCost(m_detailBudgetKiB - cost);
    }
}
void EngineService::invalidateDetails(bool preserve) {
    if (!preserve) clearRetainedDetails();
    else retainVisibleDetails();
    ++m_detailGeneration;
    m_pendingDetailKey.clear();
    m_detailLimited = false;
#ifdef OMARAW_ENGINE
    oma_engine_cancel_preview();
#endif
    for (auto &layer : m_detailLayers) layer.wanted.clear();
    { QMutexLocker lock(&m_imageMutex); m_detailCache.clear(); }
    emit detailChanged();
}

void EngineService::setDetailView(double x, double y, double width, double height, double scale) {
    setDetailLayer(0, x, y, width, height, scale);
}
void EngineService::setViewerActive(QObject *viewer, bool active) {
    if (!viewer) return;
    if (active) { m_activeViewer = viewer; return; }
    // Develop and Output share this service. A hidden viewer must not erase
    // the visible viewer's requests when their visibility signals cross.
    if (m_activeViewer != viewer) return;
    m_activeViewer.clear();
    setDetailView(0, 0, 0, 0, 0);
    setOriginalDetailView(0, 0, 0, 0, 0);
}
void EngineService::setOriginalDetailView(double x, double y, double width, double height, double scale) {
    setDetailLayer(1, x, y, width, height, scale);
}
void EngineService::setDetailLayer(int index, double x, double y, double width, double height, double scale) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) || !std::isfinite(height) || !std::isfinite(scale)) return;
    auto &layer = m_detailLayers[index];
    const QRectF view = QRectF(x, y, qMax(0.0, width), qMax(0.0, height)).intersected(QRectF(0, 0, 1, 1));
    scale = view.isEmpty() ? 0 : qBound(0.0, scale, 1.0);
    if (layer.view == view && qFuzzyCompare(layer.requestedScale, scale)) return;
    // Zoom/pan changes the viewport, not the photo. Keep its last complete
    // detail at the same image coordinates until the new visible set lands.
    // It shares the existing budget with the replacement, just like edits.
    if (scale > 0) retainVisibleDetails();
    else clearRetainedDetails();
    layer.view = view; layer.requestedScale = scale;
    rebuildDetails();
}
QString EngineService::detailKey(const QRect &rect, int index) const {
    return QStringLiteral("%1:%2:%3:%4:%5:%6:%7:%8").arg(index).arg(m_generation.load())
        .arg(index ? m_snapshotGeneration.load() : 0)
        .arg(qRound(m_detailLayers[index].scale * 1000000)).arg(rect.x()).arg(rect.y()).arg(rect.width()).arg(rect.height());
}
void EngineService::rebuildDetails() {
    m_detailLimited = false;
    for (int index = 0; index < 2; ++index) {
        auto &layer = m_detailLayers[index];
        layer.wanted.clear();
        layer.width = index ? (m_snapshotId ? m_snapshotFullW : m_originalFullW) : m_fullW;
        layer.height = index ? (m_snapshotId ? m_snapshotFullH : m_originalFullH) : m_fullH;
        if (m_imgid < 0 || layer.width <= 0 || layer.height <= 0 || layer.view.isEmpty() || layer.requestedScale <= 0 || (index && m_cropMode)) continue;
        layer.scale = qMin(1.0, std::pow(2.0, std::ceil(std::log2(layer.requestedScale))));
        const int width = qMax(1, int(std::floor(layer.width * layer.scale)));
        const int height = qMax(1, int(std::floor(layer.height * layer.scale)));
        const QRect view = QRectF(layer.view.x() * width, layer.view.y() * height, layer.view.width() * width, layer.view.height() * height)
            .toAlignedRect().intersected(QRect(0, 0, width, height));
        for (int y = view.top() / 512 * 512; y <= view.bottom(); y += 512)
            for (int x = view.left() / 512 * 512; x <= view.right(); x += 512)
                layer.wanted << QRect(x, y, qMin(512, width - x), qMin(512, height - y));
        const QPoint centre = view.center();
        std::sort(layer.wanted.begin(), layer.wanted.end(), [centre](const QRect &a, const QRect &b) {
            return (a.center() - centre).manhattanLength() < (b.center() - centre).manhattanLength();
        });
        if (layer.wanted.size() > 120) { layer.wanted.resize(120); m_detailLimited = true; }
    }
    // Both float layers share one cache. A working set larger than that
    // cache endlessly re-renders tiles evicted by the next tile. Keep the
    // nearest native tiles from each view, fairly, within the actual budget.
    // The existing overview remains underneath the unselected outer area.
    QList<QRect> selected[2];
    {
        QMutexLocker lock(&m_imageMutex);
        qint64 remaining = m_detailCache.maxCost();
        const int count = qMax(m_detailLayers[0].wanted.size(), m_detailLayers[1].wanted.size());
        for (int i = 0; i < count; ++i) for (int index = 0; index < 2; ++index) {
            const auto &wanted = m_detailLayers[index].wanted;
            if (i >= wanted.size()) continue;
            const QRect rect = wanted[i];
            const qint64 cost = (qint64(rect.width()) * rect.height() * 16 + 1023) / 1024;
            if (cost > remaining) { m_detailLimited = true; continue; }
            selected[index] << rect; remaining -= cost;
        }
        for (int index = 0; index < 2; ++index) {
            m_detailLayers[index].wanted = std::move(selected[index]);
            // Protect retained visible tiles from older off-screen entries
            // while the newly exposed parts of a pan are being rendered.
            for (const auto &rect : m_detailLayers[index].wanted) m_detailCache.object(detailKey(rect, index));
        }
    }
    // Panning within a tile or revealing the comparison pane can leave the
    // in-flight tile useful. Keep it, including its queued generation,
    // instead of cancelling the pixelpipe and throwing away its cache.
    if (m_detailPending && !m_pendingDetailKey.isEmpty()) {
        bool wanted = false;
        for (int index = 0; index < 2; ++index)
            for (const auto &rect : m_detailLayers[index].wanted)
                if (detailKey(rect, index) == m_pendingDetailKey) wanted = true;
        if (!wanted) {
            ++m_detailGeneration;
            m_pendingDetailKey.clear();
#ifdef OMARAW_ENGINE
            oma_engine_cancel_preview();
#endif
        }
    }
    releaseRetainedDetailsIfReady();
    emit detailChanged();
    renderNextDetail();
}
void EngineService::renderNextDetail() {
    if (previewEditing() || autoRunning()) return;
    if (!m_ready || m_imgid < 0 || m_busy || m_exporting || m_syncPending || m_fileOperations || m_detailPending) return;
    // Alternate corresponding current/before tiles so either side appears
    // progressively, even when both views request many native tiles.
    const int count = qMax(m_detailLayers[0].wanted.size(), m_detailLayers[1].wanted.size());
    for (int i = 0; i < count; ++i) for (int index = 0; index < 2; ++index) {
        const auto &layer = m_detailLayers[index];
        if (i >= layer.wanted.size()) continue;
        const QRect rect = layer.wanted[i];
        const QString key = detailKey(rect, index);
        // Everything this layer still lacks: while a mask is feathered the
        // engine renders that block once and cuts the tiles from it.
        QRect block;
        {
            QMutexLocker lock(&m_imageMutex);
            if (m_detailCache.contains(key)) continue;
            for (const QRect &other : layer.wanted) if (!m_detailCache.contains(detailKey(other, index))) block |= other;
        }
        m_detailPending = true;
        m_pendingDetailKey = key;
        emit detailChanged();
        const bool snapshot = index == 1 && m_snapshotId > 0;
        QMetaObject::invokeMethod(m_worker, "renderDetail", Qt::QueuedConnection, Q_ARG(int, snapshot ? m_snapshotEngineId : m_imgid),
            Q_ARG(int, m_detailGeneration.load()), Q_ARG(QString, key), Q_ARG(QRect, rect), Q_ARG(QRect, block), Q_ARG(double, layer.scale),
            Q_ARG(int, snapshot ? 2 : index), Q_ARG(bool, m_cropMode && index == 0));
        return;
    }
}
QVariantList EngineService::detailTiles() const { return detailTilesFor(0); }
QVariantList EngineService::originalDetailTiles() const { return detailTilesFor(1); }
// The tiles as the viewer asks for them: in false colour the current render's
// tiles are requested under another address, so they are fetched again.
QVariantList EngineService::detailTilesFor(int index) const {
    QVariantList tiles = plainDetailTilesFor(index);
    // Both sides of a comparison: the before tiles are false-coloured too.
    if (m_scopeMode == 4) for (QVariant &tile : tiles) { QVariantMap m = tile.toMap(); m[QStringLiteral("source")] = m.value(QStringLiteral("source")).toString() + QStringLiteral("&scope=false"); tile = m; }
    return tiles;
}
QVariantList EngineService::plainDetailTilesFor(int index) const {
    if (index == 0 && previewEditing()) return {};
    if (!m_retainedDetailTiles[index].isEmpty()) return m_retainedDetailTiles[index];
    QVariantList tiles;
    const auto &layer = m_detailLayers[index];
    if (layer.width <= 0 || layer.height <= 0) return tiles;
    QMutexLocker lock(&m_imageMutex);
    // A fitted preview and a native tile differ in sharpness (and may use
    // different RAW sampling). Publishing one tile at a time made the
    // photo appear to crawl. Reveal the requested detail together.
    for (const QRect &rect : layer.wanted)
        if (!m_detailCache.contains(detailKey(rect, index))) return {};
    for (const QRect &rect : layer.wanted) {
        const QString key = detailKey(rect, index);
        if (!m_detailCache.contains(key)) continue;
        const double width = layer.width * layer.scale, height = layer.height * layer.scale;
        tiles << QVariantMap{{QStringLiteral("source"), QStringLiteral("image://engine-detail/") + key + QStringLiteral("?display=") + QString::number(DisplayColour::instance().revision())},
            {QStringLiteral("x"), rect.x() / width}, {QStringLiteral("y"), rect.y() / height},
            {QStringLiteral("width"), rect.width() / width}, {QStringLiteral("height"), rect.height() / height}};
    }
    return tiles;
}

QImage EngineService::detailImage(const QString &key) const {
    QMutexLocker lock(&m_imageMutex);
    const QImage *image = m_detailCache.object(key);
    return image ? *image : m_retainedDetailImages.value(key);
}
bool EngineService::detailBusy() const {
    if (m_detailPending) return true;
    QMutexLocker lock(&m_imageMutex);
    for (int index = 0; index < 2; ++index)
        for (const QRect &rect : m_detailLayers[index].wanted) if (!m_detailCache.contains(detailKey(rect, index))) return true;
    return false;
}
QImage EngineDetailProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize) {
    Q_UNUSED(requestedSize)
    if (id.contains(QLatin1String("scope=false"))) { const QImage image = m_service->falseColourDetail(id.section(QLatin1Char('?'), 0, 0)); if (size) *size = image.size(); return image; }
    const QImage image = m_service->detailImage(id.section(QLatin1Char('?'), 0, 0));
    if (size) *size = image.size();
    return DisplayColour::instance().convert(image);
}

void EngineService::setMemoryBudget(int mib) {
    const auto budget = ResourceBudget::forMemory(mib);
    m_detailBudgetKiB = budget.detail * 1024;
    // Settings and pixels stay intact; discard only derived native tiles.
    invalidateDetails();
    QMetaObject::invokeMethod(m_worker, "setMemoryBudget", Qt::QueuedConnection, Q_ARG(int, mib));
    rebuildDetails();
}

void EngineService::loadSettings() {
    // The environment wins, so a measurement run can select the setting
    // without writing to the user's preferences.
    const QByteArray forced = qgetenv("OMARAW_REDUCED_PREVIEWS");
    m_reducedPreviews.store(forced.isEmpty()
        ? QSettings().value(QStringLiteral("develop/reducedPreviews"), true).toBool()
        : forced == "1");
    emit reducedPreviewsChanged();
    const QByteArray full = qgetenv("OMARAW_EXPORT_FULL_RESOLUTION");
    m_exportFullResolution.store(full.isEmpty()
        ? QSettings().value(QStringLiteral("output/fullResolution"), false).toBool()
        : full == "1");
    emit exportFullResolutionChanged();
}

void EngineService::setExportFullResolution(bool on) {
    if (on == m_exportFullResolution.load()) return;
    m_exportFullResolution.store(on);
    QSettings().setValue(QStringLiteral("output/fullResolution"), on);
    emit exportFullResolutionChanged();
}

void EngineService::setReducedPreviews(bool on) {
    if (on == m_reducedPreviews.load()) return;
    m_reducedPreviews.store(on);
    QSettings().setValue(QStringLiteral("develop/reducedPreviews"), on);
    emit reducedPreviewsChanged();
    // The overview on screen was produced under the old setting.
    if (m_imgid >= 0) requestRender();
}

void EngineService::setViewSize(int w, int h) {
    w = qMax(64, w); h = qMax(64, h);
    if (w == m_viewW && h == m_viewH) return;
    m_viewW = w; m_viewH = h;
    // Changing toolbar height or window size must not invalidate an analysis
    // in flight. Its completion renders using these updated bounds.
    if (m_geometryBusy) return;
    // Match the overview to the fitted viewport, independent of zoom.
    // Compare both fitted dimensions so portrait images do not trigger a
    // render simply because they cannot fill a landscape viewport's width.
    QSize bounds(w, h);
    if (qMax(w, h) > 4096) bounds.scale(4096, 4096, Qt::KeepAspectRatio);
    QSize fitted(m_fullW, m_fullH);
    if (fitted.width() > bounds.width() || fitted.height() > bounds.height())
        fitted.scale(bounds, Qt::KeepAspectRatio);
    if (m_imgid >= 0 && (m_image.isNull() || m_image.width() < fitted.width() * 0.9 || m_image.height() < fitted.height() * 0.9))
        requestRender();
}

void EngineService::setZoom(double z) {
    if (!std::isfinite(z)) return;
    // Zero means Fit. Positive scales below Fit are needed for native
    // pixel viewing of small photos and on displays with dense pixels.
    z = qBound(0.0, z, 256.0);
    if (qFuzzyCompare(z, m_zoom)) return;
    m_zoom = z;
    emit zoomChanged();
    // The viewer requests native tiles for its new viewport. Zoom changes
    // no photo settings and does not need another whole-image render.
    if (m_image.isNull() && !m_busy) requestRender();
}

void EngineService::invalidatePreview(bool preserveDetails) {
    invalidateDetails(preserveDetails);
    ++m_generation;
    emit editRevisionChanged();
#ifdef OMARAW_ENGINE
    oma_engine_cancel_preview();
#endif
}

void EngineService::requestRender(bool preserveDetails) {
    if (m_imgid < 0 || !m_ready) return;
    invalidatePreview(preserveDetails);
    const int gen = m_generation.load();
    ++m_pending; setBusy(true);
    // Keep a viewport-sized base while visible native tiles provide sharp
    // pixels at higher zoom. A 100% edit must not first render the entire
    // photo at 4096 pixels just to cover the area outside the viewport.
    int w = m_viewW, h = m_viewH;
    // Keep the moving picture inexpensive on large displays. Release restores
    // the normal fitted resolution, followed by native tiles. Exports never
    // use this path, and the full-quality preview preference is respected.
    const bool interactive = previewEditing() && m_reducedPreviews.load();
    const int cap = interactive ? 1024 : 4096;
    if (interactive && qMax(w, h) > cap) m_interactivePreviewSized = true;
    if (qMax(w, h) > cap) { const double s = double(cap) / qMax(w, h); w = int(w * s); h = int(h * s); }
    QMetaObject::invokeMethod(m_worker, "render", Qt::QueuedConnection,
                              Q_ARG(int, m_imgid), Q_ARG(int, w), Q_ARG(int, h), Q_ARG(int, gen), Q_ARG(bool, m_cropMode));
}

void EngineService::whiteBalanceAsShot() {
    editBoundary();
    if (m_imgid < 0) return;
    m_dirty = true;
    QMetaObject::invokeMethod(m_worker, "wbAsShot", Qt::QueuedConnection, Q_ARG(int, m_imgid));
    refresh();
    requestRender();
}

void EngineService::pickWhiteBalance(double fx, double fy) {
    editBoundary();
    if (m_imgid < 0) return;
    m_dirty = true;
    ++m_pending; setBusy(true);
    QMetaObject::invokeMethod(m_worker, "wbFromPatch", Qt::QueuedConnection, Q_ARG(int, m_imgid), Q_ARG(double, fx), Q_ARG(double, fy));
    QMetaObject::invokeMethod(m_worker, [this] {
        QMetaObject::invokeMethod(this, [this] {
            --m_pending;
            // Queue the updated preview before publishing idle readiness.
            refresh(); requestRender(); setBusy(m_pending > 0);
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void EngineService::autoExposure() {
    if (m_imgid < 0 || autoRunning()) return;
    emit autoExposureRequested();
    m_auto.requested = true;
    m_auto.imgid = m_imgid;
    if (m_failedGeneration == m_generation.load()) requestRender();
    continueAuto();
}

void EngineService::cancelAuto() {
    ++m_autoSerial;
    m_auto = AutoRun();
}

void EngineService::continueAuto() {
    // Read only once the last edit and parameter read-back agree. A serial
    // and generation prevent a late meter from overwriting a newer edit.
    if (!m_auto.requested || m_renderedGeneration != m_generation.load()
        || m_receivedParams != m_requestedParams) return;
    if (m_auto.imgid != m_imgid || m_image.isNull()) { cancelAuto(); return; }
    m_auto = AutoRun();
    startAutoExposure();
}

void EngineService::startAutoExposure() {
    if (m_lastAuto.path == m_path && m_lastAuto.variant == m_variant && m_lastAuto.end == m_historyEnd
        && m_lastAuto.sourceStamp == m_sourceStamp) {
        setStatus(tr("Exposure is already set")); return;
    }
    double current = 0;
    bool exposureOn = false, manual = true;
    for (const QVariant &v : m_params) {
        const QVariantMap m = v.toMap();
        if (m.value("op") != QLatin1String("exposure")) continue;
        if (m.value("field") == QLatin1String("exposure") && m.value("enabled").toBool()) {
            current = m.value("value").toDouble(); exposureOn = true;
        }
        if (m.value("field") == QLatin1String("mode")) manual = m.value("value").toInt() == 0;
    }
    m_auto.imgid = m_imgid; m_auto.from = current; m_auto.waiting = true;
    m_auto.needsExposure = !exposureOn || !manual;
    m_auto.restored = m_toolState.value(QStringLiteral("sceneReferred")).toBool()
        && !moduleEnabled("sigmoid") && !moduleEnabled("filmicrgb") && !m_basecurveOn && !moduleEnabled("omarawprint");
    ++m_pending; setBusy(true);
    setStatus(tr("Calculating exposure…"));
    QMetaObject::invokeMethod(m_worker, "exposureMeter", Qt::QueuedConnection,
        Q_ARG(int, m_imgid), Q_ARG(int, m_generation.load()), Q_ARG(int, ++m_autoSerial));
}

void EngineService::straightenAlong(double x0, double y0, double x1, double y1) {
    if (m_imgid < 0 || m_image.isNull() || !std::isfinite(x0) || !std::isfinite(y0)
        || !std::isfinite(x1) || !std::isfinite(y1)) return;
    const double dx = (x1 - x0) * m_image.width(), dy = (y1 - y0) * m_image.height();
    if (std::hypot(dx, dy) < 4) return;
    double a = std::atan2(dy, dx) * 180.0 / M_PI;      // screen angle, y down
    while (a > 45) a -= 90;                              // a vertical line is a horizontal one turned
    while (a < -45) a += 90;
    // Orientation runs after rotation. Each mirror (including transpose)
    // reverses its handedness, so take the screen angle back to source space.
    const int orientation = m_crop.value(QStringLiteral("orientation")).toInt();
    if (bool(orientation & 1) ^ bool(orientation & 2) ^ bool(orientation & 4)) a = -a;
    editBoundary();
    double current = 0;
    // Only a rotation that is on is in the picture (an undo can leave one listed but off).
    for (const QVariant &v : m_params) { const QVariantMap m = v.toMap(); if (m.value(QStringLiteral("op")) == QLatin1String("ashift") && m.value(QStringLiteral("field")) == QLatin1String("rotation") && m.value(QStringLiteral("enabled")).toBool()) current = m.value(QStringLiteral("value")).toDouble(); }
    // The module's positive rotation turns the un-oriented picture counter-clockwise.
    setParam(QStringLiteral("ashift"), QStringLiteral("rotation"), qBound(-45.0, current + a, 45.0));
}

void EngineService::setCropMode(bool on) {
    if (on == m_cropMode) return;
    m_cropMode = on;
    emit cropModeChanged();
    requestRender();
}

void EngineService::correctGeometry(int operation, int cropPolicy, const QVariantList &guides) {
    if (m_imgid < 0 || !m_ready || m_busy || m_geometryBusy || operation < 0 || operation > 6 || guides.size() > 4) return;
    for (const auto &entry : guides) {
        const auto line = entry.toMap();
        for (const char *field : {"x0", "y0", "x1", "y1"}) {
            bool valid = false;
            const double v = line.value(QLatin1String(field)).toDouble(&valid);
            if (!valid || !std::isfinite(v) || v < 0 || v > 1) return;
        }
    }
    if (cropPolicy == -1) cropPolicy = m_crop.value(QStringLiteral("constrain"), 1).toInt();
    if (cropPolicy < 0 || cropPolicy > 2) return;
    cancelAuto();
    m_lastAuto = {};
    invalidatePreview();
    m_geometryBusy = true;
    m_geometryStatus = tr("Calculating geometry…");
    emit geometryChanged();
    ++m_pending; setBusy(true);
    QMetaObject::invokeMethod(m_worker, "geometry", Qt::QueuedConnection, Q_ARG(int, m_imgid),
        Q_ARG(int, m_generation.load()), Q_ARG(int, operation), Q_ARG(int, cropPolicy), Q_ARG(QVariantList, guides));
}

void EngineService::setCrop(double cx, double cy, double cw, double ch, int ratioN, int ratioD, bool separateEdit) {
    cancelAuto();
    m_lastAuto = {};
    if (!separateEdit) editBoundary(QStringLiteral("crop"));
    if (m_imgid < 0) return;
    m_dirty = true;
    m_crop[QStringLiteral("cx")] = cx; m_crop[QStringLiteral("cy")] = cy; m_crop[QStringLiteral("cw")] = cw; m_crop[QStringLiteral("ch")] = ch;
    m_crop[QStringLiteral("ratioN")] = ratioN; m_crop[QStringLiteral("ratioD")] = ratioD;
    m_crop[QStringLiteral("enabled")] = !(cx <= 0 && cy <= 0 && cw >= 1 && ch >= 1);
    emit cropChanged();
    QMetaObject::invokeMethod(m_worker, "setCrop", Qt::QueuedConnection, Q_ARG(int, m_imgid),
                              Q_ARG(double, cx), Q_ARG(double, cy), Q_ARG(double, cw), Q_ARG(double, ch), Q_ARG(int, ratioN), Q_ARG(int, ratioD), Q_ARG(bool, separateEdit));
    refresh();
    // With the tool open the render shows the whole frame, so the crop itself changes nothing on screen.
    if (!m_cropMode) requestRender();
}

void EngineService::refreshCameraProfiles() {
    CameraProfiles::refresh();
    if (m_ready && m_imgid >= 0) QMetaObject::invokeMethod(m_worker, "readCameraProfiles", Qt::QueuedConnection, Q_ARG(int, m_imgid));
}

bool EngineService::importCameraProfile(const QString &source) {
    const QUrl url(source);
    QString error;
    const QString path = CameraProfiles::importFile(url.isLocalFile() ? url.toLocalFile() : source, &error);
    if (path.isEmpty()) { setStatus(tr("Camera profile not imported: %1").arg(error)); return false; }
    refreshCameraProfiles();
    QFile file(path); CameraProfiles::Dcp profile;
    if (file.open(QIODevice::ReadOnly) && CameraProfiles::parse(file.readAll(), profile, error)) {
        if (m_cameraProfileState.value("raw").toBool() && CameraProfiles::matchesCamera(profile.camera, m_cameraProfileState.value("camera").toString(), m_cameraProfileState.value("exifCamera").toString()))
            return selectCameraProfile(path);
        setStatus(tr("Imported %1 for %2. It will be available on RAW photos from that camera.").arg(profile.name, profile.camera));
    }
    return true;
}

bool EngineService::selectCameraProfile(const QString &key) {
    if (!m_ready || m_imgid < 0) return false;
    if (key.isEmpty()) {
        // Restore the parked tone mapper; keep the previous table for Undo.
        if (moduleEnabled("omarawprint")) setModuleEnabled("omarawprint", false);
        return true;
    }
    if (!m_cameraProfileState.value("raw").toBool()) { setStatus(tr("Camera profiles require a RAW photo.")); return false; }
    const QUrl url(key); const QString path = url.isLocalFile() ? url.toLocalFile() : key;
    // Validation and the expensive bake happen on the engine worker before
    // any edit is made. Rapid photo changes retain the requested image ID.
    applyValues({QVariantMap{{"op", "omarawprint"}, {"field", "cameraprofile"}, {"name", QFileInfo(path).completeBaseName()}, {"source", path}}});
    return true;
}

bool EngineService::applyCameraLook() {
    if (!m_ready || m_imgid < 0 || busy() || !m_cameraProfileState.value("raw").toBool()) return false;
    const auto values = m_cameraProfileState.value("cameraLook").toMap().value("values").toList();
    if (values.isEmpty()) { setStatus(tr("No included camera look is available for this camera.")); return false; }
    applyPresetValues(values);
    setStatus(tr("Camera look applied. Exposure, white balance and local edits were kept. Undo restores the previous look."));
    return true;
}

void EngineService::refresh() {
    if (m_imgid < 0) return;
    // The active controls already show their requested values. Reading every
    // module, menu option, mask and history row between drag samples delays
    // the next visible frame. Read the complete state once on release.
    if (previewEditing()) { m_previewParamsDeferred = true; return; }
    m_previewParamsDeferred = false;
    ++m_requestedParams;
    QMetaObject::invokeMethod(m_worker, "readParams", Qt::QueuedConnection,
                              Q_ARG(int, m_imgid), Q_ARG(QVariantList, curatedParams()));
}

bool EngineService::moduleEnabled(const QString &op) const {
    if (op == QLatin1String("basecurve")) return m_basecurveOn;
    for (const QVariant &v : m_params) { const QVariantMap m = v.toMap(); if (m.value(QStringLiteral("op")) == op) return m.value(QStringLiteral("enabled")).toBool(); }
    return false;
}

// The print is the rendering. Choosing a stock parks whichever tone mapper
// is on, in the same history step, and turning the print off brings it
// back; with nothing remembered, the default tone mapper returns.
bool EngineService::printHandOff(const QString &op, const QString &field, const QVariant &value, bool on) {
    if (op != QLatin1String("omarawprint")) return false;
    const bool wasOn = moduleEnabled(op);
    QVariantList values;
    if (on && !wasOn) {
        QVariantMap row{{QStringLiteral("op"), op}, {QStringLiteral("enabled"), true}};
        if (!field.isEmpty()) { row[QStringLiteral("field")] = field; row[QStringLiteral("value")] = value; }
        values << row;
        m_printHeld.clear();
        for (const char *tm : {"sigmoid", "filmicrgb", "basecurve"})
            if (moduleEnabled(QLatin1String(tm))) { m_printHeld << QLatin1String(tm); values << QVariantMap{{QStringLiteral("op"), QLatin1String(tm)}, {QStringLiteral("enabled"), false}}; }
    } else if (!on && wasOn) {
        values << QVariantMap{{QStringLiteral("op"), op}, {QStringLiteral("enabled"), false}};
        const int baseline = m_toolState.value("printBaseTone", 0).toInt();
        const QStringList tones{"sigmoid", "filmicrgb", "basecurve"};
        for (int i = 0; i < tones.size(); ++i)
            if (baseline & (1 << i)) values << QVariantMap{{"op", tones[i]}, {"enabled", true}};
        m_printHeld.clear();
    } else return false;
    applyValues(values);
    return true;
}

// While a print stock is on, the tone mapper it replaced is parked: its
// sliders still take values (for when the print goes), but it must not come
// back on underneath the print, which would tone-map the picture twice.
bool EngineService::toneMapperParked(const QString &op) const {
    return (op == QLatin1String("sigmoid") || op == QLatin1String("filmicrgb") || op == QLatin1String("basecurve"))
           && moduleEnabled(QStringLiteral("omarawprint"));
}

// Tone (sigmoid) and Film tone (filmic) are alternatives: moving one's
// slider takes the other off, so the picture is never tone-mapped twice.
QString EngineService::rivalToneMapper(const QString &op) const {
    const QString rival = op == QLatin1String("sigmoid") ? QStringLiteral("filmicrgb")
                        : op == QLatin1String("filmicrgb") ? QStringLiteral("sigmoid") : QString();
    return !rival.isEmpty() && moduleEnabled(rival) ? rival : QString();
}

void EngineService::setParam(const QString &op, const QString &field, double value) {
    if (m_imgid < 0) return;
    if (op == QLatin1String("ashift") && !m_geometryStatus.isEmpty()) {
        m_geometryStatus.clear();
        emit geometryChanged();
    }
    editBoundary(op + QLatin1Char('.') + field);
    // Native bloom still glows at strength 0. The UI's zero means bypass,
    // retaining its recipe so the effect switch can restore it later.
    if (op == QLatin1String("bloom") && field == QLatin1String("strength") && value <= 0) {
        if (moduleEnabled(op)) setModuleEnabled(op, false);
        return;
    }
    if (op == QLatin1String("shadhi") && !moduleEnabled(op)) {
        const auto rows = paramsFor(op);
        if (!rows.isEmpty() && !rows.first().toMap().value("configured").toBool()) {
            const QString sibling = offSibling(op, field);
            if (!sibling.isEmpty()) {
                applyValues({QVariantMap{{"op", op}, {"field", sibling}, {"value", 0.0}},
                             QVariantMap{{"op", op}, {"field", field}, {"value", value}}});
                return;
            }
        }
    }
    if (printHandOff(op, field, value, true)) return;
    const bool parked = toneMapperParked(op);
    if (!parked) {
        const QString rival = rivalToneMapper(op);
        if (!rival.isEmpty()) {
            applyValues({QVariantMap{{QStringLiteral("op"), op}, {QStringLiteral("field"), field}, {QStringLiteral("value"), value}},
                         QVariantMap{{QStringLiteral("op"), rival}, {QStringLiteral("enabled"), false}}});
            return;
        }
    }
    // Optimistic local update so the slider does not snap back while the
    // engine catches up.
    for (int i = 0; i < m_params.size(); ++i) {
        QVariantMap m = m_params[i].toMap();
        if (m.value(QStringLiteral("op")) == op && m.value(QStringLiteral("field")) == field) {
            m[QStringLiteral("value")] = value; m[QStringLiteral("enabled")] = !parked; m_params[i] = m;
        }
    }
    // Bumped without telling on purpose: this runs on every step of a slider
    // drag, and the engine's read-back (moments later) bumps and tells. The
    // bump alone keeps paramsFor() from answering from its stale cache.
    ++m_paramsVersion;
    m_dirty = true;
    QMetaObject::invokeMethod(m_worker, "setParam", Qt::QueuedConnection,
                              Q_ARG(int, m_imgid), Q_ARG(QString, op), Q_ARG(QString, field), Q_ARG(double, value));
    if (parked) QMetaObject::invokeMethod(m_worker, "setEnabled", Qt::QueuedConnection, Q_ARG(int, m_imgid), Q_ARG(QString, op), Q_ARG(bool, false));
    refresh();
    requestRender(op != QLatin1String("ashift") && op != QLatin1String("lens")
                  && op != QLatin1String("crop") && op != QLatin1String("flip") && op != QLatin1String("clipping"));
}

void EngineService::setParamString(const QString &op, const QString &field, const QString &value) {
    editBoundary();
    if (m_imgid < 0) return;
    for (int i = 0; i < m_params.size(); ++i) {
        QVariantMap m = m_params[i].toMap();
        if (m.value(QStringLiteral("op")) == op && m.value(QStringLiteral("field")) == field) { m[QStringLiteral("text")] = value; m[QStringLiteral("enabled")] = true; m_params[i] = m; }
    }
    paramsTouched();
    m_dirty = true;
    QMetaObject::invokeMethod(m_worker, "setParamString", Qt::QueuedConnection, Q_ARG(int, m_imgid), Q_ARG(QString, op), Q_ARG(QString, field), Q_ARG(QString, value));
    refresh();
    requestRender();
}

void EngineService::setPrimaryGradeEditing(QObject *owner, bool active) {
    setPreviewEditing(owner, active);
}

void EngineService::setPreviewEditing(QObject *owner, bool active) {
    if (!owner || (active && m_imgid < 0)) return;
    if (active) {
        if (m_previewEditors.contains(owner)) return;
        const bool alreadyEditing = previewEditing();
        m_previewEditors.insert(owner, connect(owner, &QObject::destroyed, this,
            [this, owner] { setPreviewEditing(owner, false); }));
        if (alreadyEditing) return;
        ColourPipeline::instance().setPrimaryGradeInteractive(this, true);
        // Let an already-running detail finish, but do not start another.
        // Hide old current-image tiles so the live overview remains visible.
        emit detailChanged();
    } else if (m_previewEditors.contains(owner)) {
        disconnect(m_previewEditors.take(owner));
        if (previewEditing()) return;
        ColourPipeline::instance().setPrimaryGradeInteractive(this, false);
        clearRetainedDetails();
        const bool finalRender = m_interactivePreviewSized || m_previewParamsDeferred;
        if (m_previewParamsDeferred) refresh();
        if (finalRender) {
            m_interactivePreviewSized = false;
            requestRender();
        }
        rebuildDetails();
    }
}

void EngineService::setPrimaryGrade(const QString &zone, double hue, double chroma,
                                     double luminance, bool separateEdit) {
    if (m_imgid < 0 || !std::isfinite(hue) || !std::isfinite(chroma) || !std::isfinite(luminance)
        || (zone != "lift" && zone != "gamma" && zone != "gain" && zone != "offset")) return;
    cancelAuto();
    m_lastAuto = {};
    hue = std::fmod(std::fmod(hue, 360.0) + 360.0, 360.0);
    chroma = std::clamp(chroma, 0.0, zone == "lift" || zone == "offset" ? 0.25 : 1.0);
    const double lo = zone == "gamma" ? .25 : zone == "gain" ? 0.0 : zone == "lift" ? -.5 : -1.0;
    const double hi = zone == "gamma" || zone == "gain" ? 4.0 : zone == "lift" ? .5 : 1.0;
    luminance = std::clamp(luminance, lo, hi);
    // Keep the controls responsive while the worker catches up. Read-back is
    // ignored by the active gesture, so it cannot move the handle being held.
    for (auto &v : m_params) {
        auto p = v.toMap();
        if (p.value("op") != "omarawgrade") continue;
        const QString field = p.value("field").toString();
        if (field == zone + "_H") p["value"] = hue;
        if (field == zone + "_C") p["value"] = chroma;
        if (field == zone + "_Y") p["value"] = luminance;
        p["enabled"] = true;
        v = p;
    }
    paramsTouched();
    m_dirty = true;
    QMetaObject::invokeMethod(m_worker, "setPrimaryGrade", Qt::QueuedConnection,
        Q_ARG(int, m_imgid), Q_ARG(QString, zone), Q_ARG(double, hue), Q_ARG(double, chroma),
        Q_ARG(double, luminance), Q_ARG(bool, separateEdit));
    refresh();
    requestRender(true);
}

void EngineService::applyPresetValues(const QVariantList &values) {
    if (!values.isEmpty()) applyValues({QVariantMap{{"preset", values}}});
}
void EngineService::applyPresetValuesTo(const QVariantList &items, const QVariantList &values) {
    if (!values.isEmpty()) applyValuesTo(items, {QVariantMap{{"preset", values}}});
}

void EngineService::applyValues(const QVariantList &given) {
    editBoundary();
    if (m_imgid < 0 || given.isEmpty()) return;
    if (given.size()==1 && given.first().toMap().contains("preset")) m_printHeld.clear();
    beginUndoGroup();
    const auto grouped = qScopeGuard([this] { endUndoGroup(); });
    // A saved look carrying its own local adjustments replaces the photo's
    // (removing them is History like any edit, so Undo brings them back).
    QVariantList effective = given;
    if (!m_locals.isEmpty() && std::any_of(given.cbegin(), given.cend(), [](const QVariant &v) { return v.toMap().value(QStringLiteral("localset")).toBool(); })) {
        QVariantList incoming, current;
        for (const QVariant &v : given) if (v.toMap().value(QStringLiteral("local")).toBool()) incoming << v;
        for (const QVariant &l : std::as_const(m_locals)) { QVariantMap m = l.toMap(); m.remove(QStringLiteral("priority")); m[QStringLiteral("local")] = true; current << m; }
        // The same locals as the photo has: nothing to replace, and replacing
        // them anyway would only add steps that change nothing.
        if (incoming == current) effective.removeIf([](const QVariant &v) { const QVariantMap m = v.toMap(); return m.value(QStringLiteral("local")).toBool() || m.value(QStringLiteral("localset")).toBool(); });
    }
    if (effective.isEmpty()) return;
    QStringList parked;
    const QVariantList values = withToneMapperRules(effective, [this](const QString &op) { return moduleEnabled(op); }, &parked);
    if (!parked.isEmpty()) m_printHeld = parked;
    m_dirty = true;
    QMetaObject::invokeMethod(m_worker, "setParams", Qt::QueuedConnection, Q_ARG(int, m_imgid), Q_ARG(QVariantList, values));
    refresh();
    requestRender();
}
void EngineService::toolAction(const QString &operation,const QString &action,const QVariantMap &values) {
    editBoundary();
    beginUndoGroup();
    const auto grouped = qScopeGuard([this] { endUndoGroup(); });
    if(m_imgid<0) return;
    m_dirty=true;
    QMetaObject::invokeMethod(m_worker,"toolAction",Qt::QueuedConnection,Q_ARG(int,m_imgid),Q_ARG(QString,operation),Q_ARG(QString,action),Q_ARG(QVariantMap,values));
    refresh();
    requestRender(operation == QLatin1String("retouch"));
}

QVariantList EngineService::currentValues(bool withLocals) const {
    return PresetState::snapshot(m_params, m_toolState, m_locals, m_spots, m_curve, m_parametric, m_zones, withLocals);
}

void EngineService::resetParam(const QString &op, const QString &field) {
    editBoundary();
    for (const QVariant &v : m_params) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("op")) == op && m.value(QStringLiteral("field")) == field) {
            if (!m.value("enabled").toBool() && m.contains("idle")) return;
            setParam(op, field, m.value("reset", m.value("def")).toDouble());
            return;
        }
    }
}

void EngineService::setModuleEnabled(const QString &op, bool on) {
    editBoundary();
    if (m_imgid < 0) return;
    if (printHandOff(op, QString(), QVariant(), on)) return;
    if (on && toneMapperParked(op)) {
        // The print stock does the tone mapping; this one would do it twice.
        setStatus(tr("The print stock replaces the tone mapper; turn the print off to use it"));
        paramsTouched();   // the eye goes back to what the engine has
        return;
    }
    if (on) {
        const QString rival = rivalToneMapper(op);
        if (!rival.isEmpty()) {
            applyValues({QVariantMap{{QStringLiteral("op"), op}, {QStringLiteral("enabled"), true}},
                         QVariantMap{{QStringLiteral("op"), rival}, {QStringLiteral("enabled"), false}}});
            return;
        }
    }
    m_dirty = true;
    QMetaObject::invokeMethod(m_worker, "setEnabled", Qt::QueuedConnection,
                              Q_ARG(int, m_imgid), Q_ARG(QString, op), Q_ARG(bool, on));
    refresh();
    requestRender();
}

void EngineService::addLocal(int shape) {
    editBoundary();
    // One step for however many items it writes (a local is a stack of tools).
    beginUndoGroup();
    const auto grouped = qScopeGuard([this] { endUndoGroup(); });
    if (m_imgid < 0) return;
    m_dirty = true;
    QMetaObject::invokeMethod(m_worker, "addLocal", Qt::QueuedConnection, Q_ARG(int, m_imgid), Q_ARG(int, shape));
    refresh();
    requestRender(true);
}

void EngineService::setLocalMask(int priority, double cx, double cy, double a, double b, double rotation, double opacity) {
    editBoundary();
    beginUndoGroup();
    const auto grouped = qScopeGuard([this] { endUndoGroup(); });
    if (m_imgid < 0) return;
    // Optimistic: the overlay follows the drag without waiting for the engine.
    for (int i = 0; i < m_locals.size(); ++i) {
        QVariantMap m = m_locals[i].toMap();
        if (m.value(QStringLiteral("priority")).toInt() != priority) continue;
        m[QStringLiteral("cx")] = cx; m[QStringLiteral("cy")] = cy; m[QStringLiteral("opacity")] = opacity;
        if (m.value(QStringLiteral("shape")).toInt() == 1) { m[QStringLiteral("radius")] = a; m[QStringLiteral("border")] = b; }
        else { m[QStringLiteral("compression")] = a; m[QStringLiteral("rotation")] = rotation; }
        m_locals[i] = m;
    }
    emit localsChanged();
    m_dirty = true;
    QMetaObject::invokeMethod(m_worker, "setLocalMask", Qt::QueuedConnection, Q_ARG(int, m_imgid), Q_ARG(int, priority),
                              Q_ARG(double, cx), Q_ARG(double, cy), Q_ARG(double, a), Q_ARG(double, b), Q_ARG(double, rotation), Q_ARG(double, opacity));
    refresh();
    requestRender(true);
}

void EngineService::setLocalParam(int priority, const QString &field, double value) {
    editBoundary(QStringLiteral("local%1.%2").arg(priority).arg(field));
    beginUndoGroup();
    const auto grouped = qScopeGuard([this] { endUndoGroup(); });
    if (m_imgid < 0) return;
    for (int i = 0; i < m_locals.size(); ++i) {
        QVariantMap m = m_locals[i].toMap();
        if (m.value(QStringLiteral("priority")).toInt() == priority) { m[field] = value; m[QStringLiteral("enabled")] = true; m_locals[i] = m; }
    }
    emit localsChanged();
    m_dirty = true;
    QMetaObject::invokeMethod(m_worker, "setLocalParam", Qt::QueuedConnection, Q_ARG(int, m_imgid), Q_ARG(int, priority), Q_ARG(QString, field), Q_ARG(double, value));
    refresh();
    requestRender(true);
}

void EngineService::setLocalColour(int priority, double warmth, double tint) {
    if (m_imgid < 0 || !std::isfinite(warmth) || !std::isfinite(tint)) return;
    const auto exists = std::any_of(m_locals.cbegin(), m_locals.cend(), [priority](const QVariant &v) {
        return v.toMap().value(QStringLiteral("priority")).toInt() == priority;
    });
    if (!exists) return;
    warmth = std::clamp(warmth, -100., 100.); tint = std::clamp(tint, -100., 100.);
    const QString key = QStringLiteral("local%1.colour").arg(priority);
    // A held wheel remains one gesture even if the user pauses or a RAW
    // preview takes longer than the normal slider merge interval.
    if (previewEditing() && m_editKey == key) m_editClock.restart();
    editBoundary(key);
    beginUndoGroup();
    const auto grouped = qScopeGuard([this] { endUndoGroup(); });
    for (auto &v : m_locals) {
        auto m = v.toMap();
        if (m.value(QStringLiteral("priority")).toInt() != priority) continue;
        m[QStringLiteral("warmth")] = warmth; m[QStringLiteral("tint")] = tint;
        m[QStringLiteral("enabled")] = true; v = m;
    }
    emit localsChanged(); m_dirty = true;
    QMetaObject::invokeMethod(m_worker, "setLocalColour", Qt::QueuedConnection,
        Q_ARG(int, m_imgid), Q_ARG(int, priority), Q_ARG(double, warmth), Q_ARG(double, tint));
    refresh(); requestRender(true);
}

void EngineService::setLocalEnabled(int priority, bool on) {
    editBoundary();
    beginUndoGroup();
    const auto grouped = qScopeGuard([this] { endUndoGroup(); });
    if (m_imgid < 0) return;
    m_dirty = true;
    QMetaObject::invokeMethod(m_worker, "setLocalEnabled", Qt::QueuedConnection, Q_ARG(int, m_imgid), Q_ARG(int, priority), Q_ARG(bool, on));
    refresh();
    requestRender(true);
}

void EngineService::removeLocal(int priority) {
    editBoundary();
    beginUndoGroup();
    const auto grouped = qScopeGuard([this] { endUndoGroup(); });
    if (m_imgid < 0) return;
    m_dirty = true;
    QMetaObject::invokeMethod(m_worker, "removeLocal", Qt::QueuedConnection, Q_ARG(int, m_imgid), Q_ARG(int, priority));
    refresh();
    requestRender(true);
}

QVariantList EngineService::shapes() const {
    for (const QVariant &v : m_locals) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("priority")).toInt() == m_activeLocal) return m.value(QStringLiteral("shapes")).toList();
    }
    return {};
}

void EngineService::addShape(int shape) {
    editBoundary();
    beginUndoGroup();
    const auto grouped = qScopeGuard([this] { endUndoGroup(); });
    if (m_imgid < 0 || m_activeLocal < 0) return;
    m_dirty = true;
    QMetaObject::invokeMethod(m_worker, "addShape", Qt::QueuedConnection, Q_ARG(int, m_imgid), Q_ARG(int, m_activeLocal), Q_ARG(int, shape));
    refresh();
    requestRender(true);
}

void EngineService::addBrushStroke(const QVariantList &nodes, double size, double hardness, double flow) {
    editBoundary();
    beginUndoGroup();
    const auto grouped = qScopeGuard([this] { endUndoGroup(); });
    if (m_imgid < 0 || m_activeLocal < 0 || nodes.isEmpty()) return;
    m_dirty = true;
    QMetaObject::invokeMethod(m_worker, "addBrushStroke", Qt::QueuedConnection, Q_ARG(int, m_imgid), Q_ARG(int, m_activeLocal),
                              Q_ARG(QVariantList, nodes), Q_ARG(double, size), Q_ARG(double, hardness), Q_ARG(double, flow));
    refresh();
    requestRender(true);
}

void EngineService::addPenPath(const QVariantList &nodes, double feather, bool newLocal) {
    if (m_imgid < 0 || (!newLocal && m_activeLocal < 0) || nodes.size() < 3 || nodes.size() > 512) return;
    editBoundary(); beginUndoGroup();
    const auto grouped = qScopeGuard([this] { endUndoGroup(); });
    m_dirty = true;
    QMetaObject::invokeMethod(m_worker, "addPenPath", Qt::QueuedConnection, Q_ARG(int, m_imgid), Q_ARG(int, m_activeLocal),
                              Q_ARG(QVariantList, nodes), Q_ARG(double, feather), Q_ARG(bool, newLocal));
    refresh(); requestRender(true);
}

QObject *EngineService::ai() const { return m_ai; }
QObject *EngineService::denoise() const { return m_denoise; }
QObject *EngineService::imageMatch() const { return m_imageMatch; }
void EngineService::requestImageMatch(int ticket,const QString &action,const QVariantMap &data,const std::shared_ptr<std::atomic<int>> &cancel) {
    if(!m_ready) {emit imageMatchReady(ticket,action,{},tr("Wait for the processing engine."));return;}
    ++m_fileOperations;
    QMetaObject::invokeMethod(m_worker,[worker=m_worker,ticket,action,data,cancel]{worker->matchRequest(ticket,action,data,cancel);},Qt::QueuedConnection);
}

void EngineService::addAiSelection(const QImage &mask) {
    QString error;
    if (m_imgid < 0) error = tr("Open a photo first.");
    else if (mask.isNull()) error = tr("The selection could not be read.");
    else if (mask.width() > 4096 || mask.height() > 4096) error = tr("The selection is larger than the supported 4096-pixel edge.");
    else if (cropMode()) error = tr("Close Crop before creating the mask.");
    else if (!maintenanceReady()) error = tr("Wait for the photo to finish rendering, then try again.");
    if (!error.isEmpty()) { emit aiMaskAccepted(false, error); return; }
    editBoundary(); beginUndoGroup();
    const auto grouped = qScopeGuard([this] { endUndoGroup(); });
    m_dirty = true;
    QMetaObject::invokeMethod(m_worker, "addAiSelection", Qt::QueuedConnection, Q_ARG(int, m_imgid), Q_ARG(QImage, mask));
    refresh(); requestRender(true);
}

void EngineService::requestAiSource(int ticket, const QString &stem) {
    if (m_imgid < 0 || !maintenanceReady()) { emit aiSourceReady(ticket, {}, tr("Wait for the current edit to finish.")); return; }
    ++m_fileOperations;
    QMetaObject::invokeMethod(m_worker, "aiSource", Qt::QueuedConnection, Q_ARG(int, m_imgid), Q_ARG(int, m_generation.load()), Q_ARG(int, ticket), Q_ARG(QString, stem));
}

void EngineService::requestAiRefinementSource(int ticket, const QString &path) {
    if (m_imgid < 0 || !maintenanceReady()) { emit aiRefinementSourceReady(ticket, {}, tr("Wait for the current edit to finish.")); return; }
    ++m_fileOperations;
    QMetaObject::invokeMethod(m_worker, "aiRefinementSource", Qt::QueuedConnection, Q_ARG(int, m_imgid),
                             Q_ARG(int, m_generation.load()), Q_ARG(int, ticket), Q_ARG(QString, path));
}

void EngineService::requestAiRepairSource(int ticket,const QString &stem,const QImage &mask) {
    if(m_imgid<0 || !maintenanceReady()) { emit aiRepairSourceReady(ticket,{},tr("Wait for the current edit to finish.")); return; }
    ++m_fileOperations;
    QMetaObject::invokeMethod(m_worker,"aiRepairSource",Qt::QueuedConnection,Q_ARG(int,m_imgid),Q_ARG(int,m_generation.load()),Q_ARG(int,ticket),Q_ARG(QString,stem),Q_ARG(QImage,mask));
}
void EngineService::previewAiRepair(int ticket,const QString &patch) {
    ++m_fileOperations;
    QMetaObject::invokeMethod(m_worker,"aiRepairPreview",Qt::QueuedConnection,Q_ARG(int,m_imgid),Q_ARG(int,m_generation.load()),Q_ARG(int,ticket),Q_ARG(QString,patch));
}
void EngineService::applyAiRepair(const QString &key) {
    if(key.size()!=64 || m_imgid<0 || !maintenanceReady()) return;
    QMetaObject::invokeMethod(m_worker,[worker=m_worker,key]{worker->draftRepairs.remove(key);},Qt::QueuedConnection);
    applyValues({QVariantMap{{"op","omarawrepair"},{"field","file"},{"text",key},{"enabled",true}}});
}
void EngineService::discardAiRepair(const QString &key) {
    QMetaObject::invokeMethod(m_worker,[worker=m_worker,key]{worker->discardRepair(key);},Qt::QueuedConnection);
}
void EngineService::exportAiRepair(int ticket,const QString &key,const QString &stem) {
    ++m_fileOperations;
    QMetaObject::invokeMethod(m_worker,"aiRepairExport",Qt::QueuedConnection,Q_ARG(int,m_imgid),Q_ARG(int,ticket),Q_ARG(QString,key),Q_ARG(QString,stem));
}

void EngineService::prepareAiCopy(int ticket, const QString &source, const QString &destination) {
    ++m_fileOperations;
    QMetaObject::invokeMethod(m_worker, [worker=m_worker, ticket, source, destination] {
        QString error; Metadata::copyProcessedMetadata(source, destination, &error);
        emit worker->aiCopyPrepared(ticket, error);
    }, Qt::QueuedConnection);
}

void EngineService::openDenoiseCopy(const QString &path) {
    load(path, 0);
    // The edit was prepared before the catalogue import. Publish its first
    // rendered thumbnail and edit badge just like an edit made in Develop.
    m_dirty = true;
    if (m_imgid >= 0 && !m_busy) requestRender();
}

void EngineService::prepareDenoiseCopy(const QString &path, const QVariantList &values) {
    ++m_fileOperations;
    QMetaObject::invokeMethod(m_worker, [worker=m_worker, path, values] {
        worker->prepareDenoiseCopy(path, values);
    }, Qt::QueuedConnection);
}

void EngineService::renderDenoisePreview(int ticket, const std::shared_ptr<QTemporaryDir> &directory,
                                         const QString &source, const QVariantList &values) {
    ++m_fileOperations;
    // Keep scratch files alive through cancellation/navigation until the worker
    // has finished reading them. The service rejects obsolete ticket results.
    QMetaObject::invokeMethod(m_worker, [worker=m_worker, ticket, directory, source, values] {
        worker->denoisePreview(ticket, directory->path(), source, values);
    }, Qt::QueuedConnection);
}

void EngineService::setPenPath(int index, const QVariantList &nodes, double feather) {
    if (m_imgid < 0 || m_activeLocal < 0 || nodes.size() < 3 || nodes.size() > 512) return;
    editBoundary(); beginUndoGroup();
    const auto grouped = qScopeGuard([this] { endUndoGroup(); });
    m_dirty = true;
    for (int i = 0; i < m_locals.size(); ++i) {
        auto local = m_locals[i].toMap();
        if (local.value("priority").toInt() != m_activeLocal) continue;
        auto shapes = local.value("shapes").toList();
        if (index < 0 || index >= shapes.size()) break;
        auto shape = shapes[index].toMap();
        if (shape.value("shape").toInt() != 4) return;
        shape["displayPoints"] = nodes; shape["border"] = feather;
        shapes[index] = shape; local["shapes"] = shapes; m_locals[i] = local;
    }
    emit localsChanged();
    QMetaObject::invokeMethod(m_worker, "setPenPath", Qt::QueuedConnection, Q_ARG(int, m_imgid), Q_ARG(int, m_activeLocal), Q_ARG(int, index),
                              Q_ARG(QVariantList, nodes), Q_ARG(double, feather));
    refresh(); requestRender(true);
}

void EngineService::setShape(int index, double cx, double cy, double a, double b, double rotation, double opacity, bool separateEdit) {
    if (separateEdit) editBoundary(); else editBoundary(QStringLiteral("shape%1").arg(index));
    beginUndoGroup();
    const auto grouped = qScopeGuard([this] { endUndoGroup(); });
    if (m_imgid < 0 || m_activeLocal < 0) return;
    m_dirty = true;
    // Optimistic, like setLocalMask: the drag must not snap back mid-gesture.
    for (int i = 0; i < m_locals.size(); ++i) {
        QVariantMap l = m_locals[i].toMap();
        if (l.value(QStringLiteral("priority")).toInt() != m_activeLocal) continue;
        QVariantList sh = l.value(QStringLiteral("shapes")).toList();
        if (index < 0 || index >= sh.size()) break;
        QVariantMap m = sh[index].toMap();
        m[QStringLiteral("cx")] = cx; m[QStringLiteral("cy")] = cy; m[QStringLiteral("opacity")] = opacity;
        if (m.value(QStringLiteral("shape")).toInt() == 2) { m[QStringLiteral("compression")] = a; m[QStringLiteral("rotation")] = rotation; }
        else if (m.value(QStringLiteral("shape")).toInt() == 1) { m[QStringLiteral("radius")] = a; m[QStringLiteral("border")] = b; }
        else if (m.value(QStringLiteral("shape")).toInt() == 4) { m[QStringLiteral("border")] = a; }
        sh[index] = m;
        l[QStringLiteral("shapes")] = sh;
        m_locals[i] = l;
    }
    emit localsChanged();
    QMetaObject::invokeMethod(m_worker, "setShape", Qt::QueuedConnection, Q_ARG(int, m_imgid), Q_ARG(int, m_activeLocal), Q_ARG(int, index),
                              Q_ARG(double, cx), Q_ARG(double, cy), Q_ARG(double, a), Q_ARG(double, b), Q_ARG(double, rotation), Q_ARG(double, opacity), Q_ARG(bool, separateEdit));
    refresh();
    requestRender(true);
}

void EngineService::setShapeCombine(int index, int combine, bool inverse) {
    editBoundary();
    beginUndoGroup();
    const auto grouped = qScopeGuard([this] { endUndoGroup(); });
    if (m_imgid < 0 || m_activeLocal < 0) return;
    m_dirty = true;
    QMetaObject::invokeMethod(m_worker, "setShapeCombine", Qt::QueuedConnection, Q_ARG(int, m_imgid), Q_ARG(int, m_activeLocal),
                              Q_ARG(int, index), Q_ARG(int, combine), Q_ARG(bool, inverse));
    refresh();
    requestRender(true);
}

void EngineService::setRange(int channel, bool active, bool inverse, double p0, double p1, double p2, double p3) {
    editBoundary(QStringLiteral("range%1").arg(channel));
    beginUndoGroup();
    const auto grouped = qScopeGuard([this] { endUndoGroup(); });
    if (m_imgid < 0 || m_activeLocal < 0) return;
    m_dirty = true;
    // Optimistic, so dragging a range node does not fight the engine.
    for (int i = 0; i < m_locals.size(); ++i) {
        QVariantMap l = m_locals[i].toMap();
        if (l.value(QStringLiteral("priority")).toInt() != m_activeLocal) continue;
        QVariantList rs = l.value(QStringLiteral("ranges")).toList();
        for (int k = 0; k < rs.size(); ++k) {
            QVariantMap r = rs[k].toMap();
            if (r.value(QStringLiteral("channel")).toInt() != channel) continue;
            r[QStringLiteral("active")] = active; r[QStringLiteral("inverse")] = inverse;
            r[QStringLiteral("p0")] = p0; r[QStringLiteral("p1")] = p1; r[QStringLiteral("p2")] = p2; r[QStringLiteral("p3")] = p3;
            rs[k] = r;
        }
        l[QStringLiteral("ranges")] = rs;
        m_locals[i] = l;
    }
    emit localsChanged();
    QMetaObject::invokeMethod(m_worker, "setRange", Qt::QueuedConnection, Q_ARG(int, m_imgid), Q_ARG(int, m_activeLocal), Q_ARG(int, channel),
                              Q_ARG(bool, active), Q_ARG(bool, inverse), Q_ARG(double, p0), Q_ARG(double, p1), Q_ARG(double, p2), Q_ARG(double, p3));
    refresh();
    requestRender();
}

void EngineService::setMaskRefine(double blur, double feather, int guide, double contrast, double brightness) {
    editBoundary(QStringLiteral("mask-refine"));
    beginUndoGroup();
    const auto grouped = qScopeGuard([this] { endUndoGroup(); });
    if (m_imgid < 0 || m_activeLocal < 0) return;
    m_dirty = true;
    // Optimistic, so the sliders and the preview move together.
    for (int i = 0; i < m_locals.size(); ++i) {
        QVariantMap l = m_locals[i].toMap();
        if (l.value(QStringLiteral("priority")).toInt() != m_activeLocal) continue;
        l[QStringLiteral("blur")] = blur; l[QStringLiteral("feather")] = feather; l[QStringLiteral("guide")] = guide;
        l[QStringLiteral("maskContrast")] = contrast; l[QStringLiteral("maskBrightness")] = brightness;
        m_locals[i] = l;
    }
    emit localsChanged();
    QMetaObject::invokeMethod(m_worker, "setMaskRefine", Qt::QueuedConnection, Q_ARG(int, m_imgid), Q_ARG(int, m_activeLocal),
                              Q_ARG(double, blur), Q_ARG(double, feather), Q_ARG(int, guide), Q_ARG(double, contrast), Q_ARG(double, brightness));
    refresh();
    requestRender();
}

void EngineService::addSpot(int algorithm, double cx, double cy, double radius, double border, double sx, double sy) {
    editBoundary();
    beginUndoGroup();
    const auto grouped = qScopeGuard([this] { endUndoGroup(); });
    if (m_imgid < 0) return;
    m_dirty = true;
    QMetaObject::invokeMethod(m_worker, "addSpot", Qt::QueuedConnection, Q_ARG(int, m_imgid), Q_ARG(int, algorithm),
                              Q_ARG(double, cx), Q_ARG(double, cy), Q_ARG(double, radius), Q_ARG(double, border), Q_ARG(double, sx), Q_ARG(double, sy));
    refresh();
    requestRender(true);
}

void EngineService::setSpot(int index, double cx, double cy, double radius, double border, double sx, double sy, double opacity) {
    editBoundary(QStringLiteral("spot%1").arg(index));
    beginUndoGroup();
    const auto grouped = qScopeGuard([this] { endUndoGroup(); });
    if (m_imgid < 0 || index < 0 || index >= m_spots.size()) return;
    m_dirty = true;
    // Optimistic: the overlay follows the drag.
    QVariantMap m = m_spots[index].toMap();
    m[QStringLiteral("cx")] = cx; m[QStringLiteral("cy")] = cy; m[QStringLiteral("radius")] = radius; m[QStringLiteral("border")] = border;
    m[QStringLiteral("sx")] = sx; m[QStringLiteral("sy")] = sy; m[QStringLiteral("opacity")] = opacity;
    m_spots[index] = m;
    emit spotsChanged();
    QMetaObject::invokeMethod(m_worker, "setSpot", Qt::QueuedConnection, Q_ARG(int, m_imgid), Q_ARG(int, index),
                              Q_ARG(double, cx), Q_ARG(double, cy), Q_ARG(double, radius), Q_ARG(double, border), Q_ARG(double, sx), Q_ARG(double, sy), Q_ARG(double, opacity));
    refresh();
    requestRender(true);
}

void EngineService::setSpotAlgorithm(int index, int algorithm, double blurRadius, double fillBrightness) {
    editBoundary();
    beginUndoGroup();
    const auto grouped = qScopeGuard([this] { endUndoGroup(); });
    if (m_imgid < 0 || index < 0 || index >= m_spots.size()) return;
    m_dirty = true;
    QMetaObject::invokeMethod(m_worker, "setSpotAlgorithm", Qt::QueuedConnection, Q_ARG(int, m_imgid), Q_ARG(int, index),
                              Q_ARG(int, algorithm), Q_ARG(double, blurRadius), Q_ARG(double, fillBrightness));
    refresh();
    requestRender(true);
}

void EngineService::setCurve(int channel, const QVariantList &xs, const QVariantList &ys, int type) {
    editBoundary(QStringLiteral("curve%1").arg(channel));
    if (m_imgid < 0 || channel < 0 || channel > 2 || xs.size() < 2 || xs.size() != ys.size()) return;
    m_dirty = true;
    // Optimistic: the nodes follow the drag; the sampled curve catches up with the render.
    QVariantList channels = m_curve.value(QStringLiteral("channels")).toList();
    if (channel < channels.size()) {
        QVariantMap c = channels[channel].toMap();
        c[QStringLiteral("xs")] = xs; c[QStringLiteral("ys")] = ys; c[QStringLiteral("count")] = int(xs.size()); c[QStringLiteral("type")] = type;
        channels[channel] = c;
        m_curve[QStringLiteral("channels")] = channels;
        m_curve[QStringLiteral("enabled")] = true;
        emit curveChanged();
    }
    QMetaObject::invokeMethod(m_worker, "setCurve", Qt::QueuedConnection, Q_ARG(int, m_imgid), Q_ARG(int, channel),
                              Q_ARG(QVariantList, xs), Q_ARG(QVariantList, ys), Q_ARG(int, type));
    refresh();
    requestRender();
}

void EngineService::setCurveLinked(bool linked) {
    editBoundary();
    if (m_imgid < 0) return;
    m_dirty = true;
    QMetaObject::invokeMethod(m_worker, "setCurveLinked", Qt::QueuedConnection, Q_ARG(int, m_imgid), Q_ARG(bool, linked));
    refresh();
    requestRender();
}

void EngineService::resetCurve(int channel) {
    editBoundary();
    const QVariantList channels = m_curve.value(QStringLiteral("channels")).toList();
    const int type = channel < channels.size() ? channels[channel].toMap().value(QStringLiteral("type"), 2).toInt() : 2;
    setCurve(channel, QVariantList{0.0, 1.0}, QVariantList{0.0, 1.0}, type);
}

QVariantList EngineService::zoneBands() {
    // Lab hues of the eight XMP-style bands, in turns; swatches are
    // roughly the sRGB colour at that hue.
    struct Band { const char *name, *colour; double degrees; };
    static const Band bands[] = {
        {"Red", "#E5484D", 40}, {"Orange", "#F08C2E", 65}, {"Yellow", "#E5C822", 100}, {"Green", "#3DBF5C", 136},
        // Centres in Lab hue (D50). Measured so each colour falls in the band
        // named after it: sky blue ~271, the chart's blue ~298, pure sRGB blue
        // ~306, purple ~318, magenta ~342 (Blue at 265 and Purple at 306 had
        // saturated blues answering to Purple).
        {"Aqua", "#2FBFBF", 200}, {"Blue", "#3B82F6", 295}, {"Purple", "#8B5CF6", 322}, {"Magenta", "#D946A8", 348},
    };
    QVariantList out;
    for (const Band &b : bands)
        out << QVariantMap{{QStringLiteral("name"), QString::fromLatin1(b.name)}, {QStringLiteral("colour"), QString::fromLatin1(b.colour)}, {QStringLiteral("hue"), b.degrees / 360.0}};
    return out;
}

void EngineService::setZone(int band, int channel, double value) {
    editBoundary(QStringLiteral("zone%1.%2").arg(band).arg(channel));
    if (m_imgid < 0 || channel < 0 || channel > 2 || !m_zones.value(QStringLiteral("found")).toBool()) return;
    QVariantList bands = m_zones.value(QStringLiteral("bands")).toList();
    if (band < 0 || band >= bands.size()) return;
    static const char *keys[] = {"lum", "sat", "shift"};
    static const double scale[] = {400.0, 200.0, 1200.0};
    value = qBound(-100.0, value, 100.0);
    QVariantMap b = bands[band].toMap(); b[QString::fromLatin1(keys[channel])] = value; bands[band] = b;
    // The whole channel is rewritten as the eight band nodes.
    QVariantList xs, ys;
    for (const QVariant &bv : bands) {
        const QVariantMap bm = bv.toMap();
        xs << bm.value(QStringLiteral("hue")).toDouble();
        ys << qBound(0.0, 0.5 + bm.value(QString::fromLatin1(keys[channel])).toDouble() / scale[channel], 1.0);
    }
    m_dirty = true;
    // Optimistic, so the slider holds while the engine renders.
    m_zones[QStringLiteral("bands")] = bands;
    m_zones[QStringLiteral("enabled")] = true;
    emit zonesChanged();
    QMetaObject::invokeMethod(m_worker, "setZones", Qt::QueuedConnection, Q_ARG(int, m_imgid), Q_ARG(int, channel), Q_ARG(QVariantList, xs), Q_ARG(QVariantList, ys));
    refresh();
    requestRender();
}

void EngineService::setParametric(int region, double value) {
    editBoundary(QStringLiteral("parametric%1").arg(region));
    if (m_imgid < 0 || region < 0 || region > 3 || !m_parametric.value(QStringLiteral("found")).toBool()) return;
    static const char *keys[] = {"highlights", "lights", "darks", "shadows"};
    m_parametric[QString::fromLatin1(keys[region])] = qBound(-100.0, value, 100.0);
    double sliders[4];
    for (int r = 0; r < 4; ++r) sliders[r] = m_parametric.value(QString::fromLatin1(keys[r])).toDouble();
    QVariantList xs, ys;
    ToneRegions::curve(sliders, xs, ys);
    m_dirty = true;
    m_parametric[QStringLiteral("enabled")] = true;
    emit parametricChanged();
    QMetaObject::invokeMethod(m_worker, "setLCurve", Qt::QueuedConnection, Q_ARG(int, m_imgid), Q_ARG(QVariantList, xs), Q_ARG(QVariantList, ys));
    refresh();
    requestRender();
}

void EngineService::setLabCurve(int channel, const QVariantList &xs, const QVariantList &ys, int type) {
    editBoundary(QStringLiteral("lab-curve%1").arg(channel));
    if (m_imgid < 0 || channel < 1 || channel > 2 || xs.size() < 2 || xs.size() != ys.size() || !m_parametric.value(QStringLiteral("found")).toBool()) return;
    QVariantList ab = m_parametric.value(QStringLiteral("ab")).toList();
    if (ab.size() != 2) return;
    // Shown at once; the engine's own samples replace the editor's preview
    // spline when its read-back lands.
    QVariantMap c = ab[channel - 1].toMap();
    c[QStringLiteral("xs")] = xs; c[QStringLiteral("ys")] = ys; c[QStringLiteral("type")] = type; c[QStringLiteral("samples")] = QVariantList();
    ab[channel - 1] = c;
    m_parametric[QStringLiteral("ab")] = ab; m_parametric[QStringLiteral("enabled")] = true;
    refreshLabColour();
    m_dirty = true;
    emit parametricChanged();
    QMetaObject::invokeMethod(m_worker, "setLabCurve", Qt::QueuedConnection, Q_ARG(int, m_imgid), Q_ARG(int, channel), Q_ARG(QVariantList, xs), Q_ARG(QVariantList, ys), Q_ARG(int, type));
    refresh();
    requestRender();
}

// The Lab colour sliders as read from the two curves (see labcolour.h).
void EngineService::refreshLabColour() {
    const QVariantList ab = m_parametric.value(QStringLiteral("ab")).toList();
    if (ab.size() != 2) { m_parametric.remove(QStringLiteral("lab")); return; }
    // Not being dragged: the master comes from the photo's latest Lab step,
    // as it was recorded, so the split reads as the sliders were set.
    if (!m_labMasterKnown && !m_labPending) {
        int last = -1;
        for (int i = 0; i < qMin(m_historyEnd, int(m_history.size())); ++i)
            if (m_history.at(i).toMap().value(QStringLiteral("op")) == QLatin1String("tonecurve")) last = i;
        const QVariantMap masters = m_savedGroups.value(QStringLiteral("lab|") + groupKey(m_path, m_variant)).toMap();
        if (last >= 0 && masters.contains(QString::number(last + 1))) { m_labMaster = masters.value(QString::number(last + 1)).toDouble(); m_labMasterKnown = true; }
    }
    const QVariantMap a = ab[0].toMap(), b = ab[1].toMap();
    m_parametric[QStringLiteral("lab")] = LabColour::sliders(LabColour::read(a.value(QStringLiteral("xs")).toList(), a.value(QStringLiteral("ys")).toList()),
                                                           LabColour::read(b.value(QStringLiteral("xs")).toList(), b.value(QStringLiteral("ys")).toList()),
                                                           &m_labMaster, &m_labMasterKnown);
}

void EngineService::setLabColour(const QString &key, double value) {
    editBoundary(QStringLiteral("lab-colour.") + key);
    QVariantList ab = m_parametric.value(QStringLiteral("ab")).toList();
    if (m_imgid < 0 || ab.size() != 2 || !m_parametric.value(QStringLiteral("found")).toBool()) return;
    QVariantMap lab = m_parametric.value(QStringLiteral("lab")).toMap();
    if (!lab.contains(key) || key == QLatin1String("custom")) return;
    // Moving a slider over curves shaped by hand starts from the sliders' own
    // (neutral) reading of them and replaces the hand-made shape.
    lab[key] = qBound(-100.0, value, 100.0);
    lab[QStringLiteral("custom")] = false;
    if (key == QLatin1String("separation")) { m_labMaster = lab.value(key).toDouble(); m_labMasterKnown = true; }
    const double master = lab.value(QStringLiteral("separation")).toDouble();
    const auto total = [&](const char *family) { return qBound(-100.0, master + lab.value(QString::fromLatin1(family)).toDouble(), 100.0); };
    LabColour::Axis a{total("greens"), total("magentas"), lab.value(QStringLiteral("tintA")).toDouble(), false},
                    b{total("blues"), total("yellows"), lab.value(QStringLiteral("tintB")).toDouble(), false};
    QVariantList xa, ya, xb, yb;
    LabColour::curve(a, xa, ya); LabColour::curve(b, xb, yb);
    const QVariantList *xs[] = {&xa, &xb}, *ys[] = {&ya, &yb};
    for (int ch = 0; ch < 2; ++ch) {
        QVariantMap c = ab[ch].toMap();
        c[QStringLiteral("xs")] = *xs[ch]; c[QStringLiteral("ys")] = *ys[ch]; c[QStringLiteral("type")] = 2; c[QStringLiteral("samples")] = QVariantList();
        ab[ch] = c;
    }
    m_parametric[QStringLiteral("ab")] = ab; m_parametric[QStringLiteral("lab")] = lab; m_parametric[QStringLiteral("enabled")] = true;
    m_labPending = true;
    m_dirty = true;
    emit parametricChanged();
    QMetaObject::invokeMethod(m_worker, "setLabCurves", Qt::QueuedConnection, Q_ARG(int, m_imgid), Q_ARG(QVariantList, xa), Q_ARG(QVariantList, ya),
                              Q_ARG(QVariantList, xb), Q_ARG(QVariantList, yb), Q_ARG(int, 2));
    refresh();
    requestRender();
}

void EngineService::resetLabColour() {
    editBoundary();
    if (m_imgid < 0 || m_parametric.value("ab").toList().size() != 2) return;
    m_labMaster = 0; m_labMasterKnown = true;
    resetModuleFields("tonecurve", {"tonecurve[1]", "tonecurve[2]", "tonecurve_nodes[1]", "tonecurve_nodes[2]",
        "tonecurve_type[1]", "tonecurve_type[2]", "tonecurve_autoscale_ab"});
}

void EngineService::resetLabCurve(int channel) {
    editBoundary();
    const QVariantList ab = m_parametric.value(QStringLiteral("ab")).toList();
    if (ab.size() != 2 || channel < 1 || channel > 2) return;
    setLabCurve(channel, QVariantList{0.0, 0.5, 1.0}, QVariantList{0.0, 0.5, 1.0}, ab[channel - 1].toMap().value(QStringLiteral("type"), 2).toInt());
}

void EngineService::resetParametric() {
    editBoundary();
    if (m_imgid < 0 || !m_parametric.value(QStringLiteral("found")).toBool()) return;
    resetModuleFields("tonecurve", {"tonecurve[0]", "tonecurve_nodes[0]", "tonecurve_type[0]"});
}

void EngineService::resetZones() {
    editBoundary();
    if (m_imgid < 0 || !m_zones.value(QStringLiteral("found")).toBool()) return;
    m_dirty = true;
    QVariantList xs, ys;
    for (const QVariant &bv : zoneBands()) { xs << bv.toMap().value(QStringLiteral("hue")).toDouble(); ys << 0.5; }
    for (int ch = 0; ch < 3; ++ch)
        QMetaObject::invokeMethod(m_worker, "setZones", Qt::QueuedConnection, Q_ARG(int, m_imgid), Q_ARG(int, ch), Q_ARG(QVariantList, xs), Q_ARG(QVariantList, ys));
    refresh();
    requestRender();
}

void EngineService::resetModuleFields(const QString &op, const QStringList &fields) {
    if (m_imgid < 0) return;
    m_dirty = true;
    QMetaObject::invokeMethod(m_worker, "resetModule", Qt::QueuedConnection,
        Q_ARG(int, m_imgid), Q_ARG(QString, op), Q_ARG(QStringList, fields));
    refresh(); requestRender();
}

void EngineService::resetModule(const QString &op) {
    editBoundary();
    if (m_imgid < 0) return;
    beginUndoGroup();
    const auto grouped = qScopeGuard([this] { endUndoGroup(); });
    if (op == "tonecurve") {
        // Tone regions share their native module with the independent Lab tool.
        resetParametric();
    } else if (op == "liquify") {
        toolAction(op, "clear", {});
    } else if (op == "retouch") {
        applyValues({QVariantMap{{"spotset", true}, {"spots", QVariantList()}}});
        resetModuleFields(op);
    } else {
        // Disabling a print restores the tone mapper that it replaced.
        if (op == "omarawprint" && moduleEnabled(op)) setModuleEnabled(op, false);
        const bool parked = toneMapperParked(op);
        resetModuleFields(op);
        if (parked) setModuleEnabled(op, false);
    }
}

void EngineService::resetTool(const QString &key) {
    editBoundary();
    if (m_imgid < 0) return;
    beginUndoGroup();
    const auto grouped = qScopeGuard([this] { endUndoGroup(); });
    if (key == "light") {
        resetModule("exposure"); resetModule("shadhi");
        resetModuleFields("colorbalancergb", {"contrast"});
    } else if (key == "colorbalancergb") {
        QStringList fields;
        for (const auto &v : paramsFor(key)) {
            const QString field = v.toMap().value("field").toString();
            if (field != "contrast") fields << field;
        }
        resetModuleFields(key, fields);
    } else if (key == "presence") {
        for (const auto &op : {"bilat", "diffuse", "hazeremoval"}) resetModule(op);
    } else if (key == "curves") {
        resetModule("rgbcurve"); resetModule("tonecurve");
    } else if (key == "lab") {
        resetLabColour();
    } else if (key == "capture") {
        resetModuleFields("demosaic", {"cs_enabled", "cs_radius"});
    } else if (key == "denoiseprofile") {
        resetModule("denoiseprofile"); resetModule("hotpixels");
    } else if (key == "crop") {
        setCrop(0, 0, 1, 1, 0, 0); resetModuleFields("ashift", {"rotation"});
    } else if (key == "ashift") {
        resetModuleFields("ashift", {"lensshift_v", "lensshift_h"});
    } else if (key == "airemove") {
        if (m_ai->mode() == "remove") m_ai->cancel();
        resetModule("omarawrepair");
    } else if (key == "rasterfile") {
        applyValues({QVariantMap{{"rasterset", true}, {"targets", QVariantList()}}});
        resetModuleFields("rasterfile");
    } else if (key == "colorin") {
        if (m_cameraProfileState.value("active").toBool()) selectCameraProfile({});
        resetModule("colorin");
    } else resetModule(key);
}

void EngineService::drainWorker() {
    if (!m_worker || QThread::currentThread() == m_worker->thread()) return;
    QMetaObject::invokeMethod(m_worker, [] {}, Qt::BlockingQueuedConnection);
}

QVariantList EngineService::firstEditValues() const {
    const auto has = [this](const char *op) {
        for (const QVariant &v : m_params) if (v.toMap().value(QStringLiteral("op")) == QLatin1String(op)) return true;
        return false;
    };
    QVariantList out;
    // Only a RAW is demosaiced: every photo lists the module, but the engine
    // switches it on for raw files alone.
    bool raw = false;
    for (const QVariant &v : m_params) { const QVariantMap p = v.toMap(); if (p.value(QStringLiteral("op")) == QLatin1String("demosaic")) { raw = p.value(QStringLiteral("enabled")).toBool(); break; } }
    if (!raw) return out;
    const auto row = [](const char *op, const QString &field, double value) { return QVariantMap{{"op", QLatin1String(op)}, {"field", field}, {"value", value}}; };
    // Capture sharpening, radius measured from the photo (0 = let the engine).
    out << row("demosaic", QStringLiteral("cs_enabled"), 1) << row("demosaic", QStringLiteral("cs_radius"), 0);
    // Noise reduction, Colour only at 50 (as NoiseReductionPanel.qml sends it).
    if (has("denoiseprofile")) {
        out << row("denoiseprofile", QStringLiteral("mode"), 4) << row("denoiseprofile", QStringLiteral("wavelet_color_mode"), 1)
            << row("denoiseprofile", QStringLiteral("strength"), 1) << row("denoiseprofile", QStringLiteral("overshooting"), 1);
        for (int b = 0; b < 7; ++b)
            out << row("denoiseprofile", QStringLiteral("y[4][%1]").arg(b), 0) << row("denoiseprofile", QStringLiteral("y[5][%1]").arg(b), 0.5);
    }
    // The lens profile, only when both body and lens are known to the database.
    if (m_lensProfile.value(QStringLiteral("cameraFound")).toBool() && m_lensProfile.value(QStringLiteral("lensFound")).toBool())
        for (const QVariant &v : m_params) {
            const QVariantMap p = v.toMap();
            if (p.value(QStringLiteral("op")) != QLatin1String("lens")) continue;
            out << QVariantMap{{"op", QStringLiteral("lens")}, {"field", p.value(QStringLiteral("field"))}, {"enabled", true}};
            break;
        }
    return out;
}

void EngineService::removeSpot(int index) {
    editBoundary();
    beginUndoGroup();
    const auto grouped = qScopeGuard([this] { endUndoGroup(); });
    if (m_imgid < 0) return;
    m_dirty = true;
    QMetaObject::invokeMethod(m_worker, "removeSpot", Qt::QueuedConnection, Q_ARG(int, m_imgid), Q_ARG(int, index));
    refresh();
    requestRender(true);
}

void EngineService::pickRange(int channel, double fx, double fy) {
    editBoundary();
    beginUndoGroup();
    const auto grouped = qScopeGuard([this] { endUndoGroup(); });
    if (m_imgid < 0 || m_activeLocal < 0 || channel < 0 || channel > 2) return;
    m_dirty = true;
    QMetaObject::invokeMethod(m_worker, "pickRange", Qt::QueuedConnection, Q_ARG(int, m_imgid),
                              Q_ARG(int, m_activeLocal), Q_ARG(int, channel), Q_ARG(double, fx), Q_ARG(double, fy));
    refresh();
    requestRender();
}

void EngineService::pickColourRange(double x, double y, double x2, double y2, bool newLocal) {
    if (m_imgid < 0 || (!newLocal && m_activeLocal < 0) || busy() || !std::isfinite(x) || !std::isfinite(y)
        || !std::isfinite(x2) || !std::isfinite(y2)) return;
    editBoundary(); beginUndoGroup();
    const auto grouped = qScopeGuard([this] { endUndoGroup(); });
    m_dirty = true;
    QMetaObject::invokeMethod(m_worker, "pickColourRange", Qt::QueuedConnection, Q_ARG(int, m_imgid), Q_ARG(int, m_activeLocal),
        Q_ARG(double, qBound(0.,x,1.)), Q_ARG(double, qBound(0.,y,1.)), Q_ARG(double, qBound(0.,x2,1.)), Q_ARG(double, qBound(0.,y2,1.)), Q_ARG(bool, newLocal));
    refresh(); requestRender(true);
}

void EngineService::setColourRange(double width, double softness) {
    if (m_imgid < 0 || m_activeLocal < 0 || !std::isfinite(width) || !std::isfinite(softness)) return;
    editBoundary(); beginUndoGroup();
    const auto grouped = qScopeGuard([this] { endUndoGroup(); });
    m_dirty = true;
    QMetaObject::invokeMethod(m_worker, "setColourRange", Qt::QueuedConnection, Q_ARG(int, m_imgid), Q_ARG(int, m_activeLocal),
        Q_ARG(double, qBound(1.,width,360.)), Q_ARG(double, qBound(0.,softness,90.)));
    refresh(); requestRender();
}

void EngineService::setMaskInverted(bool on) {
    editBoundary();
    beginUndoGroup();
    const auto grouped = qScopeGuard([this] { endUndoGroup(); });
    if (m_imgid < 0 || m_activeLocal < 0) return;
    m_dirty = true;
    QMetaObject::invokeMethod(m_worker, "setMaskInverted", Qt::QueuedConnection, Q_ARG(int, m_imgid), Q_ARG(int, m_activeLocal), Q_ARG(bool, on));
    refresh();
    requestRender();
}

QVariantList EngineService::ranges() const {
    for (const QVariant &v : m_locals) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("priority")).toInt() == m_activeLocal) return m.value(QStringLiteral("ranges")).toList();
    }
    return {};
}

void EngineService::removeShape(int index) {
    editBoundary();
    beginUndoGroup();
    const auto grouped = qScopeGuard([this] { endUndoGroup(); });
    if (m_imgid < 0 || m_activeLocal < 0) return;
    m_dirty = true;
    QMetaObject::invokeMethod(m_worker, "removeShape", Qt::QueuedConnection, Q_ARG(int, m_imgid), Q_ARG(int, m_activeLocal), Q_ARG(int, index));
    refresh();
    requestRender();
}

void EngineService::duplicateShape(int index) {
    editBoundary();
    beginUndoGroup();
    const auto grouped = qScopeGuard([this] { endUndoGroup(); });
    if (m_imgid < 0 || m_activeLocal < 0) return;
    m_dirty = true;
    QMetaObject::invokeMethod(m_worker, "duplicateShape", Qt::QueuedConnection, Q_ARG(int, m_imgid), Q_ARG(int, m_activeLocal), Q_ARG(int, index));
    refresh();
    requestRender();
}

void EngineService::moveShape(int from, int to) {
    editBoundary();
    beginUndoGroup();
    const auto grouped = qScopeGuard([this] { endUndoGroup(); });
    if (m_imgid < 0 || m_activeLocal < 0 || from == to) return;
    m_dirty = true;
    QMetaObject::invokeMethod(m_worker, "moveShape", Qt::QueuedConnection, Q_ARG(int, m_imgid), Q_ARG(int, m_activeLocal), Q_ARG(int, from), Q_ARG(int, to));
    refresh();
    requestRender();
}

void EngineService::setShapeEnabled(int index, bool on) {
    editBoundary();
    beginUndoGroup();
    const auto grouped = qScopeGuard([this] { endUndoGroup(); });
    if (m_imgid < 0 || m_activeLocal < 0) return;
    m_dirty = true;
    // Optimistic: the row and the overlay dim at once.
    for (int i = 0; i < m_locals.size(); ++i) {
        QVariantMap l = m_locals[i].toMap();
        if (l.value(QStringLiteral("priority")).toInt() != m_activeLocal) continue;
        QVariantList sh = l.value(QStringLiteral("shapes")).toList();
        if (index < 0 || index >= sh.size()) break;
        QVariantMap m = sh[index].toMap(); m[QStringLiteral("enabled")] = on; sh[index] = m;
        l[QStringLiteral("shapes")] = sh; m_locals[i] = l;
    }
    emit localsChanged();
    QMetaObject::invokeMethod(m_worker, "setShapeEnabled", Qt::QueuedConnection, Q_ARG(int, m_imgid), Q_ARG(int, m_activeLocal), Q_ARG(int, index), Q_ARG(bool, on));
    refresh();
    requestRender();
}

void EngineService::renameShape(int index, const QString &name) {
    editBoundary();
    if (m_imgid < 0 || m_activeLocal < 0) return;
    m_dirty = true;
    QMetaObject::invokeMethod(m_worker, "renameShape", Qt::QueuedConnection, Q_ARG(int, m_imgid), Q_ARG(int, m_activeLocal), Q_ARG(int, index), Q_ARG(QString, name));
    refresh();
}

void EngineService::setBrush(int index, double size, double hardness, double flow) {
    editBoundary(QStringLiteral("brush%1").arg(index));
    beginUndoGroup();
    const auto grouped = qScopeGuard([this] { endUndoGroup(); });
    if (m_imgid < 0 || m_activeLocal < 0) return;
    m_dirty = true;
    for (int i = 0; i < m_locals.size(); ++i) {
        QVariantMap l = m_locals[i].toMap();
        if (l.value(QStringLiteral("priority")).toInt() != m_activeLocal) continue;
        QVariantList sh = l.value(QStringLiteral("shapes")).toList();
        if (index < 0 || index >= sh.size()) break;
        QVariantMap m = sh[index].toMap();
        if (size > 0) m[QStringLiteral("size")] = size;
        if (hardness > 0) m[QStringLiteral("hardness")] = hardness;
        if (flow >= 0) m[QStringLiteral("flow")] = flow;
        sh[index] = m; l[QStringLiteral("shapes")] = sh; m_locals[i] = l;
    }
    emit localsChanged();
    QMetaObject::invokeMethod(m_worker, "setBrush", Qt::QueuedConnection, Q_ARG(int, m_imgid), Q_ARG(int, m_activeLocal), Q_ARG(int, index),
                              Q_ARG(double, size), Q_ARG(double, hardness), Q_ARG(double, flow));
    refresh();
    requestRender();
}

void EngineService::renameLocal(int priority, const QString &name) {
    editBoundary();
    if (m_imgid < 0 || name.trimmed().isEmpty()) return;
    m_dirty = true;
    for (int i = 0; i < m_locals.size(); ++i) {
        QVariantMap l = m_locals[i].toMap();
        if (l.value(QStringLiteral("priority")).toInt() == priority) { l[QStringLiteral("name")] = name.trimmed(); m_locals[i] = l; }
    }
    emit localsChanged();
    QMetaObject::invokeMethod(m_worker, "renameLocal", Qt::QueuedConnection, Q_ARG(int, m_imgid), Q_ARG(int, priority), Q_ARG(QString, name.trimmed()));
    refresh();
}

void EngineService::copyMask() {
    for (const QVariant &v : m_locals) {
        QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("priority")).toInt() != m_activeLocal) continue;
        m.remove(QStringLiteral("priority"));
        m[QStringLiteral("local")] = true;
        m_maskClipboard = m;
        m_clipW = renderWidth(); m_clipH = renderHeight();
        emit maskClipboardChanged();
        setStatus(tr("Copied the mask of %1").arg(m.value(QStringLiteral("name")).toString()));
        return;
    }
}

void EngineService::pasteMask() {
    editBoundary();
    beginUndoGroup();
    const auto grouped = qScopeGuard([this] { endUndoGroup(); });
    if (m_imgid < 0 || m_maskClipboard.isEmpty()) return;
    m_dirty = true;
    QMetaObject::invokeMethod(m_worker, "pasteLocal", Qt::QueuedConnection, Q_ARG(int, m_imgid), Q_ARG(QVariantMap, m_maskClipboard));
    refresh();
    requestRender();
    // Shapes are fractions of the frame, so a different aspect ratio
    // stretches them: say so, and leave the review to the eye.
    const double from = m_clipH > 0 ? double(m_clipW) / m_clipH : 0, to = renderHeight() > 0 ? double(renderWidth()) / renderHeight() : 0;
    if (from > 0 && to > 0 && qAbs(from - to) > 0.02)
        setStatus(tr("Mask pasted onto a differently shaped photo — check the shapes' positions"));
    else
        setStatus(tr("Mask pasted"));
}

// Several items from one action (a preset, a tone-mapper switch, a section
// reset) are one undo step: the ends inside a group are skipped. History's
// own rows still reach every item.
void EngineService::beginUndoGroup() {
    if (m_imgid < 0 || m_undoDepth++ > 0) return;
    ++m_marksInFlight;
    QMetaObject::invokeMethod(m_worker, "markUndo", Qt::QueuedConnection, Q_ARG(int, m_imgid), Q_ARG(bool, true));
}
void EngineService::endUndoGroup() {
    if (m_undoDepth <= 0 || --m_undoDepth > 0 || m_imgid < 0) return;
    ++m_marksInFlight;
    QMetaObject::invokeMethod(m_worker, "markUndo", Qt::QueuedConnection, Q_ARG(int, m_imgid), Q_ARG(bool, false));
}
// From a jump still on its way (the read-back lags a render), so quick
// presses each take one more step instead of repeating the same one.
void EngineService::releaseHeldSteps() {
    if (m_marksInFlight > 0 || m_auto.waiting || m_previewParamsDeferred
        || m_receivedParams != m_requestedParams || m_heldSteps == 0) return;
    const int held = m_heldSteps; m_heldSteps = 0;
    for (int i = 0; i < qAbs(held); ++i) held > 0 ? undo() : redo();
}

void EngineService::undo() {
    // Stop a held gesture and read its committed history before choosing a
    // step. The same wait covers Undo immediately after a normal release.
    if (m_previewParamsDeferred) {
        const auto owners = m_previewEditors.keys();
        for (auto *owner : owners) setPreviewEditing(owner, false);
        emit editStateReplaced();
    }
    // A group's end is still on its way back from the worker: until it
    // lands, its inner steps are not known and Undo would take one of them.
    if (m_marksInFlight > 0 || m_auto.waiting || m_receivedParams != m_requestedParams) { ++m_heldSteps; return; }
    int end = (m_jumpTarget >= 0 ? m_jumpTarget : m_historyEnd) - 1;
    while (end > 0 && m_innerSteps.contains(end)) --end;
    if (end >= 0) jumpToHistory(end);
}
void EngineService::redo() {
    if (m_previewParamsDeferred) {
        const auto owners = m_previewEditors.keys();
        for (auto *owner : owners) setPreviewEditing(owner, false);
        emit editStateReplaced();
    }
    if (m_marksInFlight > 0 || m_auto.waiting || m_receivedParams != m_requestedParams) { --m_heldSteps; return; }
    const int count = m_history.size();
    int end = (m_jumpTarget >= 0 ? m_jumpTarget : m_historyEnd) + 1;
    while (end < count && m_innerSteps.contains(end)) ++end;
    if (end <= count) jumpToHistory(end);
}

// Where one History step ends and the next begins. The engine folds an
// edit into the step before it when it is the same adjustment, which is
// right for the samples of one drag and wrong for anything else: Auto and
// then the Exposure slider, or a reset and then its curve, would be one
// step and one Undo would take both. A keyed edit (a slider, a curve point)
// continues its step only while the same control keeps moving; an edit
// without a key is always a step of its own.
QString EngineService::groupKey(const QString &path, int variant) { return path + QLatin1Char('#') + QString::number(variant); }

// The open photo's undo groups, kept beside the engine library so Undo
// takes a reset or a preset back whole after the photo is reopened, even in
// a later session.
void EngineService::storeGroups() {
    if (m_path.isEmpty() || m_groupsPath.isEmpty()) return;
    QList<int> steps(m_innerSteps.cbegin(), m_innerSteps.cend());
    std::sort(steps.begin(), steps.end());
    QVariantList list; for (int i : steps) list << i;
    const QString key = groupKey(m_path, m_variant);
    if (list.isEmpty() ? !m_savedGroups.contains(key) : m_savedGroups.value(key).toList() == list) return;
    if (list.isEmpty()) m_savedGroups.remove(key); else m_savedGroups[key] = list;
    writeGroupsFile();
}

void EngineService::writeGroupsFile() {
    if (m_groupsPath.isEmpty()) return;
    QSaveFile f(m_groupsPath);
    if (f.open(QIODevice::WriteOnly)) { f.write(QJsonDocument(QJsonObject::fromVariantMap(m_savedGroups)).toJson(QJsonDocument::Compact)); f.commit(); }
}

void EngineService::relocateGroups(const QString &oldPath, const QString &newPath, bool folder) {
    storeGroups();
    const auto movedPath = [&](const QString &path) {
        if (path == oldPath) return newPath;
        if (folder && path.startsWith(oldPath + QLatin1Char('/'))) return newPath + path.mid(oldPath.size());
        return path;
    };
    QVariantMap next = m_savedGroups, relocated;
    for (auto it = m_savedGroups.cbegin(); it != m_savedGroups.cend(); ++it) {
        const QString prefix = it.key().startsWith(QLatin1String("lab|")) ? QStringLiteral("lab|") : QString();
        const QString key = it.key().mid(prefix.size());
        const qsizetype separator = key.lastIndexOf(QLatin1Char('#'));
        if (separator < 0) continue;
        const QString path = key.left(separator), target = movedPath(path);
        if (target == path) continue;
        next.remove(it.key());
        relocated[prefix + target + key.mid(separator)] = it.value();
    }
    // Stage both removals and insertions so overlapping folder names cannot
    // erase an entry that was just moved by an earlier iteration.
    for (auto it = relocated.cbegin(); it != relocated.cend(); ++it) next[it.key()] = it.value();
    if (next != m_savedGroups) { m_savedGroups = next; writeGroupsFile(); }
    m_lastAuto.path = movedPath(m_lastAuto.path);
}

void EngineService::copyGroups(const QString &path, int from, int to, bool swap) {
    if (from == to) return;
    storeGroups();
    const QVariantMap before = m_savedGroups;
    const auto transfer = [&](const QString &source, const QString &target) {
        if (before.contains(source)) m_savedGroups[target] = before.value(source);
        else m_savedGroups.remove(target);
    };
    for (const QString &prefix : {QString(), QStringLiteral("lab|")}) {
        const QString a = prefix + groupKey(path, from), b = prefix + groupKey(path, to);
        transfer(a, b);
        if (swap) transfer(b, a);
    }
    if (before != m_savedGroups) writeGroupsFile();
    if (m_lastAuto.path == path) {
        if (swap && m_lastAuto.variant == from) m_lastAuto.variant = to;
        else if (m_lastAuto.variant == to) {
            if (swap) m_lastAuto.variant = from;
            else m_lastAuto = {};
        }
    }
}

void EngineService::forgetGroups(const QString &path, int variant) {
    if (path == m_path && variant == m_variant) {
        emit editStateReplaced();
        m_innerSteps.clear(); m_groupFrom = -1; m_jumpTarget = -1; m_readIsJump = false;
        m_labMasterKnown = false; m_labPending = false;
        cancelAuto();
        m_heldSteps = 0;
    }
    const QString key = groupKey(path, variant);
    const bool groups = m_savedGroups.remove(key) > 0;
    const bool lab = m_savedGroups.remove(QStringLiteral("lab|") + key) > 0;
    if (groups || lab) writeGroupsFile();
    if (m_lastAuto.path == path && m_lastAuto.variant == variant) m_lastAuto = {};
}

// The Lab master slider at a history end, for the open photo (see
// refreshLabColour); kept with the undo groups.
void EngineService::recordLabMaster(int end, double master) {
    if (m_path.isEmpty()) return;
    const QString key = QStringLiteral("lab|") + groupKey(m_path, m_variant);
    QVariantMap masters = m_savedGroups.value(key).toMap();
    masters[QString::number(end)] = master;
    m_savedGroups[key] = masters;
    writeGroupsFile();
}
void EngineService::pruneLabMasters(int end) {
    const QString key = QStringLiteral("lab|") + groupKey(m_path, m_variant);
    if (!m_savedGroups.contains(key)) return;
    QVariantMap masters = m_savedGroups.value(key).toMap();
    bool changed = false;
    for (auto it = masters.begin(); it != masters.end();) { if (it.key().toInt() > end) { it = masters.erase(it); changed = true; } else ++it; }
    if (!changed) return;
    if (masters.isEmpty()) m_savedGroups.remove(key); else m_savedGroups[key] = masters;
    writeGroupsFile();
}

void EngineService::editBoundary(const QString &key) {
    if (m_imgid < 0) return;
    if (!m_autoApplying) {
        cancelAuto();
        // Native edits can merge or renumber rows. A numeric history end
        // alone does not identify the result once another edit is submitted.
        m_lastAuto = {};
    }
    const bool continues = !key.isEmpty() && key == m_editKey && m_editClock.isValid() && m_editClock.elapsed() < 700;
    m_editKey = key; m_editClock.restart();
    if (continues) return;
    QMetaObject::invokeMethod(m_worker, "beginEdit", Qt::QueuedConnection, Q_ARG(int, m_imgid));
}

void EngineService::resetTo(bool start) {
    if (m_imgid < 0) return;
    // A newer replacement supersedes Undo/Redo held for an earlier group.
    // Otherwise the old key press would undo this reset when its mark lands.
    m_heldSteps = 0;
    emit editStateReplaced();
    cancelAuto();
    m_undoDepth = 0;
    m_lastAuto = {};
    m_editKey.clear();
    // The Lab master is remembered only while its slider is being used: from
    // any other state the sliders are read from the curves themselves.
    m_labMasterKnown = false;
    m_dirty = true;
    m_printHeld.clear();
    QMetaObject::invokeMethod(m_worker, "resetHistory", Qt::QueuedConnection, Q_ARG(int, m_imgid), Q_ARG(bool, start));
    refresh();
    requestRender();
}

void EngineService::jumpHistory(int end) {
    m_heldSteps = 0;
    jumpToHistory(end);
}

void EngineService::jumpToHistory(int end) {
    if (m_imgid < 0) return;
    cancelAuto();
    m_editKey.clear();
    // The Lab master is remembered only while its slider is being used: from
    // any other state the sliders are read from the curves themselves.
    m_labMasterKnown = false;
    m_jumpTarget = end;
    emit historyJumped();
    m_dirty = true;
    QMetaObject::invokeMethod(m_worker, "setHistoryEnd", Qt::QueuedConnection, Q_ARG(int, m_imgid), Q_ARG(int, end));
    ++m_requestedParams;
    QMetaObject::invokeMethod(m_worker, "readParamsAfterJump", Qt::QueuedConnection,
                              Q_ARG(int, m_imgid), Q_ARG(QVariantList, curatedParams()), Q_ARG(int, end));
    requestRender();
}

void EngineService::exportJpeg(const QString &path, int maxEdge, int quality) {
    if (m_imgid < 0) return;
    setStatus(tr("Exporting…"));
    QMetaObject::invokeMethod(m_worker, "exportJpeg", Qt::QueuedConnection,
                              Q_ARG(int, m_imgid), Q_ARG(QString, path), Q_ARG(int, maxEdge), Q_ARG(int, quality));
}

QVariantList EngineService::exportFormats() const {
    static const QHash<QString, QString> labels = {
        {QStringLiteral("jpeg"), QStringLiteral("JPEG")}, {QStringLiteral("tiff"), QStringLiteral("TIFF")},
        {QStringLiteral("png"), QStringLiteral("PNG")}, {QStringLiteral("webp"), QStringLiteral("WebP")},
        {QStringLiteral("avif"), QStringLiteral("AVIF")}, {QStringLiteral("jpegxl"), QStringLiteral("JPEG XL")},
        {QStringLiteral("psd"), QStringLiteral("PSD")}};
    QVariantList out;
    for (const QString &f : m_formats) {
        const QString id = f.section(QLatin1Char(':'), 0, 0), ext = f.section(QLatin1Char(':'), 1);
        QVariantMap m;
        m[QStringLiteral("id")] = id;
        m[QStringLiteral("label")] = labels.value(id, id);
        m[QStringLiteral("ext")] = ext;
        m[QStringLiteral("quality")] = id != QLatin1String("tiff") && id != QLatin1String("png") && id != QLatin1String("psd");
        m[QStringLiteral("bpp")] = id == QLatin1String("tiff") || id == QLatin1String("png") || id == QLatin1String("psd");
        out << m;
    }
    return out;
}

int EngineService::metaFlagsFor(bool exif, bool location, bool keywords, bool history) {
#ifdef OMARAW_ENGINE
    int f = OMA_META_METADATA;
    if (exif) f |= OMA_META_EXIF;
    if (location) f |= OMA_META_GEOTAG;
    if (keywords) f |= OMA_META_TAG;
    if (history) f |= OMA_META_HISTORY;
    return f;
#else
    Q_UNUSED(exif) Q_UNUSED(location) Q_UNUSED(keywords) Q_UNUSED(history)
    return -1;
#endif
}

void EngineService::exportBatch(const QStringList &paths, const QString &folder, int maxEdge, int quality, const QString &suffix,
                                const QString &format, int bpp, int metaFlags) {
    QVariantList items;
    for (const QString &p : paths) { QVariantMap m; m[QStringLiteral("path")] = p; m[QStringLiteral("variant")] = 0; items << m; }
    exportBatchItems(items, folder, maxEdge, quality, suffix, format, bpp, metaFlags);
}

static QString safeName(QString s) {
    s.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._ -]+")), QStringLiteral("_"));
    return s.trimmed();
}

QString EngineService::exportName(const QString &pattern, const QVariantMap &item, int seq) {
    const QString path = item.value(QStringLiteral("path")).toString();
    const QFileInfo fi(path);
    const int variant = item.value(QStringLiteral("variant")).toInt();
    // The variant's name becomes part of the file name; "-vN" when unnamed.
    QString tag;
    if (variant) {
        tag = safeName(item.value(QStringLiteral("name")).toString()).replace(QLatin1Char(' '), QLatin1Char('_'));
        if (tag.isEmpty()) tag = QStringLiteral("v%1").arg(variant);
    }
    if (!pattern.contains(QLatin1Char('{'))) return (fi.completeBaseName() + (tag.isEmpty() ? QString() : QLatin1Char('-') + tag) + pattern).replace('/', '_').replace('\\', '_');
    QDateTime when = QDateTime::fromString(item.value(QStringLiteral("capturedAt")).toString(), Qt::ISODate);
    if (!when.isValid() && fi.exists()) when = fi.lastModified();
    const QString camera = safeName((item.value(QStringLiteral("make")).toString() + QLatin1Char(' ') + item.value(QStringLiteral("model")).toString()).simplified());
    QString out = pattern;
    out.replace(QLatin1String("{name}"), fi.completeBaseName());
    out.replace(QLatin1String("{tag}"), tag);
    out.replace(QLatin1String("{seq}"), QStringLiteral("%1").arg(seq, 3, 10, QLatin1Char('0')));
    out.replace(QLatin1String("{date}"), when.isValid() ? when.toString(QStringLiteral("yyyy-MM-dd")) : QString());
    out.replace(QLatin1String("{time}"), when.isValid() ? when.toString(QStringLiteral("HH-mm-ss")) : QString());
    out.replace(QLatin1String("{camera}"), camera);
    out.replace(QLatin1String("{rating}"), QString::number(item.value(QStringLiteral("rating")).toInt()));
    out.replace(QLatin1String("{folder}"), safeName(fi.dir().dirName()));
    out = safeName(out);
    // A pattern that resolved to nothing (or only punctuation) falls back to the original name.
    if (out.isEmpty() || !out.contains(QRegularExpression(QStringLiteral("[A-Za-z0-9]")))) out = fi.completeBaseName();
    return out;
}

QSize EngineService::exportSize(const QVariantMap &resize, int maxEdge, int w, int h, int orientation) {
    const QString mode = resize.value(QStringLiteral("mode"), QStringLiteral("long")).toString();
    const double v = resize.contains(QStringLiteral("value")) ? resize.value(QStringLiteral("value")).toDouble() : maxEdge;
    if (orientation >= 5 && orientation <= 8) std::swap(w, h);   // the file's pixels stand on their side
    if (mode == QLatin1String("none") || v <= 0) return QSize(0, 0);
    if (mode == QLatin1String("width")) return QSize(int(v), 0);
    if (mode == QLatin1String("height")) return QSize(0, int(v));
    if (mode == QLatin1String("short")) {
        if (w <= 0 || h <= 0) return QSize(int(v), int(v));
        return w >= h ? QSize(0, int(v)) : QSize(int(v), 0);
    }
    if (mode == QLatin1String("megapixels") || mode == QLatin1String("percent")) {
        if (w <= 0 || h <= 0) return QSize(0, 0);
        const double scale = mode == QLatin1String("percent") ? v / 100.0 : std::sqrt(v * 1e6 / (double(w) * h));
        if (scale >= 1.0) return QSize(0, 0);                       // never upsized
        return QSize(qMax(1, int(std::lround(w * scale))), qMax(1, int(std::lround(h * scale))));
    }
    return QSize(int(v), int(v));                                    // long edge
}

void EngineService::retryFailed() {
    int n = 0;
    for (int i = 0; i < m_queue.size(); ++i) {
        QVariantMap row = m_queue[i].toMap();
        const QString st = row.value(QStringLiteral("status")).toString();
        if (st != QLatin1String("failed") && st != QLatin1String("cancelled")) continue;
        row[QStringLiteral("status")] = QStringLiteral("queued"); row[QStringLiteral("error")] = QString(); row[QStringLiteral("out")] = QString();
        m_queue[i] = row; ++n;
    }
    if (!n) return;
    m_exportFailed = qMax(0, m_exportFailed - n);
    m_cancelExport = false;
    notifyExportQueue();
    if (!m_exporting) exportNext();
}

void EngineService::removeQueueRow(int index) {
    if (index < 0 || index >= m_queue.size()) return;
    const QVariantMap row = m_queue[index].toMap();
    const QString st = row.value(QStringLiteral("status")).toString();
    if (st == QLatin1String("rendering")) return;
    if (st == QLatin1String("done")) m_exportDone = qMax(0, m_exportDone - 1);
    if (st == QLatin1String("failed")) m_exportFailed = qMax(0, m_exportFailed - 1);
    m_queue.removeAt(index);
    notifyExportQueue();
}

void EngineService::moveQueueRow(int index, int delta) {
    const int to = index + delta;
    if (index < 0 || index >= m_queue.size() || to < 0 || to >= m_queue.size()) return;
    if (m_queue[index].toMap().value(QStringLiteral("status")) != QLatin1String("queued") || m_queue[to].toMap().value(QStringLiteral("status")) != QLatin1String("queued")) return;
    m_queue.swapItemsAt(index, to);
    notifyExportQueue();
}

void EngineService::duplicateQueueRow(int index) {
    if (index < 0 || index >= m_queue.size()) return;
    QVariantMap row = m_queue[index].toMap();
    row[QStringLiteral("status")] = QStringLiteral("queued"); row[QStringLiteral("error")] = QString(); row[QStringLiteral("out")] = QString();
    m_queue.insert(index + 1, row);
    notifyExportQueue();
    if (!m_exporting) exportNext();
}

QString EngineService::outputProfileError(int profile, const QString &path, int bpp) const {
    if (profile < 0 || profile > 3) return tr("Choose an output profile");
    // Linear encoding spends most of 256 levels on the highlights; an 8-bit
    // file in it bands visibly in the shadows.
    if (profile == 2 && bpp < 16) return tr("Linear ProPhoto RGB needs 16 bits: choose 16 bit, or another profile for an 8-bit file");
    QString error;
    if (profile == 3) Proof::readRgbProfile(path, &error);
    return error;
}

void EngineService::exportBatchItems(const QVariantList &items, const QString &folder, int maxEdge, int quality, const QString &suffix,
                                     const QString &format, int bpp, int metaFlags, const QVariantMap &finishing, const QVariantMap &options) {
    if (!m_ready || items.isEmpty()) { setStatus(tr("Nothing to export")); return; }
    const int profile = options.value(QStringLiteral("outputProfile")).toInt();
    const int intent = options.value(QStringLiteral("outputIntent")).toInt();
    QString error = outputProfileError(profile, options.value(QStringLiteral("outputIcc")).toString(), bpp);
    QVariantMap colour{{QStringLiteral("profile"), profile}, {QStringLiteral("intent"), intent}};
    if (profile == 3) colour[QStringLiteral("icc")] = Proof::readRgbProfile(options.value(QStringLiteral("outputIcc")).toString(), &error);
    if (intent < 0 || intent > 3) error = tr("Choose a rendering intent");
    // A watermark picture that has moved or cannot be read would otherwise
    // be left off every file in the batch without a word.
    const QString mark = finishing.value(QStringLiteral("imagePath")).toString();
    if (error.isEmpty() && !mark.isEmpty() && QImageReader(mark).size().isEmpty())
        error = tr("the watermark picture %1 cannot be read").arg(QFileInfo(mark).fileName());
    if (!error.isEmpty()) { setStatus(tr("Export: %1").arg(error)); return; }
    m_exportFormat = format; m_exportBpp = bpp; m_exportMeta = metaFlags; m_exportFinishing = finishing; m_exportOptions = options;
    const QVariantMap resize = options;
    const QString absoluteFolder = QFileInfo(folder).absoluteFilePath();
    QDir().mkpath(absoluteFolder);
    int seq = 0;
    for (const QVariant &it : items) {
        const QVariantMap m = it.toMap();
        const QString p = QFileInfo(m.value(QStringLiteral("path")).toString()).absoluteFilePath();
        const int variant = m.value(QStringLiteral("variant")).toInt();
        QString tag;
        if (variant) { tag = safeName(m.value(QStringLiteral("name")).toString()).replace(QLatin1Char(' '), QLatin1Char('_')); if (tag.isEmpty()) tag = QStringLiteral("v%1").arg(variant); }
        QVariantMap row;
        row[QStringLiteral("folder")] = absoluteFolder;
        row[QStringLiteral("format")] = format;
        row[QStringLiteral("quality")] = quality;
        row[QStringLiteral("bpp")] = bpp;
        row[QStringLiteral("meta")] = metaFlags;
        row[QStringLiteral("finishing")] = finishing;
        row[QStringLiteral("options")] = options;
        row[QStringLiteral("colour")] = colour;
        row[QStringLiteral("file")] = p;
        row[QStringLiteral("variant")] = variant;
        row[QStringLiteral("tag")] = tag;
        row[QStringLiteral("stem")] = exportName(suffix, m, ++seq);
        QVariantMap describe;
        for (const char *k : {"title", "caption", "copyright", "creator", "keywords"}) if (m.contains(QLatin1String(k))) describe[QLatin1String(k)] = m.value(QLatin1String(k));
        if (options.value(QStringLiteral("ppi")).toInt() > 0) describe[QStringLiteral("ppi")] = options.value(QStringLiteral("ppi")).toInt();
        row[QStringLiteral("describe")] = describe;
        row[QStringLiteral("rating")] = m.value(QStringLiteral("rating")).toInt();
        const QSize sz = exportSize(resize, maxEdge, m.value(QStringLiteral("width")).toInt(), m.value(QStringLiteral("height")).toInt(), m.value(QStringLiteral("orientation"), 1).toInt());
        row[QStringLiteral("maxW")] = sz.width(); row[QStringLiteral("maxH")] = sz.height();
        row[QStringLiteral("name")] = QFileInfo(p).fileName() + (variant ? QStringLiteral(" (%1)").arg(tag) : QString());
        row[QStringLiteral("status")] = QStringLiteral("queued");
        row[QStringLiteral("out")] = QString();
        row[QStringLiteral("error")] = QString();
        m_queue << row;
    }
    m_exportFolder = folder; m_exportSuffix = suffix; m_exportEdge = maxEdge; m_exportQuality = quality;
    m_cancelExport = false;
    notifyExportQueue();
    if (!m_exporting) exportNext();
}

bool EngineService::saveExportQueue() {
    if (m_queueUnreadable) return false;
    if (m_queuePath.isEmpty()) return true;
    return ExportState::save(m_queuePath, m_queue, &m_queueError);
}

void EngineService::notifyExportQueue(bool persist) {
    m_exportDone = m_exportFailed = 0;
    for (const auto &value : std::as_const(m_queue)) {
        const QString status = value.toMap().value("status").toString();
        if (status == "done") ++m_exportDone;
        if (status == "failed") ++m_exportFailed;
    }
    if (persist && !saveExportQueue()) m_exportPaused = true;
    emit exportQueueChanged();
}

void EngineService::pauseExport() {
    m_exportPaused = true;
    notifyExportQueue();
    setStatus(m_exporting ? tr("Pausing after the current photo…") : tr("Export queue paused"));
}

void EngineService::resumeExport() {
    if (!m_ready || m_formats.isEmpty()) { setStatus(tr("Wait for the export engine to become ready")); return; }
    if (!saveExportQueue()) { notifyExportQueue(); setStatus(m_queueError); return; }
    m_exportPaused = false;
    m_cancelExport = false;
    notifyExportQueue();
    if (!m_exporting) exportNext();
}

void EngineService::exportNext() {
    const bool pending = std::any_of(m_queue.cbegin(), m_queue.cend(), [](const QVariant &v) { return v.toMap().value("status") == "queued"; });
    if (pending && m_exportPaused && !m_cancelExport) {
        m_exporting = false;
        notifyExportQueue();
        setStatus(m_queueError.isEmpty() ? tr("Export queue paused — Resume to continue") : m_queueError);
        return;
    }
    if (pending && (!m_ready || m_formats.isEmpty())) return;
    // Next queued row, in order.
    for (int i = 0; i < m_queue.size(); ++i) {
        QVariantMap row = m_queue[i].toMap();
        if (row.value(QStringLiteral("status")) != QLatin1String("queued")) continue;
        if (m_cancelExport) { row[QStringLiteral("status")] = QStringLiteral("cancelled"); m_queue[i] = row; continue; }
        // Checked again here, not only when queued: a queue resumed after a
        // relaunch may point at a watermark picture that has since gone.
        const QString mark = row.value(QStringLiteral("finishing")).toMap().value(QStringLiteral("imagePath")).toString();
        if (!mark.isEmpty() && QImageReader(mark).size().isEmpty()) {
            row[QStringLiteral("status")] = QStringLiteral("failed");
            row[QStringLiteral("error")] = tr("the watermark picture %1 cannot be read").arg(QFileInfo(mark).fileName());
            m_queue[i] = row; ++m_exportFailed; m_exporting = true;
            continue;
        }
        row[QStringLiteral("status")] = QStringLiteral("rendering");
        m_queue[i] = row;
        m_exporting = true;
        const QString src = row.value(QStringLiteral("file")).toString();
        const int variant = row.value(QStringLiteral("variant")).toInt();
        // Never clobber: a taken name gets a counter. The storage appends
        // the format's extension, so the check uses the same one.
        QString ext = QStringLiteral("jpg");
        const QString format = row.value(QStringLiteral("format"), m_exportFormat).toString();
        for (const QString &f : m_formats) if (f.startsWith(format + QLatin1Char(':'))) ext = f.section(QLatin1Char(':'), 1);
        const QString base = row.value(QStringLiteral("folder"), m_exportFolder).toString() + QLatin1Char('/') + row.value(QStringLiteral("stem")).toString();
        QString stem = base;
        for (int n = 2; QFile::exists(stem + QLatin1Char('.') + ext); ++n) stem = base + QStringLiteral("-%1").arg(n);
        row[QStringLiteral("out")] = stem + QLatin1Char('.') + ext;
        row[QStringLiteral("error")] = QString();
        m_queue[i] = row;
        // Journal the exact attempt before any encoder can write a file.
        if (!saveExportQueue()) {
            row[QStringLiteral("status")] = QStringLiteral("queued");
            m_queue[i] = row;
            m_exporting = false;
            m_exportPaused = true;
            notifyExportQueue();
            setStatus(m_queueError);
            return;
        }
        notifyExportQueue(false);
        setStatus(tr("Exporting %1…").arg(QFileInfo(src).fileName()));
        QMetaObject::invokeMethod(m_worker, "exportFile", Qt::QueuedConnection,
                                  Q_ARG(QString, src), Q_ARG(int, variant), Q_ARG(QString, stem), Q_ARG(QString, format),
                                  Q_ARG(int, row.contains(QStringLiteral("maxW")) ? row.value(QStringLiteral("maxW")).toInt() : m_exportEdge),
                                  Q_ARG(int, row.contains(QStringLiteral("maxH")) ? row.value(QStringLiteral("maxH")).toInt() : m_exportEdge),
                                  Q_ARG(int, row.value(QStringLiteral("quality"), m_exportQuality).toInt()), Q_ARG(int, row.value(QStringLiteral("bpp"), m_exportBpp).toInt()), Q_ARG(int, row.value(QStringLiteral("meta"), m_exportMeta).toInt()),
                                  Q_ARG(QVariantMap, row.value(QStringLiteral("finishing")).toMap()), Q_ARG(QVariantMap, row.value(QStringLiteral("describe")).toMap()), Q_ARG(QVariantMap, row.value(QStringLiteral("colour")).toMap()));
        return;
    }
    const bool was = m_exporting;
    m_exporting = false;
    notifyExportQueue();
    if (was) {
        setStatus(m_exportFailed ? tr("Export finished: %1 done, %2 failed").arg(m_exportDone).arg(m_exportFailed)
                                 : tr("Exported %1 photos to %2").arg(m_exportDone).arg(m_exportFolder));
        emit exportBatchFinished(m_exportDone, m_exportFailed);
    }
}

QString EngineService::proofProfileNameOf(const QString &path) const {
    QString p = path;
    if (p.startsWith(QLatin1String("file:"))) p = QUrl(p).toLocalFile();
    return Proof::profileName(p);
}

void EngineService::cancelExport() {
    m_cancelExport = true;
    m_exportPaused = false;
    for (auto &value : m_queue) {
        auto row = value.toMap();
        if (row.value("status") == "queued") { row.insert("status", "cancelled"); value = row; }
    }
    notifyExportQueue();
    if (!m_exporting) exportNext();
}

void EngineService::clearExportQueue() {
    if (m_exporting) return;
    if (m_queueUnreadable && QFile::exists(m_queuePath)) {
        const QString saved = m_queuePath + QStringLiteral(".unreadable-") + QString::number(QDateTime::currentMSecsSinceEpoch());
        if (!QFile::rename(m_queuePath, saved)) { setStatus(tr("Cannot preserve the unreadable queue file")); return; }
    }
    m_queueUnreadable = false; m_exportPaused = false; m_cancelExport = false;
    m_queue.clear(); m_exportDone = 0; m_exportFailed = 0;
    notifyExportQueue();
}

// ── settings across photos ──────────────────────────────────────────────
// The groups a copy / sync dialog offers. `defaultOn` is what the dialog
// ticks to begin with: crop and retouch spots are about one frame's
// content, so they start off.
QVariantList EngineService::settingsGroups() {
    struct G { const char *key, *label; bool defaultOn; };
    static const G groups[] = {
        {"wb", "White balance", true}, {"basic", "Basic tone", true}, {"curve", "Tone curves", true},
        {"profile", "Creative profile", true}, {"color", "Colour", true}, {"detail", "Detail", true}, {"film", "Film", true}, {"lens", "Lens and geometry", true},
        {"crop", "Crop and straighten", false}, {"local", "Local adjustments", true}, {"retouch", "Retouch spots", false},
    };
    QVariantList out;
    for (const G &g : groups)
        out << QVariantMap{{QStringLiteral("key"), QString::fromLatin1(g.key)}, {QStringLiteral("label"), tr(g.label)}, {QStringLiteral("defaultOn"), g.defaultOn}};
    return out;
}

// Which settings group a saved-state row (a currentValues row) belongs to.
QString EngineService::groupOf(const QVariantMap &row) {
    if(row.value("liquifyset").toBool()) return "retouch";
    if(row.value("rasterset").toBool()) return "local";
    if (row.value(QStringLiteral("local")).toBool() || row.value(QStringLiteral("localset")).toBool()) return QStringLiteral("local");
    if (row.value(QStringLiteral("spotset")).toBool()) return QStringLiteral("retouch");
    if (row.value(QStringLiteral("curveset")).toBool() || row.value(QStringLiteral("tonecurveset")).toBool()) return QStringLiteral("curve");
    if (row.value(QStringLiteral("zoneset")).toBool()) return QStringLiteral("color");
    const QString op = row.value(QStringLiteral("op")).toString(), field = row.value(QStringLiteral("field")).toString();
    if (op == QLatin1String("omarawprofile")) return QStringLiteral("profile");
    if (op == QLatin1String("omarawprint")) return QStringLiteral("film");
    if (op == QLatin1String("colorbalancergb")) return QStringLiteral("color");
    if (op == QLatin1String("crop") || (op == QLatin1String("ashift") && (field == QLatin1String("rotation") || field == QLatin1String("cropmode")))) return QStringLiteral("crop");
    if (EngineWorker::isWhiteBalance(op, field)) return QStringLiteral("wb");
    static QHash<QString, QString> byRow;
    if (byRow.isEmpty()) {
        for (const QVariant &v : curatedParams()) {
            const QVariantMap c = v.toMap();
            byRow.insert(c.value(QStringLiteral("op")).toString() + QLatin1Char('.') + c.value(QStringLiteral("field")).toString(),
                         c.value(QStringLiteral("group")).toString().toLower());
        }
    }
    return byRow.value(op + QLatin1Char('.') + field, QStringLiteral("basic"));
}

QVariantList EngineService::valuesFor(const QStringList &groups) const {
    QVariantList out;
    for (const QVariant &v : currentValues(true)) if (groups.contains(groupOf(v.toMap()))) out << v;
    return out;
}

QString EngineService::moduleOf(const QVariantMap &row) {
    if(row.value("liquifyset").toBool()) return "liquify";
    if(row.value("rasterset").toBool()) return "rasterfile";
    if (row.value("spotset").toBool()) return "retouch";
    if (row.value("local").toBool() || row.value("localset").toBool()) return {};
    if (row.value("curveset").toBool()) return QStringLiteral("rgbcurve");
    if (row.value("tonecurveset").toBool()) return QStringLiteral("tonecurve");
    if (row.value("zoneset").toBool()) return QStringLiteral("colorzones");
    return row.value("op").toString();
}

QVariantList EngineService::valuesForModule(const QString &operation) const {
    QVariantList out;
    if (operation.isEmpty()) return out;
    for (const auto &value : currentValues(operation == "retouch")) if (moduleOf(value.toMap()) == operation) out << value;
    return out;
}

QStringList EngineService::clipboardModules() const {
    QStringList operations;
    for (const auto &value : m_settingsClipboard) {
        const auto operation = moduleOf(value.toMap());
        if (!operation.isEmpty() && !operations.contains(operation)) operations << operation;
    }
    return operations;
}

bool EngineService::copySettings(const QStringList &groups) {
    if (m_imgid < 0 || !maintenanceReady() || image().isNull()) { setStatus(tr("Wait for a completed edit before copying settings.")); return false; }
    const auto values = valuesFor(groups);
    if (values.isEmpty()) { setStatus(tr("Choose the settings to copy.")); return false; }
    m_settingsClipboard = values;
    emit settingsClipboardChanged();
    setStatus(tr("Copied %1 settings from %2").arg(m_settingsClipboard.size()).arg(QFileInfo(m_path).fileName()));
    return true;
}

bool EngineService::copyModuleSettings(const QString &operation) {
    if (m_imgid < 0 || !maintenanceReady() || image().isNull()) { setStatus(tr("Wait for a completed edit before copying settings.")); return false; }
    const auto values = valuesForModule(operation);
    if (values.isEmpty()) { setStatus(tr("This adjustment has no settings to copy.")); return false; }
    m_settingsClipboard = values; emit settingsClipboardChanged();
    setStatus(tr("Copied adjustment settings from %1").arg(QFileInfo(m_path).fileName())); return true;
}

// A paste leaves alone an adjustment that is off in what was copied and off
// here too: it would change only hidden values, a History step with
// nothing to see. (A snapshot restore keeps them: it restores exactly.)
QVariantList EngineService::withoutHiddenChanges(const QVariantList &rows) const {
    QSet<QString> offThere;
    for (const QVariant &v : rows) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("local")).toBool() || m.value(QStringLiteral("localset")).toBool()) continue;
        if (m.contains(QStringLiteral("enabled")) && !m.value(QStringLiteral("enabled")).toBool()) offThere << moduleOf(m);
    }
    QVariantList out;
    for (const QVariant &v : rows) {
        const QVariantMap m = v.toMap();
        const QString op = moduleOf(m);
        if (!op.isEmpty() && offThere.contains(op) && !moduleEnabled(op) && !m.value(QStringLiteral("local")).toBool()) continue;
        out << v;
    }
    return out;
}

bool EngineService::pasteModuleSettings(const QString &operation) {
    if (m_imgid < 0 || !maintenanceReady() || image().isNull()) { setStatus(tr("Wait for a completed edit before pasting settings.")); return false; }
    QVariantList values;
    if (!valuesForModule(operation).isEmpty())
        for (const auto &value : m_settingsClipboard) if (moduleOf(value.toMap()) == operation) values << value;
    if (values.isEmpty()) { setStatus(tr("The clipboard has no settings for this adjustment.")); return false; }
    values = withoutHiddenChanges(values);
    if (values.isEmpty()) { setStatus(tr("The copied adjustment is off, as it is here: nothing to paste")); return true; }
    applyValues(values); setStatus(tr("Pasted adjustment settings")); return true;
}

void EngineService::pasteSettings() {
    editBoundary();
    if (m_imgid < 0 || m_settingsClipboard.isEmpty()) return;
    const QVariantList values = withoutHiddenChanges(m_settingsClipboard);
    if (!values.isEmpty()) applyValues(values);
    setStatus(tr("Pasted %1 settings").arg(m_settingsClipboard.size()));
}

void EngineService::requestSidecarEdit(const QString &path, bool raw) {
    if (!m_ready || path.isEmpty()) return;
    QMetaObject::invokeMethod(m_worker, "sidecarEdit", Qt::QueuedConnection, Q_ARG(QString, path), Q_ARG(bool, raw), Q_ARG(QVariantList, curatedParams()));
}

// Edits carried in sidecars, onto their photos: {path, variant, sidecar,
// exact} replaces the history with the sidecar's own (OmaRAW's or
// darktable's); {path, variant, values} applies a converted XMP edit.
void EngineService::applySidecarEdits(const QVariantList &items) {
    if (!m_ready || items.isEmpty()) return;
    if (m_syncPending == 0) { m_syncDone = 0; m_syncFailed = 0; }
    for (const QVariant &v : items) {
        const QVariantMap it = v.toMap();
        const QString path = it.value(QStringLiteral("path")).toString();
        const int variant = it.value(QStringLiteral("variant")).toInt();
        if (path.isEmpty()) continue;
        if (it.value(QStringLiteral("exact")).toBool()) {
            ++m_syncPending;
            QMetaObject::invokeMethod(m_worker, "applySidecarEdit", Qt::QueuedConnection, Q_ARG(QString, path), Q_ARG(int, variant),
                                      Q_ARG(QString, it.value(QStringLiteral("sidecar")).toString()), Q_ARG(int, 1024));
            // Companion state and the open view follow the worker's successful
            // replacement signal. A failed sidecar keeps the existing history.
        } else {
            const QVariantList values = it.value(QStringLiteral("values")).toList();
            if (!values.isEmpty()) applyValuesTo({it}, values);
        }
    }
    emit syncChanged();
    if (m_syncPending == 0) emit syncFinished(m_syncDone, m_syncFailed);
}

void EngineService::applyValuesTo(const QVariantList &items, const QVariantList &values) {
    if (!m_ready || items.isEmpty() || values.isEmpty()) return;
    if (m_syncPending == 0) { m_syncDone = 0; m_syncFailed = 0; }
    for (const QVariant &v : items) {
        const QVariantMap it = v.toMap();
        const QString path = it.value(QStringLiteral("path")).toString();
        const int variant = it.value(QStringLiteral("variant")).toInt();
        if (path.isEmpty()) continue;
        // The open photo takes the main path so the viewer follows.
        if (path == m_path && variant == m_variant && m_imgid >= 0) { applyValues(values); ++m_syncDone; continue; }
        ++m_syncPending;
        QMetaObject::invokeMethod(m_worker, "applyTo", Qt::QueuedConnection, Q_ARG(QString, path), Q_ARG(int, variant), Q_ARG(QVariantList, values), Q_ARG(int, 1024));
    }
    emit syncChanged();
    if (m_syncPending == 0) emit syncFinished(m_syncDone, m_syncFailed);
    else setStatus(m_syncPending == 1 ? tr("Applying settings to one photo…") : tr("Applying settings to %1 photos…").arg(m_syncPending));
}

void EngineService::nudgeValues(const QVariantList &items, const QString &op, const QString &field, double delta, double lo, double hi) {
    nudge(items, op, field, delta, lo, hi, QString());
}
// The Library's quick adjust for a control Develop shows as a ±100 or 0..100
// figure: the step is in those figures, so both read alike.
void EngineService::nudgeFigures(const QVariantList &items, const QString &op, const QString &field, double delta) {
    const QString how = figureDisplay(op, field);
    if (how.isEmpty()) { setStatus(tr("%1 has no figure scale; nudge it in its own units").arg(op + QLatin1Char('.') + field)); return; }
    nudge(items, op, field, delta, -std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), how);
}
void EngineService::nudge(const QVariantList &items, const QString &op, const QString &field, double delta, double lo, double hi, const QString &how) {
    if (!m_ready || items.isEmpty() || op.isEmpty() || field.isEmpty()) return;
    if (m_syncPending == 0) { m_syncDone = 0; m_syncFailed = 0; }
    for (const QVariant &v : items) {
        const QVariantMap it = v.toMap();
        const QString path = it.value(QStringLiteral("path")).toString();
        const int variant = it.value(QStringLiteral("variant")).toInt();
        if (path.isEmpty()) continue;
        if (path == m_path && variant == m_variant && m_imgid >= 0) {
            // The open photo: read the value the panel shows, move it, and
            // let the viewer follow through the ordinary edit path.
            double cur = 0, def = 0, mn = lo, mx = hi;
            bool figures = false, off = false;
            for (const QVariant &pv : m_params) {
                const QVariantMap m = pv.toMap();
                if (m.value(QStringLiteral("op")) != op || m.value(QStringLiteral("field")) != field) continue;
                def = m.value("reset", m.value("def")).toDouble();
                off = !m.value(QStringLiteral("enabled")).toBool();
                cur = off ? offStart(op, field, def) : m.value(QStringLiteral("value")).toDouble();
                const double pmin = m.value(QStringLiteral("min")).toDouble(), pmax = m.value(QStringLiteral("max")).toDouble();
                if (pmax > pmin) { mn = qMax(mn, pmin); mx = qMin(mx, pmax); }
                figures = !how.isEmpty() && pmax > pmin;
                if (figures) { mn = pmin; mx = pmax; }
                break;
            }
            beginUndoGroup();
            editBoundary();
            if (const QString other = offSibling(op, field); off && !other.isEmpty()) setParam(op, other, 0);
            setParam(op, field, figures ? fromFigure(how, toFigure(how, cur, mn, def, mx) + delta, mn, def, mx) : qBound(mn, cur + delta, mx));
            endUndoGroup();
            ++m_syncDone;
            continue;
        }
        ++m_syncPending;
        QMetaObject::invokeMethod(m_worker, "nudge", Qt::QueuedConnection, Q_ARG(QString, path), Q_ARG(int, variant),
                                  Q_ARG(QString, op), Q_ARG(QString, field), Q_ARG(double, delta), Q_ARG(double, lo), Q_ARG(double, hi), Q_ARG(int, 1024), Q_ARG(QString, how));
    }
    emit syncChanged();
    if (m_syncPending == 0) emit syncFinished(m_syncDone, m_syncFailed);
    else setStatus(m_syncPending == 1 ? tr("Adjusting one photo…") : tr("Adjusting %1 photos…").arg(m_syncPending));
}

// QML bindings hear a signal before any connect()ed slot does, so the
// version has to move before the emit, never in a slot of its own.
void EngineService::paramsTouched() {
    ++m_paramsVersion;
    emit paramsChanged();
}

QVariantList EngineService::paramsFor(const QString &op) const {
    if (m_byOpVersion != m_paramsVersion) {
        m_byOp.clear();
        for (const QVariant &v : m_params) m_byOp[v.toMap().value(QStringLiteral("op")).toString()] << v;
        m_byOpVersion = m_paramsVersion;
    }
    return m_byOp.value(op);
}

void EngineService::loadCurveHistogram() {
    if (m_imgid < 0 || !m_ready || m_renderSerial == 0) return;
    // A second render of the whole picture: made once per set of settings
    // (a zoom or pan changes nothing it shows), and never queued behind
    // another, so a drag does not stack them up on the engine's thread.
    if (m_curveHistogramSerial == m_paramsSerial || m_curveHistogramInFlight) return;
    m_curveHistogramSerial = m_paramsSerial;
    m_curveHistogramInFlight = true;
    QMetaObject::invokeMethod(m_worker, "inputHistogram", Qt::QueuedConnection, Q_ARG(int, m_imgid), Q_ARG(QString, QStringLiteral("rgbcurve")), Q_ARG(int, m_paramsSerial));
}

void EngineService::loadLensCandidates() {
    if (m_imgid < 0) return;
    QMetaObject::invokeMethod(m_worker, "lensCandidates", Qt::QueuedConnection, Q_ARG(int, m_imgid));
}

void EngineService::setLensOverride(const QString &lens) {
    editBoundary();
    if (m_imgid < 0) return;
    m_dirty = true;
    QMetaObject::invokeMethod(m_worker, "setLens", Qt::QueuedConnection, Q_ARG(int, m_imgid), Q_ARG(QString, lens.trimmed()));
    refresh();
    requestRender();
}

QVariantList EngineService::curatedParams() {
    // kind: nullptr = slider (a switch for a bool field, a dropdown for an
    // enum); "file" = a text field holding a path, shown as a chooser.
    struct Row { const char *group, *op, *field, *label, *suffix; int decimals; const char *kind; };
    static const Row rows[] = {
        {"Basic", "channelmixerrgb", "temperature", "White balance", " K", 0},
        {"Basic", "channelmixerrgb", "tint", "Tint", "", 0},
        {"Basic", "exposure", "exposure", "Exposure", " EV", 2},
        {"Basic", "exposure", "black", "Black level", "", 3},
        {"Basic", "shadhi", "shadows", "Shadows", "", 0},
        {"Basic", "shadhi", "highlights", "Highlights", "", 0},
        {"Basic", "sigmoid", "middle_grey_contrast", "Contrast", "", 2},
        {"Basic", "sigmoid", "contrast_skewness", "Skew", "", 2},
        {"Basic", "filmicrgb", "white_point_source", "White relative exposure", " EV", 2},
        {"Basic", "filmicrgb", "black_point_source", "Black relative exposure", " EV", 2},
        {"Basic", "highlights", "mode", "Highlight reconstruction", "", 0},
        {"Basic", "highlights", "clip", "Highlight clip", "", 2},
        {"Basic", "hazeremoval", "strength", "Dehaze", "", 2},
        {"Basic", "hazeremoval", "distance", "Dehaze distance", "", 2},
        {"Color", "omarawgrade", "lift_H", "Lift hue", "°", 1},
        {"Color", "omarawgrade", "lift_C", "Lift chroma", "", 3},
        {"Color", "omarawgrade", "lift_Y", "Lift level", "", 3},
        {"Color", "omarawgrade", "gamma_H", "Gamma hue", "°", 1},
        {"Color", "omarawgrade", "gamma_C", "Gamma chroma", "", 3},
        {"Color", "omarawgrade", "gamma_Y", "Gamma level", "", 3},
        {"Color", "omarawgrade", "gain_H", "Gain hue", "°", 1},
        {"Color", "omarawgrade", "gain_C", "Gain chroma", "", 3},
        {"Color", "omarawgrade", "gain_Y", "Gain level", "", 3},
        {"Color", "omarawgrade", "offset_H", "Offset hue", "°", 1},
        {"Color", "omarawgrade", "offset_C", "Offset chroma", "", 3},
        {"Color", "omarawgrade", "offset_Y", "Offset level", "", 3},
        {"Color", "omarawgrade", "lum_mix", "Lum Mix", "", 0},
        {"Color", "colorbalancergb", "saturation_global", "Saturation", "", 2},
        {"Color", "colorbalancergb", "vibrance", "Vibrance", "", 2},
        {"Color", "colorbalancergb", "contrast", "Contrast", "", 2},
        {"Color", "colorbalancergb", "chroma_global", "Chroma", "", 2},
        {"Color", "colorbalancergb", "brilliance_global", "Brilliance", "", 2},
        {"Color", "colorbalancergb", "hue_angle", "Hue shift", "°", 0},
        {"Color", "colorin", "type", "Input profile", "", 0},
        {"Color", "colorin", "intent", "Rendering intent", "", 0},
        {"Color", "colorin", "normalize", "Gamut clipping", "", 0},
        {"Color", "colorin", "type_work", "Working profile", "", 0},
        {"Color", "monochrome", "size", "B&W filter size", "", 2},
        {"Color", "monochrome", "highlights", "B&W highlights", "", 2},
        {"Profile", "omarawprofile", "look", "Profile", "", 0},
        {"Profile", "omarawprofile", "amount", "Amount", "", 0},
        {"Profile", "omarawprofile", "filepath", "Profile LUT", "", 0, "file"},
        {"Profile", "omarawprofile", "colorspace", "LUT colour space", "", 0},
        {"Retouch", "omarawrepair", "file", "AI repairs", "", 0, "file"},
        {"Color", "lut3d", "filepath", "LUT file", "", 0, "file"},
        {"Color", "lut3d", "colorspace", "LUT colour space", "", 0},
        {"Color", "lut3d", "interpolation", "LUT interpolation", "", 0},
        {"Detail", "sharpen", "amount", "Sharpen amount", "", 2},
        {"Detail", "sharpen", "radius", "Sharpen radius", " px", 1},
        {"Detail", "demosaic", "demosaicing_method", "Demosaic method", "", 0},
        {"Detail", "demosaic", "color_smoothing", "Colour smoothing", "", 0},
        {"Detail", "demosaic", "green_eq", "Match greens", "", 0},
        {"Detail", "demosaic", "cs_enabled", "Capture sharpen", "", 0},
        {"Detail", "demosaic", "cs_radius", "Capture sharpen radius", "", 2},
        {"Detail", "denoiseprofile", "mode", "Denoise method", "", 0},
        {"Detail", "denoiseprofile", "strength", "Denoise strength", "", 2},
        {"Detail", "bilat", "detail", "Clarity", "", 2},
        {"Detail", "diffuse", "texture", "Texture", "", 0},
        {"Detail", "hotpixels", "strength", "Hot pixels", "", 2},
        {"Detail", "hotpixels", "threshold", "Hot pixel threshold", "", 3},
        {"Detail", "hotpixels", "markfixed", "Mark fixed pixels", "", 0},
        {"Detail", "cacorrect", "avoidshift", "Chromatic aberration: avoid colour shift", "", 0},
        {"Film", "omarawprint", "stock", "Print stock", "", 0},
        {"Film", "omarawprint", "strength", "Print strength", "", 0},
        {"Film", "omarawprint", "white", "Display white", "", 0},
        {"Film", "omarawprint", "paper", "Paper white", "", 0},
        {"Film", "omarawprint", "trim_r", "Red printer light", "", 2},
        {"Film", "omarawprint", "trim_g", "Green printer light", "", 2},
        {"Film", "omarawprint", "trim_b", "Blue printer light", "", 2},
        {"Film", "omarawprint", "gain_r", "Red density", "", 2},
        {"Film", "omarawprint", "gain_g", "Green density", "", 2},
        {"Film", "omarawprint", "gain_b", "Blue density", "", 2},
        {"Film", "omarawprint", "profile", "Camera profile tables", "", 0, "file"},
        {"Film", "grain", "strength", "Grain", "", 0},
        {"Film", "grain", "scale", "Grain size", "", 1},
        {"Film", "grain", "midtones_bias", "Grain mid-tone bias", "", 0},
        {"Film", "bloom", "strength", "Bloom strength", "", 0},
        {"Film", "bloom", "size", "Bloom size", "", 0},
        {"Film", "bloom", "threshold", "Bloom threshold", "", 0},
        {"Film", "omarawhalation", "strength", "Halation", "", 0},
        {"Film", "omarawhalation", "scatter", "Halation scatter", "", 0},
        {"Film", "omarawhalation", "dye", "Halation dye transmission", "", 0},
        {"Film", "omarawhalation", "boost", "Halation boost", "", 0},
        {"Film", "omarawhalation", "threshold", "Halation threshold", "", 0},
        {"Lens", "lens", "method", "Lens correction method", "", 0},
        {"Lens", "lens", "modify_flags", "Lens corrections", "", 0},
        {"Lens", "lens", "scale", "Lens correction scale", "", 2},
        {"Lens", "flip", "orientation", "Orientation", "", 0},
        {"Lens", "defringe", "op_mode", "Defringe mode", "", 0},
        {"Lens", "defringe", "radius", "Defringe radius", " px", 1},
        {"Lens", "defringe", "thresh", "Defringe threshold", "", 1},
        {"Lens", "ashift", "rotation", "Rotate", "°", 2},
        {"Lens", "ashift", "lensshift_v", "Vertical", "", 2},
        {"Lens", "ashift", "lensshift_h", "Horizontal", "", 2},
        {"Lens", "ashift", "cropmode", "Constrain crop", "", 0},
        {"Lens", "crop", "cx", "Crop left", "", 3},
        {"Lens", "crop", "cy", "Crop top", "", 3},
        {"Lens", "crop", "cw", "Crop right", "", 3},
        {"Lens", "crop", "ch", "Crop bottom", "", 3},
        {"Film", "vignette", "brightness", "Vignette", "", 2},
        {"Film", "vignette", "scale", "Vignette start", " %", 0},
        {"Film", "vignette", "falloff_scale", "Vignette fall-off", " %", 0},
        {"Film", "vignette", "saturation", "Vignette saturation", "", 2},
        {"Film", "vignette", "shape", "Vignette shape", "", 2},
    };
    QVariantList out;
    for (const Row &r : rows) {
        QVariantMap m;
        m[QStringLiteral("group")] = QString::fromLatin1(r.group);
        m[QStringLiteral("op")] = QString::fromLatin1(r.op);
        m[QStringLiteral("field")] = QString::fromLatin1(r.field);
        m[QStringLiteral("label")] = QString::fromLatin1(r.label);
        m[QStringLiteral("suffix")] = QString::fromUtf8(r.suffix);
        m[QStringLiteral("decimals")] = r.decimals;
        if (r.kind) m[QStringLiteral("kind")] = QString::fromLatin1(r.kind);

        if (const QString how = figureDisplay(QString::fromLatin1(r.op), QString::fromLatin1(r.field)); !how.isEmpty())
            m[QStringLiteral("display")] = how;
        // darktable declares no limits for the B&W filter's size (its own
        // mouse wheel keeps it to 0.5..3); an unbounded range cannot be a
        // slider, let alone a 0..100 figure.
        if (!qstrcmp(r.op, "monochrome") && !qstrcmp(r.field, "size")) { m[QStringLiteral("uiMin")] = 0.5; m[QStringLiteral("uiMax")] = 3.0; }
        out << m;
    }
    for (const auto &band : {QPair<const char *, const char *>("red", "Reds"), {"orange", "Oranges"},
                             {"yellow", "Yellows"}, {"green", "Greens"}, {"aqua", "Aquas"},
                             {"blue", "Blues"}, {"purple", "Purples"}, {"magenta", "Magentas"}})
        out << QVariantMap{{"group", "Color"}, {"op", "monochrome"}, {"field", QStringLiteral("mix_") + QLatin1String(band.first)},
            {"label", tr(band.second)}, {"suffix", ""}, {"decimals", 2}, {"display", "strength"}};
    for (const char *band : {"red", "orange", "yellow", "green", "cyan", "blue", "lavender", "magenta"}) {
        for (const char *channel : {"hue", "sat", "bright"}) {
            const QString c = QString::fromLatin1(channel);
            out << QVariantMap{{"group", "Color"}, {"op", "colorequal"}, {"field", c + QLatin1Char('_') + QLatin1String(band)},
                {"label", c == "hue" ? tr("Hue") : c == "sat" ? tr("Saturation") : tr("Brightness")},
                {"suffix", c == "hue" ? QString::fromUtf8("°") : QString()}, {"decimals", c == "hue" ? 0 : 2},
                               {"display", c == "hue" ? QString() : QStringLiteral("strength")}};
        }
    }
    for (const auto &entry : {QPair<const char *, const char *>("threshold", "Protect neutrals"), {"chroma_size", "Analysis radius"},
                             {"param_size", "Effect radius"}, {"smoothing_hue", "Hue smoothing"}, {"use_filter", "Guided filter"}})
        out << QVariantMap{{"group", "Color"}, {"op", "colorequal"}, {"field", QLatin1String(entry.first)},
            {"label", tr(entry.second)}, {"suffix", ""}, {"decimals", 2}};
    out << DevelopTools::parameters();
    out << ImageMatch::controls();
    return out;
}

#include "engineservice.moc"
