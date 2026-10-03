// SPDX-License-Identifier: GPL-3.0-or-later
#include "enhancedprofile.h"
#include "creativeprofiledata.h"
#include "xmppreset.h"
#include <QRegularExpression>
#include <QSet>
#include <QtEndian>
#include <zlib.h>
#include <cmath>

namespace {
QByteArray table(const QString &text, QString &error) {
    // The XMP table format’s XML-safe base85 alphabet, little-endian digits and bytes.
    const QByteArray alphabet="0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-:+=^!/*?`'|()[]{}@%$#";
    QByteArray digits;
    for(const QChar c:text) {
        if(c.isSpace()) continue;
        if(c.unicode()>127 || !alphabet.contains(char(c.unicode()))) { error="Invalid character in the embedded table."; return {}; }
        digits+=char(c.unicode());
    }
    if(digits.size()<7 || digits.size()>24*1024*1024 || digits.size()%5==1) { error="Invalid embedded table length."; return {}; }
    QByteArray compressed;
    compressed.reserve(digits.size()*4/5);
    for(qsizetype pos=0;pos<digits.size();pos+=5) {
        const int count=int(qMin(qsizetype(5),digits.size()-pos));
        quint64 value=0,power=1;
        for(int i=0;i<count;++i) { value+=quint64(alphabet.indexOf(digits[pos+i]))*power; power*=85; }
        if(value>UINT32_MAX) { error="Overflow in the embedded table encoding."; return {}; }
        for(int i=0;i<(count==5 ? 4 : count-1);++i) compressed+=char(value>>(8*i));
    }
    if(compressed.size()<5) { error="Truncated embedded table."; return {}; }
    const quint32 length=qFromLittleEndian<quint32>(compressed.constData());
    if(length<24 || length>16*1024*1024) { error="Embedded table exceeds the supported size."; return {}; }
    QByteArray result(length,Qt::Uninitialized);
    uLongf destination=length; uLong source=compressed.size()-4;
    const int status=uncompress2(reinterpret_cast<Bytef *>(result.data()),&destination,
                                reinterpret_cast<const Bytef *>(compressed.constData()+4),&source);
    if(status!=Z_OK || destination!=length || source!=uLong(compressed.size()-4)) {
        error="Damaged or truncated compressed profile table."; return {};
    }
    return result;
}
bool boolValue(const QVariantMap &f,const QString &key,bool fallback,bool &valid) {
    if(!f.contains(key)) return fallback;
    const QString s=f.value(key).toString().toLower();
    if(s=="true") return true;
    if(s=="false") return false;
    valid=false; return fallback;
}
void integer(QByteArray &out,quint32 n) { char b[4]; qToLittleEndian(n,b); out.append(b,4); }
void real(QByteArray &out,float n) { quint32 bits; memcpy(&bits,&n,4); integer(out,bits); }
}

