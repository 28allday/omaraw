#include "proof.h"
#include "colourpipeline.h"
#include <QColorSpace>

#include <QFileInfo>
#include <QFile>
#include <QUrl>
#include <lcms2.h>
#include <vector>

namespace Proof {

QByteArray readRgbProfile(const QString &pathOrUrl, QString *error) {
    if (error) error->clear();
    const QString path = pathOrUrl.startsWith(QLatin1String("file:")) ? QUrl(pathOrUrl).toLocalFile() : pathOrUrl;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > 16 * 1024 * 1024) {
        if (error) *error = QStringLiteral("Choose a readable RGB ICC profile (up to 16 MB)"); return {};
    }
    const QByteArray bytes = file.readAll();
    cmsHPROFILE p = cmsOpenProfileFromMem(bytes.constData(), bytes.size());
    const bool valid = p && cmsGetColorSpace(p) == cmsSigRgbData && cmsGetDeviceClass(p) != cmsSigLinkClass
        && cmsIsIntentSupported(p, INTENT_RELATIVE_COLORIMETRIC, LCMS_USED_AS_OUTPUT);
    if (p) cmsCloseProfile(p);
    if (!valid) { if (error) *error = QStringLiteral("This file is not a supported RGB ICC profile"); return {}; }
    return bytes;
}

QImage apply(const QImage &image, const QString &iccPath, int intent, bool gamutWarning, QString *error) {
    if (error) error->clear();
    if (image.isNull() || iccPath.isEmpty()) return image;
    QString problem;
    const QByteArray paper = readRgbProfile(iccPath, &problem);
    if (paper.isEmpty()) { if (error) *error = QStringLiteral("cannot open profile %1: %2").arg(QFileInfo(iccPath).fileName(), problem); return image; }
    const QByteArray linearIcc = QColorSpace(QColorSpace::SRgbLinear).iccProfile();
    cmsContext context = cmsCreateContext(nullptr, nullptr);
    const cmsUInt16Number alarm[cmsMAXCHANNELS] = {0xFFFF, 0x0000, 0xFFFF};
    cmsSetAlarmCodesTHR(context, alarm);
    cmsHPROFILE printer = cmsOpenProfileFromMemTHR(context, paper.constData(), paper.size());
    cmsHPROFILE linear = cmsOpenProfileFromMemTHR(context, linearIcc.constData(), linearIcc.size());
    // Black point compensation, as printing uses by default (Print sheet
    // "black point"): without it the proof crushes shadows to the paper's
    // black that the print itself keeps.
    const cmsUInt32Number flags = cmsFLAGS_SOFTPROOFING | cmsFLAGS_COPY_ALPHA | cmsFLAGS_NOOPTIMIZE
        | cmsFLAGS_BLACKPOINTCOMPENSATION | (gamutWarning ? cmsFLAGS_GAMUTCHECK : 0);
    cmsHTRANSFORM t = printer && linear ? cmsCreateProofingTransformTHR(context, linear, TYPE_RGBA_FLT,
        linear, TYPE_RGBA_FLT, printer, qBound(0, intent, 3), INTENT_RELATIVE_COLORIMETRIC, flags) : nullptr;
    QImage out = image;
    if (t) {
        out = ColourPipeline::linear(image);
        for (int y = 0; y < out.height(); ++y) {
            cmsDoTransform(t, out.constScanLine(y), out.scanLine(y), out.width());
            // Older Little CMS uses negative RGB for float alarms; newer
            // versions use the context's explicit alarm colour above.
            // Keep genuine extended-range RGB; only its all-negative sentinel
            // becomes the existing opaque magenta proof warning.
            if (gamutWarning) {
                auto *row = reinterpret_cast<float *>(out.scanLine(y));
                for (int x = 0; x < out.width(); ++x) if (row[4*x] == -1.f && row[4*x+1] == -1.f && row[4*x+2] == -1.f) {
                    row[4*x] = 1.f; row[4*x+1] = 0.f; row[4*x+2] = 1.f;
                }
            }
        }
        cmsDeleteTransform(t);
        // Compatibility for callers supplying an ordinary encoded preview.
        // Developed float inputs stay float through proof and the OCIO view.
        if (image.colorSpace() != QColorSpace(QColorSpace::SRgbLinear)) out = ColourPipeline::srgb8(out);
    } else if (error) *error = QStringLiteral("profile %1 cannot be used for proofing").arg(QFileInfo(iccPath).fileName());
    if (printer) cmsCloseProfile(printer);
    if (linear) cmsCloseProfile(linear);
    cmsDeleteContext(context);
    return out;
}

