#include "cameraprofiles.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QtEndian>
#include <algorithm>
#include <array>
#include <vector>
#include <cmath>
#include <cstring>

namespace CameraProfiles {
namespace {

// ── the file: a TIFF-shaped container ("IIRC"/"MMCR") of DNG profile tags ──
enum Tag : quint16 {
    UniqueCameraModel = 50708, ColorMatrix1 = 50721, ColorMatrix2 = 50722, CalibrationIlluminant1 = 50778, CalibrationIlluminant2 = 50779,
    ForwardMatrix1 = 50964, ForwardMatrix2 = 50965,
    ProfileName = 50936, HueSatMapDims = 50937, HueSatMapData1 = 50938, HueSatMapData2 = 50939,
    ProfileToneCurve = 50940, LookTableDims = 50981, LookTableData = 50982,
    HueSatMapEncoding = 51107, LookTableEncoding = 51108, BaselineExposureOffset = 51109,
};

struct Entry { quint16 type = 0; quint32 count = 0, offset = 0; bool inline_ = false; };

class Reader {
public:
    // `partial`: only the start of the file was read (for its names); what
    // lies beyond is skipped rather than refused.
    explicit Reader(const QByteArray &bytes, bool partial = false) : b(bytes), partial(partial) {}
    bool header(QString &error) {
        if (b.size() < 8) { error = QStringLiteral("too short to be a camera profile"); return false; }
        if (b.startsWith("IIRC")) big = false;
        else if (b.startsWith("MMCR")) big = true;
        else { error = QStringLiteral("not a DCP camera profile"); return false; }
        const quint32 ifd = u32(4);
        if (ifd < 8 || qint64(ifd) + 2 > b.size()) { error = QStringLiteral("damaged header"); return false; }
        const int n = u16(ifd);
        if (qint64(ifd) + 2 + qint64(n) * 12 > b.size()) { error = partial ? QStringLiteral("names beyond the part read") : QStringLiteral("damaged header"); return false; }
        for (int i = 0; i < n; ++i) {
            const quint32 at = ifd + 2 + i * 12;
            Entry e;
            e.type = u16(at + 2); e.count = u32(at + 4);
            const qint64 bytes = qint64(e.count) * width(e.type);
            if (width(e.type) == 0) continue;
            if (bytes <= 4) { e.inline_ = true; e.offset = at + 8; }
            else {
                e.offset = u32(at + 8);
                if (qint64(e.offset) + bytes > b.size()) {
                    if (partial) continue;
                    error = QStringLiteral("damaged: a table runs past the end of the file");
                    return false;
                }
            }
            tags.insert(u16(at), e);
        }
        return true;
    }
    bool has(quint16 tag) const { return tags.contains(tag); }
    QString text(quint16 tag) const {
        const Entry e = tags.value(tag);
        if (e.type != 2 && e.type != 1) return {};
        QByteArray s = b.mid(e.offset, e.count);
        const int nul = s.indexOf('\0');
        if (nul >= 0) s.truncate(nul);
        return QString::fromUtf8(s).simplified();
    }
    QVector<double> numbers(quint16 tag) const {
        const Entry e = tags.value(tag);
        QVector<double> out;
        out.reserve(e.count);
        for (quint32 i = 0; i < e.count; ++i) {
            const quint32 at = e.offset + i * width(e.type);
            switch (e.type) {
            case 3: out << u16(at); break;
            case 4: out << u32(at); break;
            case 9: out << qint32(u32(at)); break;
            case 5: { const double d = u32(at + 4); out << (d ? u32(at) / d : 0.0); break; }
            case 10: { const double d = qint32(u32(at + 4)); out << (d ? qint32(u32(at)) / d : 0.0); break; }
            case 11: { const quint32 v = u32(at); float f; std::memcpy(&f, &v, 4); out << f; break; }
            case 12: { quint64 v = big ? qFromBigEndian<quint64>(b.constData() + at) : qFromLittleEndian<quint64>(b.constData() + at); double d; std::memcpy(&d, &v, 8); out << d; break; }
            default: return {};
            }
        }
        return out;
    }
private:
    static int width(quint16 type) {
        switch (type) { case 1: case 2: case 7: return 1; case 3: return 2; case 4: case 9: case 11: return 4; case 5: case 10: case 12: return 8; default: return 0; }
    }
    quint16 u16(quint32 at) const { return big ? qFromBigEndian<quint16>(b.constData() + at) : qFromLittleEndian<quint16>(b.constData() + at); }
    quint32 u32(quint32 at) const { return big ? qFromBigEndian<quint32>(b.constData() + at) : qFromLittleEndian<quint32>(b.constData() + at); }
    const QByteArray &b;
    bool partial = false, big = false;
    QHash<quint16, Entry> tags;
};

// A hue/saturation/value table: every entry a plausible hue shift and
// scale. A damaged file reads as random numbers somewhere in its tables.
bool table(const Reader &r, quint16 dimsTag, quint16 dataTag, int dims[3], QVector<float> &out, QString &error) {
    const QVector<double> d = r.numbers(dimsTag), v = r.numbers(dataTag);
    if (d.size() != 3 || d[0] < 1 || d[1] < 2 || d[2] < 1 || d[0] > 360 || d[1] > 64 || d[2] > 64) { error = QStringLiteral("damaged table sizes"); return false; }
    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(d[i]) || d[i] != std::floor(d[i])) { error = QStringLiteral("damaged table sizes"); return false; }
        dims[i] = int(d[i]);
    }
    if (v.size() != dims[0] * dims[1] * dims[2] * 3) { error = QStringLiteral("damaged: a table has the wrong number of entries"); return false; }
    out.resize(v.size());
    for (int i = 0; i < v.size(); ++i) {
        const double x = v[i];
        const bool hue = i % 3 == 0;
        if (!std::isfinite(x) || (hue ? std::abs(x) > 360 : (x < 0 || x > 16 || (x != 0 && x < 1e-6)))) {
            error = QStringLiteral("damaged: its colour tables hold values no profile uses (entry %1 of %2)").arg(i / 3 + 1).arg(v.size() / 3);
            return false;
        }
        out[i] = float(x);
    }
    return true;
}

