// SPDX-License-Identifier: GPL-3.0-or-later
#include "engineservice.h"
#include "creativeprofiledata.h"
#include "enhancedprofile.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

namespace {
QString library() { return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/creative-profiles"; }
QString label(const QString &path) { return QFileInfo(path).completeBaseName().section("--", 0, 0); }
bool validProfile(const QByteArray &bytes) {
    oma_creative_data profile{}; const bool ok = oma_creative_parse(bytes.constData(), size_t(bytes.size()), &profile);
    oma_creative_clear(&profile); return ok;
}
}

QVariantList EngineService::creativeProfiles() const {
    const QStringList names{tr("None"), tr("Warm Portrait"), tr("Clear Landscape"), tr("Soft Colour"),
                            tr("Cinema Dusk"), tr("Silver Monochrome"), tr("Warm Monochrome")};
    const QStringList descriptions{tr("Keep the existing rendering."), tr("Gentle colour with warm highlights."),
        tr("Crisp colour and deeper contrast."), tr("Quiet colour, soft contrast and lifted blacks."),
        tr("Cool shadows and warm highlights with restrained colour."), tr("Neutral black and white with firm contrast."),
        tr("Soft black and white with a warm tone.")};
    QVariantList out;
    for(int i=0; i<names.size(); ++i)
        out << QVariantMap{{"key", "builtin:"+QString::number(i)}, {"name", names[i]}, {"description", descriptions[i]}};
    const QDir folder(library());
    for(const auto &file : folder.entryInfoList({"*.cube","*.omaprofile"}, QDir::Files | QDir::NoSymLinks, QDir::Name))
        out << QVariantMap{{"key", file.absoluteFilePath()}, {"name", label(file.fileName())},
                          {"description", file.suffix()=="omaprofile" ? tr("Imported XMP tables. Rendering uses OmaRAW's base colour.")
                                                                    : tr("Imported LUT. Match its colour space below.")}};
    return out;
}

bool EngineService::importCreativeProfile(const QString &source) {
    if(!m_ready || m_imgid<0) return false;
    const QUrl url(source); const QString path=url.isLocalFile() ? url.toLocalFile() : source;
    const bool xmp=QFileInfo(path).suffix().compare("xmp",Qt::CaseInsensitive)==0;
    QFile input(path);
    if((!xmp && QFileInfo(path).suffix().compare("cube", Qt::CaseInsensitive)) || !input.open(QIODevice::ReadOnly)
       || input.size()<=0 || input.size()>OMA_CUBE_MAX_BYTES) {
        setStatus(tr("Choose a readable .cube LUT or enhanced .xmp profile, up to 32 MB.")); return false;
    }
    const QByteArray original=input.read(OMA_CUBE_MAX_BYTES+1);
    QByteArray bytes=original;
    QString name=QFileInfo(path).completeBaseName();
    if(xmp) {
        const auto imported=EnhancedProfile::read(original);
        if(!imported.error.isEmpty()) { setStatus(imported.error); return false; }
        bytes=imported.profile; name=imported.name;
    }
    if(!validProfile(bytes)) {
        setStatus(tr("Unsupported or invalid LUT. Use a 3D CUBE with 2–64 entries per axis, complete finite RGB rows and no 1D shaper.")); return false;
    }
    name=name.left(70);
    name.replace(QRegularExpression("[^\\p{L}\\p{N} _.-]"), "_"); name.replace("--", "_");
    if(name.isEmpty()) name="Imported LUT";
    const QString base=library()+'/'+name+"--"+QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
    const QString destination=base+(xmp ? ".omaprofile" : ".cube");
    if(destination.toUtf8().size()>=512 || !QDir().mkpath(library())) { setStatus(tr("The profile library path is unavailable or too long.")); return false; }
    // Content-addressed copies retain the exact imported version. No edit
    // follows subsequent changes to the user's original LUT.
    QSaveFile output(destination);
    if(!output.open(QIODevice::WriteOnly) || output.write(bytes)!=bytes.size() || !output.commit()) {
        setStatus(tr("Could not save the LUT in the profile library.")); return false;
    }
    if(xmp) {
        // Retain the original metadata/copyright alongside the runtime tables.
        QSaveFile sourceCopy(base+".xmp");
        if(!sourceCopy.open(QIODevice::WriteOnly) || sourceCopy.write(original)!=original.size() || !sourceCopy.commit()) {
            setStatus(tr("Could not retain the source XMP in the profile library.")); return false;
        }
    }
    emit creativeProfilesChanged();
    return selectCreativeProfile(destination);
}

bool EngineService::selectCreativeProfile(const QString &key) {
    if(!m_ready || m_imgid<0) return false;
    int look=7; QString path=key;
    if(key.startsWith("builtin:")) {
        bool ok=false; look=key.mid(8).toInt(&ok);
        if(!ok || look<0 || look>6) return false;
        path.clear();
    } else {
        QFile input(path);
        if(path.toUtf8().size()>=512 || !input.open(QIODevice::ReadOnly) || input.size()>OMA_CUBE_MAX_BYTES
           || !validProfile(input.read(OMA_CUBE_MAX_BYTES+1))) {
            setStatus(tr("This profile's LUT is missing or invalid. Import it again to restore the look.")); return false;
        }
        const auto details=creativeProfileDetails(path);
        if(details.value("enhanced").toBool()) {
            const bool raw=m_toolState.value("sceneReferred").toBool();
            if(!details.value(raw ? "scene" : "output").toBool()) {
                setStatus(tr("This profile does not support the current photo type (RAW or rendered).")); return false;
            }
        }
    }
    applyValues({QVariantMap{{"op","omarawprofile"},{"field","filepath"},{"text",path}},
                 QVariantMap{{"op","omarawprofile"},{"field","look"},{"value",look}},
                 QVariantMap{{"op","omarawprofile"},{"field","colorspace"},{"value",0}},
                 QVariantMap{{"op","omarawprofile"},{"enabled",look!=0}}});
    return true;
}

QString EngineService::creativeProfileFileStatus(const QString &path) const {
    return QFileInfo::exists(path) ? (path.endsWith(".omaprofile")
              ? tr("Imported XMP: %1. Uses OmaRAW's base rendering; Source-specific adjustments are not reproduced.").arg(label(path))
              : tr("Imported: %1").arg(label(path)))
                                 : tr("LUT missing. Import the original LUT again to restore this look.");
}

QVariantMap EngineService::creativeProfileDetails(const QString &path) const {
    oma_creative_data p{};
    if(!oma_creative_read(path.toUtf8().constData(),&p)) return {};
    const QVariantMap out{{"enhanced",bool(p.enhanced)},{"amount",bool(p.supports_amount)},
                          {"scene",bool(p.scene)},{"output",bool(p.output)}};
    oma_creative_clear(&p); return out;
}