QImage convert(const QImage &in, const QString &iccPath, int intent, bool blackPointCompensation, QString *error) {
    if (in.isNull() || iccPath.isEmpty()) return in;
    cmsHPROFILE out = cmsOpenProfileFromFile(iccPath.toLocal8Bit().constData(), "r");
    if (!out) { if (error) *error = QStringLiteral("cannot open profile %1").arg(QFileInfo(iccPath).fileName()); return in; }
    if (cmsGetColorSpace(out) != cmsSigRgbData) {
        cmsCloseProfile(out);
        if (error) *error = QStringLiteral("%1 is not an RGB profile; the printer driver must manage colour for it").arg(QFileInfo(iccPath).fileName());
        return in;
    }
    // From the picture's own profile (a print render is wide-gamut, 16-bit),
    // so nothing the paper can hold is clipped to sRGB on the way; sRGB for
    // an untagged picture.
    const QByteArray icc = in.colorSpace().isValid() ? in.colorSpace().iccProfile() : QByteArray();
    cmsHPROFILE source = icc.isEmpty() ? nullptr : cmsOpenProfileFromMem(icc.constData(), cmsUInt32Number(icc.size()));
    if (!source) source = cmsCreate_sRGBProfile();
    const int intents[] = {INTENT_PERCEPTUAL, INTENT_RELATIVE_COLORIMETRIC, INTENT_SATURATION, INTENT_ABSOLUTE_COLORIMETRIC};
    const cmsUInt32Number flags = blackPointCompensation ? cmsFLAGS_BLACKPOINTCOMPENSATION : 0;
    const bool deep = in.depth() > 32;
    cmsHTRANSFORM t = cmsCreateTransform(source, deep ? TYPE_RGBA_16 : TYPE_BGRA_8, out, TYPE_BGRA_8, intents[qBound(0, intent, 3)], flags);
    if (!t) {
        cmsCloseProfile(out); cmsCloseProfile(source);
        if (error) *error = QStringLiteral("profile %1 cannot be used for output").arg(QFileInfo(iccPath).fileName());
        return in;
    }
    const QImage src = in.convertToFormat(deep ? QImage::Format_RGBA64 : QImage::Format_RGB32);
    QImage result(src.size(), QImage::Format_RGB32);
    for (int y = 0; y < src.height(); ++y) cmsDoTransform(t, src.constScanLine(y), result.scanLine(y), cmsUInt32Number(src.width()));
    cmsDeleteTransform(t);
    cmsCloseProfile(out); cmsCloseProfile(source);
    return result;
}

QString profileName(const QString &iccPath) {
    if (iccPath.isEmpty()) return QString();
    cmsHPROFILE p = cmsOpenProfileFromFile(iccPath.toLocal8Bit().constData(), "r");
    if (!p) return QFileInfo(iccPath).fileName();
    char buf[256] = {0};
    const cmsUInt32Number n = cmsGetProfileInfoASCII(p, cmsInfoDescription, "en", "US", buf, sizeof buf);
    cmsCloseProfile(p);
    const QString name = n ? QString::fromLatin1(buf).trimmed() : QString();
    return name.isEmpty() ? QFileInfo(iccPath).fileName() : name;
}

bool writeTestProfile(const QString &path, bool narrow) {
    cmsHPROFILE p = nullptr;
    if (!narrow) p = cmsCreate_sRGBProfile();
    else {
        // Primaries pulled well inside sRGB: strong reds, greens and blues fall outside it.
        cmsCIExyY d65 = {0.3127, 0.3290, 1.0};
        cmsCIExyYTRIPLE prim = {{0.50, 0.33, 1.0}, {0.32, 0.50, 1.0}, {0.20, 0.15, 1.0}};
        cmsToneCurve *g = cmsBuildGamma(nullptr, 2.2);
        cmsToneCurve *curves[3] = {g, g, g};
        p = cmsCreateRGBProfile(&d65, &prim, curves);
        cmsFreeToneCurve(g);
        if (p) cmsWriteTag(p, cmsSigProfileDescriptionTag, nullptr);
    }
    if (!p) return false;
    if (narrow) {
        cmsMLU *mlu = cmsMLUalloc(nullptr, 1);
        cmsMLUsetASCII(mlu, "en", "US", "OmaRAW narrow test profile");
        cmsWriteTag(p, cmsSigProfileDescriptionTag, mlu);
        cmsMLUfree(mlu);
    }
    const bool ok = cmsSaveProfileToFile(p, path.toLocal8Bit().constData()) != 0;
    cmsCloseProfile(p);
    return ok;
}

} // namespace Proof