// ── colour: 3×3 matrices between linear Rec. 2020 and ProPhoto, both on
// the D50 white the engine's working profiles use ──
using M3 = std::array<std::array<double, 3>, 3>;
M3 mul(const M3 &a, const M3 &b) {
    M3 o{};
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) for (int k = 0; k < 3; ++k) o[r][c] += a[r][k] * b[k][c];
    return o;
}
M3 inverse(const M3 &m) {
    const double a = m[0][0], b = m[0][1], c = m[0][2], d = m[1][0], e = m[1][1], f = m[1][2], g = m[2][0], h = m[2][1], i = m[2][2];
    const double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    return {{{(e * i - f * h) / det, (c * h - b * i) / det, (b * f - c * e) / det},
             {(f * g - d * i) / det, (a * i - c * g) / det, (c * d - a * f) / det},
             {(d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det}}};
}
M3 toXyz(const double xy[3][2], const double white[3]) {
    M3 p{};
    for (int c = 0; c < 3; ++c) { p[0][c] = xy[c][0] / xy[c][1]; p[1][c] = 1; p[2][c] = (1 - xy[c][0] - xy[c][1]) / xy[c][1]; }
    const M3 inv = inverse(p);
    double s[3];
    for (int r = 0; r < 3; ++r) s[r] = inv[r][0] * white[0] + inv[r][1] * white[1] + inv[r][2] * white[2];
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) p[r][c] *= s[c];
    return p;
}
const M3 &rec2020ToProPhoto() {
    static const M3 m = [] {
        const double d65[3] = {0.95047, 1.0, 1.08883}, d50[3] = {0.96422, 1.0, 0.82521};
        const double rec2020[3][2] = {{0.708, 0.292}, {0.170, 0.797}, {0.131, 0.046}};
        const double prophoto[3][2] = {{0.7347, 0.2653}, {0.1596, 0.8404}, {0.0366, 0.0001}};
        const M3 bradford{{{0.8951, 0.2664, -0.1614}, {-0.7502, 1.7135, 0.0367}, {0.0389, -0.0685, 1.0296}}};
        const M3 bi = inverse(bradford);
        double s65[3], s50[3];
        for (int r = 0; r < 3; ++r) {
            s65[r] = bradford[r][0] * d65[0] + bradford[r][1] * d65[1] + bradford[r][2] * d65[2];
            s50[r] = bradford[r][0] * d50[0] + bradford[r][1] * d50[1] + bradford[r][2] * d50[2];
        }
        M3 scale{};
        for (int r = 0; r < 3; ++r) scale[r][r] = s50[r] / s65[r];
        const M3 adapt = mul(bi, mul(scale, bradford));
        return mul(inverse(toXyz(prophoto, d50)), mul(adapt, toXyz(rec2020, d65)));
    }();
    return m;
}
const M3 &proPhotoToRec2020() { static const M3 m = inverse(rec2020ToProPhoto()); return m; }
M3 bradford(const double from[3], const double to[3]) {
    const M3 b{{{0.8951, 0.2664, -0.1614}, {-0.7502, 1.7135, 0.0367}, {0.0389, -0.0685, 1.0296}}};
    double sf[3], st[3];
    for (int r = 0; r < 3; ++r) { sf[r] = b[r][0] * from[0] + b[r][1] * from[1] + b[r][2] * from[2]; st[r] = b[r][0] * to[0] + b[r][1] * to[1] + b[r][2] * to[2]; }
    M3 scale{};
    for (int r = 0; r < 3; ++r) scale[r][r] = st[r] / sf[r];
    return mul(inverse(b), mul(scale, b));
}
const M3 &xyzToProPhoto() {
    static const M3 m = [] {
        const double d50[3] = {0.96422, 1.0, 0.82521};
        const double prophoto[3][2] = {{0.7347, 0.2653}, {0.1596, 0.8404}, {0.0366, 0.0001}};
        return inverse(toXyz(prophoto, d50));
    }();
    return m;
}

