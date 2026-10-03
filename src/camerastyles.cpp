#include "camerastyles.h"
#include "cameraprofiles.h"
#include <QDir>
#include <QFile>
#include <QSet>
#include <QXmlStreamReader>
#include <cmath>
#ifdef OMARAW_ENGINE
#include "engine/bridge.h"
#endif

QVariantMap CameraStyles::forCamera(int image, const QString &prefix, const QString &camera, const QString &exifCamera) {
#ifndef OMARAW_ENGINE
    Q_UNUSED(image); Q_UNUSED(prefix); Q_UNUSED(camera); Q_UNUSED(exifCamera);
    return {};
#else
    const QDir directory(prefix + "/share/darktable/styles");
    QString file;
    for (const auto &name : directory.entryList({"darktable_*.dtstyle"}, QDir::Files)) {
        const QString body = name.mid(10, name.size() - 18).replace('_', ' ');
        if (CameraProfiles::matchesCamera(body, camera, exifCamera)) { file = directory.filePath(name); break; }
    }
    if (file.isEmpty()) return {};
    QFile input(file);
    if (!input.open(QIODevice::ReadOnly) || input.size() > 256 * 1024) return {};
    QXmlStreamReader xml(input.readAll());
    QVariantList values{QVariantMap{{"op", "omarawprint"}, {"enabled", false}}};
    const QStringList allowed{"filmicrgb", "sigmoid", "basecurve", "colorbalancergb"};
    QSet<QString> found;
    int toneCount = 0;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.tokenType() == QXmlStreamReader::DTD) return {};
        if (!xml.isStartElement() || xml.name() != "plugin") continue;
        QMap<QString, QString> plugin;
        while (xml.readNextStartElement()) plugin[xml.name().toString()] = xml.readElementText();
        const QString op = plugin.value("operation");
        if (!allowed.contains(op)) continue;
        if (found.contains(op) || plugin.value("multi_priority").toInt() != 0) return {};
        found.insert(op);
        const bool enabled = plugin.value("enabled") == "1";
        if (op != "colorbalancergb" && enabled) ++toneCount;
        char *decoded = oma_engine_camera_style_fields(image, op.toLatin1().constData(), plugin.value("module").toInt(),
                                                       plugin.value("op_params").toLatin1().constData());
        if (!decoded) return {};
        const QByteArray fields(decoded); oma_engine_free(decoded);
        for (const auto &line : fields.split('\n')) {
            if (line.isEmpty()) continue;
            const int tab = line.indexOf('\t');
            bool ok = false; const double value = line.mid(tab + 1).toDouble(&ok);
            if (tab <= 0 || !ok || !std::isfinite(value)) return {};
            values << QVariantMap{{"op", op}, {"field", QString::fromLatin1(line.left(tab))}, {"value", value}, {"enabled", enabled}};
        }
        values << QVariantMap{{"op", op}, {"enabled", enabled}};
    }
    if (xml.hasError() || toneCount != 1 || !found.contains("colorbalancergb")) return {};
    // Some upstream styles omit a disabled rival. Switching must still turn
    // all other tone mappers off, including a previously selected print.
    for (const auto &op : allowed)
        if (!found.contains(op)) values << QVariantMap{{"op", op}, {"enabled", false}};
    return {{"source", file}, {"values", values}, {"camera", camera.isEmpty() ? exifCamera : camera}};
#endif
}
