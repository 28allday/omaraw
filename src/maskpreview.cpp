#include "maskpreview.h"

#include <QColor>
#include <cmath>

namespace {

// darktable's circle: quadratic falloff between the radius and the outer
// edge of the feathering, squared. Distances are in pixels, with radius
// and border as fractions of the short side.
float circleAt(float px, float py, float cx, float cy, float radius, float border, float mindim) {
    const float r = radius * mindim, t = (radius + border) * mindim;
    const float r2 = r * r, t2 = t * t;
    const float b2 = t2 - r2;
    if (b2 <= 0.f) return (px - cx) * (px - cx) + (py - cy) * (py - cy) <= r2 ? 1.f : 0.f;
    const float l2 = (px - cx) * (px - cx) + (py - cy) * (py - cy);
    const float f = qBound(0.f, (t2 - l2) / b2, 1.f);
    return f * f;
}

// darktable's gradient: half an error function across the line, with the
// distance normalised by the frame diagonal.
float gradientAt(float px, float py, float ax, float ay, float rotation, float compression,
                 float w, float h) {
    const float hw = 1.f / std::hypot(w, h);
    const float v = rotation * float(M_PI) / 180.f;
    const float cosv = std::cos(v), sinv = std::sin(v);
    // Perpendicular distance from the line through the anchor, signed.
    const float dx = px - ax, dy = py - ay;
    const float dist = (-sinv * dx - cosv * dy) * hw;
    const float c = qMax(0.001f, compression);
    return qBound(0.f, 0.5f + 0.5f * std::erf(dist / c), 1.f);
}

// A stroke is the distance to its polyline: solid out to hardness × the
// half width, then the circle's falloff to nothing at the full width.
float brushAt(float px, float py, const QVariantList &nodes, float size, float hardness,
              float w, float h, float mindim) {
    if (nodes.isEmpty()) return 0.f;
    const float half = qMax(0.002f, size) * mindim;
    const float solid = half * qBound(0.05f, hardness, 1.f);
    float best = std::numeric_limits<float>::max();
    QPointF prev;
    for (int i = 0; i < nodes.size(); ++i) {
        const QVariantMap n = nodes[i].toMap();
        const QPointF p(n.value(QStringLiteral("x")).toDouble() * w, n.value(QStringLiteral("y")).toDouble() * h);
        if (i == 0) { best = qMin(best, float(std::hypot(px - p.x(), py - p.y()))); prev = p; continue; }
        // distance from the point to the segment prev→p
        const double vx = p.x() - prev.x(), vy = p.y() - prev.y();
        const double len2 = vx * vx + vy * vy;
        double t = len2 > 0 ? ((px - prev.x()) * vx + (py - prev.y()) * vy) / len2 : 0.0;
        t = qBound(0.0, t, 1.0);
        const double qx = prev.x() + t * vx, qy = prev.y() + t * vy;
        best = qMin(best, float(std::hypot(px - qx, py - qy)));
        prev = p;
    }
    if (best <= solid) return 1.f;
    if (best >= half) return 0.f;
    const float f = (half - best) / qMax(0.001f, half - solid);
    return f * f;
}

// One blendif trapezoid: nothing below p0, climbing to p1, flat to p2,
// falling to nothing at p3.
float bandAt(float v, float p0, float p1, float p2, float p3, bool inverse) {
    float f;
    if (v <= p0) f = 0.f;
    else if (v < p1) f = (v - p0) / qMax(0.001f, p1 - p0);
    else if (v <= p2) f = 1.f;
    else if (v < p3) f = 1.f - (v - p2) / qMax(0.001f, p3 - p2);
    else f = 0.f;
    // Open ends must not clip: a band starting at 0 keeps everything below it.
    if (p0 <= 0.f && p1 <= 0.f && v <= p1) f = 1.f;
    if (p2 >= 1.f && p3 >= 1.f && v >= p2) f = 1.f;
    return inverse ? 1.f - f : f;
}

// Separable gaussian over a coverage map, sigma in preview pixels; the
// edges clamp, as darktable's does.
void blurCoverage(QVector<float> &cov, int w, int h, float sigma) {
    if (sigma < 0.3f) return;
    const int r = qMin(int(sigma * 3.f + 1.f), qMax(w, h));
    QVector<float> k(2 * r + 1);
    float sum = 0.f;
    for (int i = -r; i <= r; ++i) { k[i + r] = std::exp(-0.5f * float(i * i) / (sigma * sigma)); sum += k[i + r]; }
    for (float &v : k) v /= sum;
    QVector<float> tmp(cov.size());
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        float acc = 0.f;
        for (int i = -r; i <= r; ++i) acc += cov[y * w + qBound(0, x + i, w - 1)] * k[i + r];
        tmp[y * w + x] = acc;
    }
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        float acc = 0.f;
        for (int i = -r; i <= r; ++i) acc += tmp[qBound(0, y + i, h - 1) * w + x] * k[i + r];
        cov[y * w + x] = acc;
    }
}