// The engine renders a RAW through the camera's colour matrix: white-
// balanced camera colour to XYZ as the inverse of the matrix, scaled so
// white is white, adapted to D50. A DCP with forward matrices renders
// through those instead, and tunes its tables to that. The difference, as
// a matrix on linear ProPhoto; identity when the profile has no forward
// matrix or no usable colour matrix.
M3 forwardCorrection(const Reader &r) {
    const M3 identity{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
    const auto daylight = [](double l) { return l == 21 || l == 20 || l == 23 || l == 1 || l == 4 || l == 9 || l == 22; };
    const QVector<double> i1 = r.numbers(CalibrationIlluminant1), i2 = r.numbers(CalibrationIlluminant2);
    // The daylight calibration: the second unless only the first is daylight.
    const bool first = !r.has(ColorMatrix2) || (!i1.isEmpty() && daylight(i1[0]) && !(i2.size() && daylight(i2[0])));
    const QVector<double> cm = r.numbers(first ? ColorMatrix1 : ColorMatrix2);
    QVector<double> fm = r.numbers(first ? ForwardMatrix1 : ForwardMatrix2);
    if (fm.size() != 9) fm = r.numbers(first ? ForwardMatrix2 : ForwardMatrix1);
    if (cm.size() != 9 || fm.size() != 9) return identity;
    M3 CM, FM;
    for (int i = 0; i < 9; ++i) {
        if (!std::isfinite(cm[i]) || !std::isfinite(fm[i])) return identity;
        CM[i / 3][i % 3] = cm[i]; FM[i / 3][i % 3] = fm[i];
    }
    const double d65[3] = {0.95047, 1.0, 1.08883}, d50[3] = {0.96422, 1.0, 0.82521};
    double white[3];
    for (int k = 0; k < 3; ++k) white[k] = CM[k][0] * d65[0] + CM[k][1] * d65[1] + CM[k][2] * d65[2];
    const double peak = std::max({white[0], white[1], white[2]});
    if (!(peak > 0) || std::min({white[0], white[1], white[2]}) <= 0) return identity;
    M3 wb{};
    for (int k = 0; k < 3; ++k) wb[k][k] = white[k] / peak;
    M3 engine = mul(bradford(d65, d50), mul(inverse(CM), wb));
    const double y = engine[1][0] + engine[1][1] + engine[1][2];
    if (!(y > 0)) return identity;
    for (auto &row : engine) for (double &v : row) v /= y;
    // Forward matrices are normalised so white-balanced white is D50 white.
    double fy = FM[1][0] + FM[1][1] + FM[1][2];
    if (!(fy > 0)) return identity;
    for (auto &row : FM) for (double &v : row) v /= fy;
    const M3 &toPP = xyzToProPhoto();
    const M3 c = mul(toPP, mul(FM, mul(inverse(engine), inverse(toPP))));
    for (const auto &row : c) for (double v : row) if (!std::isfinite(v) || std::abs(v) > 8) return identity;
    return c;
}

// ── the DNG rendering steps (DNG specification, "Camera Profiles") ──
double srgbEncode(double x) { return x <= 0.0031308 ? 12.92 * x : 1.055 * std::pow(x, 1 / 2.4) - 0.055; }
double srgbDecode(double x) { return x <= 0.04045 ? x / 12.92 : std::pow((x + 0.055) / 1.055, 2.4); }

void toHsv(const double rgb[3], double &h, double &s, double &v) {
    const double r = rgb[0], g = rgb[1], b = rgb[2];
    v = std::max({r, g, b});
    const double gap = v - std::min({r, g, b});
    if (gap > 0) {
        if (r == v) { h = (g - b) / gap; if (h < 0) h += 6; }
        else if (g == v) h = 2 + (b - r) / gap;
        else h = 4 + (r - g) / gap;
        s = gap / v;
    } else h = s = 0;
}
void fromHsv(double h, double s, double v, double rgb[3]) {
    if (s <= 0) { rgb[0] = rgb[1] = rgb[2] = v; return; }
    h = std::fmod(h, 6.0); if (h < 0) h += 6;
    const int i = std::min(int(h), 5);
    const double f = h - i, p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    switch (i) {
    case 0: rgb[0] = v; rgb[1] = t; rgb[2] = p; break;
    case 1: rgb[0] = q; rgb[1] = v; rgb[2] = p; break;
    case 2: rgb[0] = p; rgb[1] = v; rgb[2] = t; break;
    case 3: rgb[0] = p; rgb[1] = q; rgb[2] = v; break;
    case 4: rgb[0] = t; rgb[1] = p; rgb[2] = v; break;
    default: rgb[0] = v; rgb[1] = p; rgb[2] = q; break;
    }
}

// A hue/saturation(/value) table applied as the DNG reference does, but
// light above 1 keeps its level (scaled, not clipped), as it still has a
// highlight roll-off to go through.
void hueSatMap(const int dims[3], const QVector<float> &t, bool srgb, double rgb[3]) {
    const int hueDiv = dims[0], satDiv = dims[1], valDiv = dims[2];
    double h, s, v;
    toHsv(rgb, h, s, v);
    if (v <= 0) return;
    const double vIn = std::min(v, 1.0), vEnc = srgb ? srgbEncode(vIn) : vIn;
    const double hScaled = hueDiv < 2 ? 0 : h * (hueDiv / 6.0), sScaled = s * (satDiv - 1);
    int h0 = int(hScaled), h1 = h0 + 1;
    if (h0 >= hueDiv - 1) { h0 = hueDiv - 1; h1 = 0; }
    const int s0 = std::min(int(sScaled), satDiv - 2);
    const double hf = hueDiv < 2 ? 0 : hScaled - h0, sf = std::min(sScaled - s0, 1.0);
    double vScaled = 0; int v0 = 0; double vf = 0;
    if (valDiv > 1) { vScaled = vEnc * (valDiv - 1); v0 = std::min(int(vScaled), valDiv - 2); vf = vScaled - v0; }
    double shift = 0, satScale = 0, valScale = 0;
    for (int dv = 0; dv < (valDiv > 1 ? 2 : 1); ++dv)
        for (int dh = 0; dh < 2; ++dh)
            for (int ds = 0; ds < 2; ++ds) {
                const double w = (valDiv > 1 ? (dv ? vf : 1 - vf) : 1) * (dh ? hf : 1 - hf) * (ds ? sf : 1 - sf);
                if (w == 0) continue;
                const int idx = (((v0 + dv) * hueDiv + (dh ? h1 : h0)) * satDiv + s0 + ds) * 3;
                shift += w * t[idx]; satScale += w * t[idx + 1]; valScale += w * t[idx + 2];
            }
    h += shift * (6.0 / 360.0);
    s = std::min(s * satScale, 1.0);
    // The value scale acts on the encoded value where the table asks for it.
    if (srgb && vIn > 0) v *= srgbDecode(std::clamp(vEnc * valScale, 0.0, 1.0)) / vIn;
    else v *= valScale;
    fromHsv(h, s, v, rgb);
}

// A smooth curve through the profile's tone points (natural cubic spline).
struct Spline {
    std::vector<double> x, y, m;
    explicit Spline(const QVector<float> &pairs) {
        for (int i = 0; i + 1 < pairs.size(); i += 2) { x.push_back(pairs[i]); y.push_back(pairs[i + 1]); }
        const size_t n = x.size();
        m.assign(n, 0);
        if (n < 3) return;
        std::vector<double> u(n, 0);
        for (size_t i = 1; i + 1 < n; ++i) {
            const double sig = (x[i] - x[i - 1]) / (x[i + 1] - x[i - 1]), p = sig * m[i - 1] + 2;
            m[i] = (sig - 1) / p;
            u[i] = (6 * ((y[i + 1] - y[i]) / (x[i + 1] - x[i]) - (y[i] - y[i - 1]) / (x[i] - x[i - 1])) / (x[i + 1] - x[i - 1]) - sig * u[i - 1]) / p;
        }
        if (n) m[n - 1] = 0;
        for (size_t i = n - 1; i-- > 0;) m[i] = m[i] * m[i + 1] + u[i];
    }
    double operator()(double v) const {
        if (x.size() < 2) return v;
        v = std::clamp(v, x.front(), x.back());
        const size_t k = std::min<size_t>(std::upper_bound(x.begin(), x.end(), v) - x.begin(), x.size() - 1) - 1;
        const double h = x[k + 1] - x[k], a = (x[k + 1] - v) / h, b = (v - x[k]) / h;
        return std::clamp(a * y[k] + b * y[k + 1] + ((a * a * a - a) * m[k] + (b * b * b - b) * m[k + 1]) * h * h / 6, 0.0, 1.0);
    }
};

// The tone curve applied to the brightest and darkest channel, the middle
// one kept in proportion between them, so hues hold (the DNG "RGB tone").
void rgbTone(const Spline &curve, double rgb[3]) {
    int hi = 0, lo = 0;
    for (int c = 1; c < 3; ++c) { if (rgb[c] > rgb[hi]) hi = c; if (rgb[c] < rgb[lo]) lo = c; }
    if (hi == lo) { const double t = curve(rgb[0]); rgb[0] = rgb[1] = rgb[2] = t; return; }
    const int mid = 3 - hi - lo;
    const double h = rgb[hi], l = rgb[lo], m = rgb[mid];
    const double th = curve(h), tl = curve(l);
    rgb[hi] = th; rgb[lo] = tl;
    rgb[mid] = tl + (th - tl) * (m - l) / (h - l);
}

// Scene light above the profile's white would clip in its tone curve;
// above 0.7 it is eased towards 1 instead, keeping about two more stops of
// highlight. Hue held: every channel scaled alike.
void shoulder(double rgb[3]) {
    const double k = 0.7, peak = std::max({rgb[0], rgb[1], rgb[2]});
    if (peak <= k) return;
    const double eased = k + (1 - k) * (1 - std::exp(-(peak - k) / (1 - k)));
    for (int c = 0; c < 3; ++c) rgb[c] *= eased / peak;
}

// Every step, in the order the DNG reference renders them: the exposure
// offset, the camera's colour map, the look table and the tone curve.
// The engine's scene light has its middle grey at 0.1845, where the source editor's
// renderings, and so the profiles' tone curves, expect about half that: a
// profile's curve turns 0.09 into the display's middle grey.
constexpr double kSceneToProfile = 0.09 / 0.1845;

// A profile's curves, made once for every pixel of a bake.
struct Curves {
    Spline tone, regions, channel[3];
    bool finish;
    explicit Curves(const Dcp &d)
        : tone(d.tone), regions(d.finishRegions), channel{Spline(d.finishCurves[0]), Spline(d.finishCurves[1]), Spline(d.finishCurves[2])},
          finish(!d.finishRegions.isEmpty() || !d.finishCurves[0].isEmpty() || !d.finishCurves[1].isEmpty() || !d.finishCurves[2].isEmpty()) {}
};

void develop(const Dcp &dcp, const Curves &curves, double p[3]) {
    const Spline &curve = curves.tone;
    const double gain = std::exp2(dcp.exposureOffset) * kSceneToProfile;
    const double *f = dcp.toForward;
    const double q[3] = {p[0], p[1], p[2]};
    for (int c = 0; c < 3; ++c) p[c] = std::max(f[c * 3] * q[0] + f[c * 3 + 1] * q[1] + f[c * 3 + 2] * q[2], 0.0) * gain;
    if (!dcp.map.isEmpty()) hueSatMap(dcp.mapDims, dcp.map, dcp.mapSrgb, p);
    shoulder(p);
    if (!dcp.look.isEmpty()) hueSatMap(dcp.lookDims, dcp.look, dcp.lookSrgb, p);
    for (int c = 0; c < 3; ++c) p[c] = std::clamp(p[c], 0.0, 1.0);
    rgbTone(curve, p);
    // The preset's curves, on display values, as the source editor applies them.
    if (curves.finish)
        for (int c = 0; c < 3; ++c) {
            double e = srgbEncode(p[c]);
            if (!dcp.finishRegions.isEmpty()) e = curves.regions(e);
            if (!dcp.finishCurves[c].isEmpty()) e = curves.channel[c](e);
            p[c] = srgbDecode(e);
        }
}

// Where the profile's tables and the print tables are kept.
QString cacheDir() { return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/camera-profiles"); }
constexpr const char *kBakeVersion = "omaraw-dcp-bake-4";

QString key(const QString &s) {
    QString out;
    for (const QChar c : s.toLower()) if (c.isLetterOrNumber()) out += c;
    return out;
}

struct Index {
    QString folder;
    QString library;
    QHash<QString, QString> byCameraAndName;   // key(camera) + '\n' + key(name) -> file
    QVariantList entries;
    QHash<QString, QVariantList> compatible;
    qint64 scannedAt = 0;
};
QMutex g_lock;
Index g_index;

void scan(Index &index, const QString &folder) {
    index = Index{};
    index.folder = folder;
    index.library = cacheDir() + QStringLiteral("/sources");
    index.scannedAt = QDateTime::currentMSecsSinceEpoch();
    QStringList files;
    for (const QString &directory : {QStringLiteral(":/camera-profiles"), index.library, folder}) {
        if (directory.isEmpty()) continue;
        QDirIterator it(directory, {QStringLiteral("*.dcp"), QStringLiteral("*.DCP")}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) files << it.next();
    }
    files.removeDuplicates();
    std::sort(files.begin(), files.end(), [&](const QString &a, const QString &b) {
        const bool aIncluded = a.startsWith(":/camera-profiles/"), bIncluded = b.startsWith(":/camera-profiles/");
        if (aIncluded != bIncluded) return aIncluded;
        const bool aManaged = a.startsWith(index.library + '/'), bManaged = b.startsWith(index.library + '/');
        return aManaged != bManaged ? aManaged : a < b;
    });
    for (const QString &path : std::as_const(files)) {
        QFile f(path);
        if (f.size() > 16 * 1024 * 1024 || !f.open(QIODevice::ReadOnly)) continue;
        // The names sit near the start: read that much, the whole file only
        // when they do not.
        QByteArray bytes = f.read(16384);
        QString error;
        Reader head(bytes, true);
        bool ok = head.header(error) && !head.text(UniqueCameraModel).isEmpty() && !head.text(ProfileName).isEmpty();
        if (!ok) { bytes += f.readAll(); }
        Reader r(bytes, true);
        if (!r.header(error)) continue;
        const QString camera = r.text(UniqueCameraModel), name = r.text(ProfileName);
        if (camera.isEmpty() || name.isEmpty()) continue;
        const bool included = path.startsWith(":/camera-profiles/");
        index.entries << QVariantMap{{"key", path}, {"name", name}, {"camera", camera}, {"included", included},
                                    {"displayName", included ? QStringLiteral("Community colour") : name}};
        const QString k = key(camera) + QLatin1Char('\n') + key(name);
        if (!index.byCameraAndName.contains(k)) index.byCameraAndName.insert(k, path);
    }
}
} // namespace

bool parse(const QByteArray &bytes, Dcp &out, QString &error) {
    out = Dcp{};
    if (bytes.size() > 16 * 1024 * 1024) { error = QStringLiteral("larger than any camera profile"); return false; }
    Reader r(bytes);
    if (!r.header(error)) return false;
    out.camera = r.text(UniqueCameraModel);
    out.name = r.text(ProfileName);
    if (out.camera.isEmpty()) { error = QStringLiteral("names no camera"); return false; }
    // Of two colour maps (one per calibration light), the one nearer daylight.
    if (r.has(HueSatMapDims)) {
        quint16 data = r.has(HueSatMapData2) ? HueSatMapData2 : HueSatMapData1;
        if (r.has(HueSatMapData1) && r.has(HueSatMapData2)) {
            const auto daylight = [](double l) { return l == 21 || l == 20 || l == 23 || l == 1 || l == 4 || l == 9 || l == 22; };
            const QVector<double> i1 = r.numbers(CalibrationIlluminant1), i2 = r.numbers(CalibrationIlluminant2);
            if (!i1.isEmpty() && daylight(i1[0]) && !(i2.size() && daylight(i2[0]))) data = HueSatMapData1;
        }
        if (r.has(data) && !table(r, HueSatMapDims, data, out.mapDims, out.map, error)) return false;
        const QVector<double> enc = r.numbers(HueSatMapEncoding);
        out.mapSrgb = !enc.isEmpty() && enc[0] == 1;
    }
    if (r.has(LookTableDims) && r.has(LookTableData)) {
        if (!table(r, LookTableDims, LookTableData, out.lookDims, out.look, error)) return false;
        const QVector<double> enc = r.numbers(LookTableEncoding);
        out.lookSrgb = !enc.isEmpty() && enc[0] == 1;
    }
    const QVector<double> tone = r.numbers(ProfileToneCurve);
    if (tone.size() < 4 || tone.size() % 2 || tone.size() > 16384) {
        error = QStringLiteral("has no tone curve of its own; only profiles with one are supported");
        return false;
    }
    for (int i = 0; i < tone.size(); ++i) {
        if (!std::isfinite(tone[i]) || tone[i] < 0 || tone[i] > 1 || (i % 2 == 0 && i && tone[i] <= tone[i - 2])) {
            error = QStringLiteral("damaged tone curve");
            return false;
        }
        out.tone << float(tone[i]);
    }
    const M3 forward = forwardCorrection(r);
    for (int i = 0; i < 9; ++i) out.toForward[i] = forward[i / 3][i % 3];
    const QVector<double> offset = r.numbers(BaselineExposureOffset);
    if (!offset.isEmpty() && !std::isfinite(offset[0])) { error = QStringLiteral("damaged exposure offset"); return false; }
    out.exposureOffset = offset.isEmpty() ? 0 : std::clamp(offset[0], -4.0, 4.0);
    return true;
}

void render(const Dcp &dcp, const double in[3], double out[3]) {
    double p[3] = {in[0], in[1], in[2]};
    develop(dcp, Curves(dcp), p);
    for (int c = 0; c < 3; ++c) out[c] = p[c];
}

void renderWorking(const Dcp &dcp, const double in[3], double out[3]) {
    const M3 &to = rec2020ToProPhoto(), &back = proPhotoToRec2020();
    double p[3];
    for (int c = 0; c < 3; ++c) p[c] = to[c][0] * in[0] + to[c][1] * in[1] + to[c][2] * in[2];
    develop(dcp, Curves(dcp), p);
    for (int c = 0; c < 3; ++c) out[c] = back[c][0] * p[0] + back[c][1] * p[1] + back[c][2] * p[2];
}

QByteArray bake(const Dcp &dcp, int size) {
    size = std::clamp(size, 2, 256);
    // Scene light: log-shaped, 0.18 at the grey, from about 12 stops below
    // it to 4.5 above (the roll-off has reached white by then).
    const float grey = 0.18f, greyLogE = 0, nlMin = -3.6f, nlMax = 1.4f, llMin = -16.0f;
    // Printer lights and densities act on the cube's encoded output: a whole
    // printer-light step moves a colour by 1/16 of its range.
    const float apdMax = 16.0f;
    QByteArray out;
    const auto put = [&out](const void *p, int n) { out.append(static_cast<const char *>(p), n); };
    const auto u16 = [&](double v) { const quint16 x = quint16(std::lround(std::clamp(v, 0.0, 1.0) * 65535)); put(&x, 2); };
    put("OMPT", 4);
    // The cube holds display light sRGB-encoded, where neighbouring entries
    // differ about as much as they look; the output curve (b1, per colour)
    // turns that into the log light the module expects. A cube of log light
    // swung across its whole range between two entries wherever a saturated
    // colour took a channel near zero.
    const int n = 4096;
    const quint32 head[3] = {3, quint32(size), quint32(n)};
    put(head, sizeof head);
    const float consts[8] = {grey, greyLogE, nlMin, nlMax, 1.0f, apdMax, 1.0f, llMin};
    put(consts, sizeof consts);
    // Working Rec. 2020 to ProPhoto: the module computes e[c] = Σk p[k]·m[k·3+c].
    const M3 &m = rec2020ToProPhoto();
    float mm[9];
    for (int k = 0; k < 3; ++k) for (int c = 0; c < 3; ++c) mm[k * 3 + c] = float(m[c][k]);
    put(mm, sizeof mm);
    // a2: the log shape straight into the cube.
    for (int i = 0; i < n; ++i) for (int c = 0; c < 3; ++c) u16(i / double(n - 1));
    const Curves curves(dcp);
    const M3 &back = proPhotoToRec2020();
    for (int b = 0; b < size; ++b)
        for (int g = 0; g < size; ++g)
            for (int r = 0; r < size; ++r) {
                const int idx[3] = {r, g, b};
                double p[3];
                for (int c = 0; c < 3; ++c) p[c] = grey * std::pow(10.0, nlMin + (nlMax - nlMin) * idx[c] / double(size - 1));
                develop(dcp, curves, p);
                for (int c = 0; c < 3; ++c) {
                    const double v = back[c][0] * p[0] + back[c][1] * p[1] + back[c][2] * p[2];
                    u16(srgbEncode(std::clamp(v, 0.0, 1.0)));
                }
            }
    // b1: sRGB-encoded light to the module's log light.
    for (int i = 0; i < n; ++i) {
        const double light = srgbDecode(i / double(n - 1));
        for (int c = 0; c < 3; ++c) u16((std::log2(std::max(light, std::exp2(double(llMin)))) - llMin) / -llMin);
    }
    // b2: identity, the same size as the look (the module reads both so).
    for (int b = 0; b < size; ++b) for (int g = 0; g < size; ++g) for (int r = 0; r < size; ++r) {
        u16(r / double(size - 1)); u16(g / double(size - 1)); u16(b / double(size - 1));
    }
    return out;
}

bool setFinish(Dcp &dcp, const QVariantList &rows) {
    const auto pairs = [](const QVariantMap &c) {
        const QVariantList xs = c.value(QStringLiteral("xs")).toList(), ys = c.value(QStringLiteral("ys")).toList();
        QVector<float> out;
        double last = -1;
        for (int i = 0; i < std::min(xs.size(), ys.size()); ++i) {
            const double x = xs[i].toDouble(), y = ys[i].toDouble();
            if (!std::isfinite(x) || !std::isfinite(y) || x <= last || x < 0 || x > 1) return QVector<float>();
            out << float(x) << float(std::clamp(y, 0.0, 1.0));
            last = x;
        }
        return out.size() >= 4 ? out : QVector<float>();
    };
    bool any = false;
    for (const QVariant &v : rows) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("tonecurveset")).toBool() && m.value(QStringLiteral("enabled"), true).toBool()) {
            dcp.finishRegions = pairs(m);
            any |= !dcp.finishRegions.isEmpty();
        }
        if (m.value(QStringLiteral("curveset")).toBool() && m.value(QStringLiteral("enabled"), true).toBool()) {
            const QVariantList channels = m.value(QStringLiteral("channels")).toList();
            for (int c = 0; c < 3 && c < channels.size(); ++c) {
                dcp.finishCurves[c] = pairs(channels[c].toMap());
                any |= !dcp.finishCurves[c].isEmpty();
            }
        }
    }
    return any;
}

