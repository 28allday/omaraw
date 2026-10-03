// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QSet>
#include <QVariantList>
#include <algorithm>
#include <utility>

namespace PresetState {
inline QString owner(const QVariantMap &row) {
    if (row.value("local").toBool() || row.value("localset").toBool()) return "@locals";
    if (row.value("spotset").toBool()) return "@spots";
    if (row.value("rasterset").toBool()) return "@raster";
    if (row.value("liquifyset").toBool()) return "@liquify";
    if (row.value("curveset").toBool()) return "rgbcurve";
    if (row.value("tonecurveset").toBool()) return "tonecurve";
    if (row.value("zoneset").toBool()) return "colorzones";
    return row.value("op").toString();
}
inline QSet<QString> owners(const QVariantList &values) {
    QSet<QString> out;
    for (const auto &v : values) {
        const auto row=v.toMap(); const QString op=owner(row);
        if (!op.isEmpty()) out.insert(op);
        if (op=="omarawprint" || op=="sigmoid" || op=="filmicrgb" || op=="basecurve")
            out.unite({"omarawprint","sigmoid","filmicrgb","basecurve"});
        // A DCP can bake incoming curves and straighten the native ones.
        if (op=="omarawprint" && (row.value("field")=="cameraprofile"
            || (row.value("field")=="stock" && row.value("value").toInt()==22)))
            out.unite({"rgbcurve","tonecurve"});
    }
    return out;
}
inline QVariantList select(const QVariantList &snapshot, const QSet<QString> &owned) {
    QVariantList out;
    for (const auto &v : snapshot) if (owned.contains(owner(v.toMap()))) out << v;
    return out;
}
inline QVariantList snapshot(const QVariantList &params, const QVariantMap &toolState,
                             const QVariantList &locals, const QVariantList &spots,
                             const QVariantMap &curve, const QVariantMap &parametric,
                             const QVariantMap &zones, bool withLocals) {
    QVariantList out;
    if(toolState.value("liquifyAvailable").toBool()) out<<QVariantMap{{"liquifyset",true},{"enabled",toolState.value("liquifyEnabled")},{"warps",toolState.value("warps")}};
    if(withLocals || !toolState.value("rasterTargets").toList().isEmpty()) out<<QVariantMap{{"rasterset",true},{"path",toolState.value("rasterFile")},{"targets",toolState.value("rasterTargets")}};
    if (withLocals) out << QVariantMap{{QStringLiteral("localset"), true}};
    if (withLocals) for (const QVariant &l : locals) { QVariantMap m = l.toMap(); m.remove(QStringLiteral("priority")); m[QStringLiteral("local")] = true; out << m; }
    // The spots travel as one row, so a snapshot with none clears them too.
    if (withLocals) out << QVariantMap{{QStringLiteral("spotset"), true}, {QStringLiteral("spots"), spots}};
    // The tone curve is a global adjustment, so presets carry it as well as snapshots.
    if (curve.value(QStringLiteral("found")).toBool()) {
        QVariantList channels;
        for (const QVariant &v : curve.value(QStringLiteral("channels")).toList()) {
            const QVariantMap c = v.toMap();
            channels << QVariantMap{{QStringLiteral("xs"), c.value(QStringLiteral("xs"))}, {QStringLiteral("ys"), c.value(QStringLiteral("ys"))}, {QStringLiteral("type"), c.value(QStringLiteral("type"))}};
        }
        out << QVariantMap{{QStringLiteral("curveset"), true}, {QStringLiteral("linked"), curve.value(QStringLiteral("linked"), true)},
                           {QStringLiteral("enabled"), curve.value(QStringLiteral("enabled"), false)}, {QStringLiteral("channels"), channels}};
    }
    if (parametric.value(QStringLiteral("found")).toBool()) {
        QVariantList ab;
        for (const QVariant &v : parametric.value(QStringLiteral("ab")).toList()) {
            const QVariantMap c = v.toMap();
            ab << QVariantMap{{QStringLiteral("xs"), c.value(QStringLiteral("xs"))}, {QStringLiteral("ys"), c.value(QStringLiteral("ys"))}, {QStringLiteral("type"), c.value(QStringLiteral("type"))}};
        }
        QVariantMap row{{QStringLiteral("tonecurveset"), true}, {QStringLiteral("enabled"), parametric.value(QStringLiteral("enabled"), false)},
                        {QStringLiteral("xs"), parametric.value(QStringLiteral("xs"))}, {QStringLiteral("ys"), parametric.value(QStringLiteral("ys"))}};
        if (ab.size() == 2) row[QStringLiteral("ab")] = ab;
        out << row;
    }
    if (zones.value(QStringLiteral("found")).toBool())
        out << QVariantMap{{QStringLiteral("zoneset"), true}, {QStringLiteral("enabled"), zones.value(QStringLiteral("enabled"), false)},
                           {QStringLiteral("channels"), zones.value(QStringLiteral("channels"))}};
    for (const QVariant &v : params) {
        const QVariantMap m = v.toMap();
        QVariantMap row;
        row[QStringLiteral("op")] = m.value(QStringLiteral("op"));
        row[QStringLiteral("field")] = m.value(QStringLiteral("field"));
        row[QStringLiteral("value")] = m.value(QStringLiteral("value"));
        row[QStringLiteral("enabled")] = m.value(QStringLiteral("enabled"), true);
        if (m.value(QStringLiteral("kind")) == QLatin1String("file")) {
            row[QStringLiteral("kind")] = QStringLiteral("file");
            row[QStringLiteral("text")] = m.value(QStringLiteral("text"));
        }
        out << row;
    }
    // Native camera styles also set tone fields outside the simplified UI.
    // Carry those through presets/snapshots so saving and reopening a recipe
    // reproduces the look. Public rows win and no field is duplicated.
    QSet<QString> savedFields;
    for (const auto &v : std::as_const(out)) {
        const auto row = v.toMap();
        savedFields.insert(row.value("op").toString() + '/' + row.value("field").toString());
    }
    for (const auto &v : toolState.value("cameraLookParams").toList()) {
        const auto row = v.toMap();
        if (!savedFields.contains(row.value("op").toString() + '/' + row.value("field").toString())) out << v;
    }
    if (toolState.contains("printBaseTone"))
        out << QVariantMap{{"op", "omarawprint"}, {"field", "base_tone"},
                           {"value", toolState.value("printBaseTone")}, {"enabled", std::any_of(params.cbegin(), params.cend(), [](const QVariant &v) {
                               const auto row=v.toMap(); return row.value("op")=="omarawprint" && row.value("enabled").toBool();
                           })},
                           {"toneParams", toolState.value("printToneParams", QVariantList{})}};
    return out;
}

}