// darktable's mask tone curve: brightness shifts the mask, contrast
// steepens it around the middle (at opacity 1).
float maskCurve(float v, float contrast, float brightness) {
    const float e = std::exp(3.f * contrast);
    float x = 2.f * v - 1.f;
    if (1.f - brightness <= 0.f) x = v <= 1e-6f ? -1.f : 1.f;
    else if (1.f + brightness <= 0.f) x = v >= 1.f - 1e-6f ? 1.f : -1.f;
    else if (brightness > 0.f) x = qMin((x + brightness) / (1.f - brightness), 1.f);
    else x = qMax((x + brightness) / (1.f + brightness), -1.f);
    const float c = 0.5f * (x * e / (1.f + (e - 1.f) * std::fabs(x))) + 0.5f;
    return c > 1e-6f ? qBound(0.f, c, 1.f) : 0.f;
}

}

QVector<float> MaskPreview::coverage(const QVariantList &shapes, const QVariantList &ranges, bool inverted,
                                     const QImage &render, int w, int h, const Refine &refine) {
    QVector<float> out(qMax(0, w * h), 0.f);
    if (w <= 0 || h <= 0) return out;
    const float mindim = float(qMin(w, h));

    // No drawn shape at all means the mask starts uniform and only the
    // ranges narrow it — the same as darktable with MASK off.
    if (shapes.isEmpty()) out.fill(1.f);

    // A bypassed shape is skipped, and whichever live shape comes first
    // has nothing under it, so it is a union whatever its operator says —
    // the same two rules the bridge applies to the group.
    bool liveSeen = false;
    for (int s = 0; s < shapes.size(); ++s) {
        const QVariantMap sh = shapes[s].toMap();
        if (!sh.value(QStringLiteral("enabled"), true).toBool()) continue;
        const int type = sh.value(QStringLiteral("shape")).toInt();
        const int combine = liveSeen ? sh.value(QStringLiteral("combine")).toInt() : 0;
        liveSeen = true;
        const bool inv = sh.value(QStringLiteral("inverse")).toBool();
        const float opacity = float(sh.value(QStringLiteral("opacity"), 1.0).toDouble());
        // Brush flow scales the stroke before it meets the rest.
        const float flow = type == 3 ? float(sh.value(QStringLiteral("flow"), 1.0).toDouble()) : 1.f;
        const float cx = float(sh.value(QStringLiteral("cx")).toDouble()) * w;
        const float cy = float(sh.value(QStringLiteral("cy")).toDouble()) * h;
        const QVariantList nodes = sh.value(QStringLiteral("points")).toList();
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const float px = x + 0.5f, py = y + 0.5f;
                float v = 0.f;
                if (type == 1) v = circleAt(px, py, cx, cy, float(sh.value(QStringLiteral("radius")).toDouble()),
                                            float(sh.value(QStringLiteral("border")).toDouble()), mindim);
                else if (type == 2) v = gradientAt(px, py, cx, cy, float(sh.value(QStringLiteral("rotation")).toDouble()),
                                                   float(sh.value(QStringLiteral("compression")).toDouble()), float(w), float(h));
                else if (type == 3) v = brushAt(px, py, nodes, float(sh.value(QStringLiteral("size")).toDouble()),
                                                float(sh.value(QStringLiteral("hardness"), 0.6).toDouble()), float(w), float(h), mindim);
                v *= flow;
                if (inv) v = 1.f - v;
                v *= opacity;
                float &acc = out[y * w + x];
                switch (combine) {
                case 1: acc = (acc > 0.f && v > 0.f) ? qMin(acc, v) : 0.f; break;             // intersect
                case 2: if (acc > 0.f && v > 0.f) acc = acc * (1.f - v); break;               // subtract
                case 3: acc = (acc > 0.f && v > 0.f) ? qMax((1.f - acc) * v, acc * (1.f - v)) // exclude
                                                     : qMax(acc, v); break;
                default: acc = qMax(acc, v); break;                                           // add
                }
            }
        }
    }

    // The ranges multiply in, read off the render. Hue and chroma come
    // from HSV, which is what the display-referred channels amount to.
    QVector<int> active;
    for (const QVariant &rv : ranges) if (rv.toMap().value(QStringLiteral("active")).toBool()) active << ranges.indexOf(rv);
    if (!active.isEmpty() && !render.isNull()) {
        const QImage src = render.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32);
        for (int y = 0; y < h; ++y) {
            const QRgb *row = reinterpret_cast<const QRgb *>(src.constScanLine(y));
            for (int x = 0; x < w; ++x) {
                float &acc = out[y * w + x];
                if (acc <= 0.f) continue;
                const QColor c = QColor::fromRgb(row[x]);
                for (int i : active) {
                    const QVariantMap r = ranges[i].toMap();
                    const int ch = r.value(QStringLiteral("channel")).toInt();
                    float v = 0.f;
                    if (ch == 0) v = float(qGray(row[x])) / 255.f;
                    else if (ch == 1) v = c.hueF() < 0 ? 0.f : float(c.hueF());
                    else v = float(c.saturationF());
                    acc *= bandAt(v, float(r.value(QStringLiteral("p0")).toDouble()), float(r.value(QStringLiteral("p1")).toDouble()),
                                  float(r.value(QStringLiteral("p2")).toDouble()), float(r.value(QStringLiteral("p3")).toDouble()),
                                  r.value(QStringLiteral("inverse")).toBool());
                }
            }
        }
    }

    if (inverted) for (float &v : out) v = 1.f - v;

    // Refinement last, on the finished mask, in darktable's order: blur,
    // then the tone curve.
    if (refine.blur > 0.1) blurCoverage(out, w, h, float(refine.blur * refine.scale));
    if (refine.contrast != 0 || refine.brightness != 0)
        for (float &v : out) v = maskCurve(v, float(refine.contrast), float(refine.brightness));
    return out;
}

QImage MaskPreview::overlay(const QVariantList &shapes, const QVariantList &ranges, bool inverted,
                            const QImage &render, int w, int h, const QColor &colour, double strength,
                            const Refine &refine) {
    QImage img(qMax(1, w), qMax(1, h), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    if (w <= 0 || h <= 0) return img;
    const QVector<float> cov = coverage(shapes, ranges, inverted, render, w, h, refine);
    const int r = colour.red(), g = colour.green(), b = colour.blue();
    for (int y = 0; y < h; ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const float a = qBound(0.f, cov[y * w + x] * float(strength), 1.f);
            // Premultiplied: the colour carries the alpha with it.
            row[x] = qRgba(int(r * a), int(g * a), int(b * a), int(a * 255));
        }
    }
    return img;
}