QString folder() { return QSettings().value(QStringLiteral("cameraProfiles/folder")).toString(); }
void setFolder(const QString &path) {
    QSettings().setValue(QStringLiteral("cameraProfiles/folder"), path);
    QMutexLocker lock(&g_lock);
    g_index = Index{};
}

void refresh() { QMutexLocker lock(&g_lock); g_index = Index{}; }

bool matchesCamera(const QString &profileCamera, const QString &camera, const QString &exifCamera) {
    const QString profile = key(profileCamera);
    return !profile.isEmpty() && (profile == key(camera) || profile == key(exifCamera));
}

QVariantList available(const QString &camera, const QString &exifCamera) {
    QMutexLocker lock(&g_lock);
    const QString dir = folder(), library = cacheDir() + QStringLiteral("/sources");
    if (!g_index.scannedAt || g_index.folder != dir || g_index.library != library) scan(g_index, dir);
    const QString identity = key(camera) + '\n' + key(exifCamera);
    if (g_index.compatible.contains(identity)) return g_index.compatible.value(identity);
    QVariantList result;
    QSet<QByteArray> seen;
    for (const auto &entry : std::as_const(g_index.entries)) {
        auto row = entry.toMap();
        if (!matchesCamera(row.value("camera").toString(), camera, exifCamera)) continue;
        QFile f(row.value("key").toString());
        Dcp profile; QString error;
        const bool readable = f.open(QIODevice::ReadOnly) && f.size() <= 16 * 1024 * 1024;
        const QByteArray bytes = readable ? f.read(16 * 1024 * 1024 + 1) : QByteArray();
        const auto hash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
        if (readable && seen.contains(hash)) continue;
        if (readable) seen.insert(hash);
        const bool valid = readable && parse(bytes, profile, error)
                           && matchesCamera(profile.camera, camera, exifCamera);
        row["supported"] = valid;
        row["error"] = valid ? QString() : error.isEmpty() ? QStringLiteral("unreadable or changed profile") : error;
        result << row;
    }
    std::sort(result.begin(), result.end(), [](const QVariant &a, const QVariant &b) {
        if (a.toMap().value("included") != b.toMap().value("included")) return a.toMap().value("included").toBool();
        const int compared = QString::compare(a.toMap().value("name").toString(), b.toMap().value("name").toString(), Qt::CaseInsensitive);
        return compared ? compared < 0 : a.toMap().value("key").toString() < b.toMap().value("key").toString();
    });
    g_index.compatible.insert(identity, result);
    return result;
}