EnhancedProfile::Result EnhancedProfile::read(const QByteArray &bytes) {
    Result out; const QVariantMap fields=XmpPreset::readXmpProperties(bytes,out.error);
    if(!out.error.isEmpty()) return out;
    if(fields.value("PresetType").toString()!="Look") {
        out.error="Choose an enhanced XMP profile (PresetType Look). Develop presets belong in Presets → Import."; return out;
    }
    out.name=fields.value("Name").toString().trimmed();
    if(out.name.isEmpty() || out.name.size()>512) { out.error="The XMP profile needs a valid name."; return out; }
    // These describe the file or are handled below. Rendering properties not
    // implemented by our pipeline are accepted only when explicitly neutral.
    static const QSet<QString> metadata={"PresetType","Version","ProcessVersion","UUID","Name","ShortName","SortName",
        "Group","Description","Copyright","ContactInfo","LookTable","RGBTable","LookTableAmount","RGBTableAmount",
        "SupportsAmount","SupportsColor","SupportsMonochrome","SupportsHighDynamicRange","SupportsNormalDynamicRange",
        "SupportsSceneReferred","SupportsOutputReferred","CameraModelRestriction","CameraProfile","CameraProfileDigest",
        "HasSettings","HasCrop","Cluster","Treatment","ConvertToGrayscale"};
    QStringList unsupported;
    for(auto it=fields.begin();it!=fields.end();++it) {
        if(metadata.contains(it.key()) || it.key().startsWith("Table_")) continue;
        bool neutral=false;
        if(it.key().startsWith("ToneCurve") && it.value().typeId()==QMetaType::QVariantList) {
            neutral=true;
            for(const auto &point:it.value().toList()) {
                const auto pair=point.toString().split(','); bool a=false,b=false;
                const double x=pair.value(0).toDouble(&a),y=pair.value(1).toDouble(&b);
                if(pair.size()!=2 || !a || !b || x!=y) neutral=false;
            }
        } else if(it.value().typeId()==QMetaType::QString) {
            const QString value=it.value().toString().trimmed(); bool ok=false;
            const double n=value.toDouble(&ok);
            neutral=value.isEmpty() || value.compare("false",Qt::CaseInsensitive)==0 || (ok && n==0)
                    || (it.key().startsWith("ToneCurveName") && value=="Linear");
        }
        if(!neutral) unsupported<<it.key();
    }
    if(!fields.value("CameraModelRestriction").toString().trimmed().isEmpty()) unsupported<<"CameraModelRestriction";
    const QString camera=fields.value("CameraProfile").toString();
    if(!camera.isEmpty() && camera!="Adobe Standard" && camera!="Embedded") unsupported<<"CameraProfile: "+camera;
    if(fields.value("ConvertToGrayscale").toString().compare("true",Qt::CaseInsensitive)==0
       || fields.value("Treatment").toString()=="Black & White") unsupported<<"black-and-white treatment";
    if(!unsupported.isEmpty()) {
        out.error="This profile also requires unsupported source settings: "+unsupported.join(", ")+". No partial look was imported."; return out;
    }
    bool valid=true;
    const bool amount=boolValue(fields,"SupportsAmount",true,valid);
    const bool scene=boolValue(fields,"SupportsSceneReferred",true,valid);
    const bool output=boolValue(fields,"SupportsOutputReferred",true,valid);
    if(!valid || (!scene && !output)) { out.error="Invalid profile support flags."; return out; }
    QByteArray tables[2]; float middle[2]={1,1}; const QString names[2]={"RGBTable","LookTable"};
    for(int i=0;i<2;++i) {
        const QString id=fields.value(names[i]).toString();
        if(id.isEmpty()) continue;
        if(!QRegularExpression("^[0-9A-Fa-f]{32}$").match(id).hasMatch() || !fields.contains("Table_"+id)) {
            out.error="The profile references a missing external "+names[i]+". Import a self-contained profile."; return out;
        }
        tables[i]=table(fields.value("Table_"+id).toString(),out.error);
        if(!out.error.isEmpty()) return out;
        if(fields.contains(names[i]+"Amount")) {
            bool ok=false; const double value=fields.value(names[i]+"Amount").toDouble(&ok);
            if(!ok || !std::isfinite(value) || value<0 || value>10) { out.error="Invalid profile table amount."; return out; }
            middle[i]=float(value);
        }
    }
    out.profile="OMAPRF01";
    integer(out.profile,(amount ? 1u : 0u)|(scene ? 2u : 0u)|(output ? 4u : 0u));
    real(out.profile,middle[0]); real(out.profile,middle[1]);
    integer(out.profile,tables[0].size()); integer(out.profile,tables[1].size());
    out.profile+=tables[0]; out.profile+=tables[1];
    oma_creative_data parsed{};
    if(!oma_creative_parse(out.profile.constData(),out.profile.size(),&parsed)) {
        out.error="Unsupported or invalid RGB/HSV table format, dimensions, flags or values."; out.profile.clear();
    }
    oma_creative_clear(&parsed); return out;
}