QString importFile(const QString &source, QString *error) {
    const auto fail = [&](const QString &why) { if (error) *error = why; return QString(); };
    QFile input(source);
    if (QFileInfo(source).suffix().compare("dcp", Qt::CaseInsensitive) || !input.open(QIODevice::ReadOnly)
        || input.size() <= 0 || input.size() > 16 * 1024 * 1024) return fail(QStringLiteral("Choose a readable .dcp camera profile, up to 16 MB."));
    const QByteArray bytes = input.read(16 * 1024 * 1024 + 1);
    Dcp profile; QString why;
    if (!parse(bytes, profile, why)) return fail(why);
    if (profile.name.isEmpty()) return fail(QStringLiteral("This profile has no name."));
    const QString dir = cacheDir() + QStringLiteral("/sources");
    const QString path = dir + '/' + QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()) + ".dcp";
    if (!QDir().mkpath(dir)) return fail(QStringLiteral("Could not create the camera profile library."));
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) return fail(QStringLiteral("Could not save the camera profile."));
    refresh();
    return path;
}

QVariantMap tableDetails(const QString &tables) {
    QFile file(tables + ".json");
    if (!file.open(QIODevice::ReadOnly) || file.size() > 65536) return {};
    return QJsonDocument::fromJson(file.readAll()).object().toVariantMap();
}

int profileCount(int *cameras) {
    const QString dir = folder();
    QMutexLocker lock(&g_lock);
    scan(g_index, dir);
    // This count belongs to the user-library preferences, not the included
    // collection. Keep it useful when a person chooses/removes their folder.
    QSet<QString> bodies, profiles;
    for (const auto &v : std::as_const(g_index.entries)) {
        const auto row = v.toMap();
        if (row.value("included").toBool()) continue;
        bodies.insert(key(row.value("camera").toString()));
        profiles.insert(key(row.value("camera").toString()) + '\n' + key(row.value("name").toString()));
    }
    if (cameras) *cameras = bodies.size();
    return profiles.size();
}

bool isBuiltIn(const QString &profile) {
    const QString p = profile.simplified();
    return p.isEmpty() || p.startsWith(QLatin1String("Adobe "), Qt::CaseInsensitive) || p.startsWith(QLatin1String("Camera "), Qt::CaseInsensitive)
           || p.compare(QLatin1String("Embedded"), Qt::CaseInsensitive) == 0;
}

QString tablesFor(const QString &camera, const QString &exifCamera, const QString &profile, const QVariantList &finish, QString *error) {
    const auto fail = [error](const QString &why) { if (error) *error = why; return QString(); };
    const QString dir = folder();
    QString path;
    {
        QMutexLocker lock(&g_lock);
        const auto find = [&] {
            for (const QString &c : {camera, exifCamera}) {
                if (c.isEmpty()) continue;
                const QString p = g_index.byCameraAndName.value(key(c) + QLatin1Char('\n') + key(profile));
                if (!p.isEmpty()) return p;
            }
            return QString();
        };
        if (!g_index.scannedAt || g_index.folder != dir || g_index.library != cacheDir() + "/sources") scan(g_index, dir);
        path = find();
        // Profiles added since the last look: looked for again, not more
        // than every ten seconds.
        if (path.isEmpty() && QDateTime::currentMSecsSinceEpoch() - g_index.scannedAt > 10000) { scan(g_index, dir); path = find(); }
    }
    if (path.isEmpty())
        return fail(QStringLiteral("no \"%1\" profile for the %2 in %3").arg(profile, camera.isEmpty() ? exifCamera : camera, QDir::toNativeSeparators(dir)));
    return tablesFromFile(path, camera, exifCamera, finish, error);
}

QString tablesFromFile(const QString &path, const QString &camera, const QString &exifCamera, const QVariantList &finish, QString *error) {
    const auto fail = [error](const QString &why) { if (error) *error = why; return QString(); };
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return fail(QStringLiteral("cannot read %1").arg(path));
    if (f.size() > 16 * 1024 * 1024) return fail(QStringLiteral("camera profile is too large"));
    const QByteArray bytes = f.read(16 * 1024 * 1024 + 1);
    Dcp dcp;
    QString why;
    if (!parse(bytes, dcp, why)) return fail(QStringLiteral("%1: %2").arg(QFileInfo(path).fileName(), why));
    if (!matchesCamera(dcp.camera, camera, exifCamera)) return fail(QStringLiteral("this profile is for %1, not %2").arg(dcp.camera, camera.isEmpty() ? exifCamera : camera));
    // Named by the profile, the finish and the way it is baked: a change to
    // any of them is baked afresh, and an unchanged one is reused.
    setFinish(dcp, finish);
    QByteArray identity = bytes + kBakeVersion;
    for (const QVector<float> *curve : {&dcp.finishRegions, &dcp.finishCurves[0], &dcp.finishCurves[1], &dcp.finishCurves[2]})
        identity += QByteArray(reinterpret_cast<const char *>(curve->constData()), curve->size() * int(sizeof(float))) + '|';
    const QString name = QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha1).toHex());
    const QString out = cacheDir() + QLatin1Char('/') + name + QStringLiteral(".ompt");
    QDir().mkpath(cacheDir());
    QSaveFile metadata(out + ".json");
    const bool included = path.startsWith(":/camera-profiles/");
    const QByteArray description = QJsonDocument(QJsonObject{{"name", dcp.name}, {"camera", dcp.camera},
        {"source", included ? path : QFileInfo(path).absoluteFilePath()}, {"included", included},
        {"displayName", included ? QStringLiteral("Community colour") : dcp.name}}).toJson();
    if (!metadata.open(QIODevice::WriteOnly) || metadata.write(description) != description.size() || !metadata.commit()) return fail(QStringLiteral("cannot retain profile identification"));
    QFile cached(out);
    if (cached.open(QIODevice::ReadOnly) && cached.size() == 84 + 12LL * (81 * 81 * 81 + 4096)) {
        // OMPT is the native-endian engine cache format. Check the constants
        // as well as its size so selecting again repairs invalid containers.
        const QByteArray header = cached.read(84);
        if (header.size() == 84 && header.startsWith("OMPT")) {
            quint32 dimensions[3]; float values[17];
            std::memcpy(dimensions, header.constData() + 4, sizeof dimensions);
            std::memcpy(values, header.constData() + 16, sizeof values);
            const bool finite = std::all_of(std::begin(values), std::end(values), [](float v) { return std::isfinite(v); });
            if (dimensions[0] == 3 && dimensions[1] == 81 && dimensions[2] == 4096 && finite
                && values[0] > 0 && values[3] > values[2] && values[5] > 0 && values[7] < 0) return out;
        }
    }
    cached.close();
    QSaveFile save(out);
    const QByteArray tables = bake(dcp);
    if (!save.open(QIODevice::WriteOnly) || save.write(tables) != tables.size() || !save.commit()) return fail(QStringLiteral("cannot write %1").arg(out));
    return out;
}
}
