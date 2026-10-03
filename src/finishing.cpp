#include "finishing.h"

#include <QColorSpace>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QDebug>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <thread>
#include <vector>


namespace Finishing {

Options Options::fromMap(const QVariantMap &m) {
    Options o;
    o.sharpen = m.value(QStringLiteral("sharpen"), o.sharpen).toInt();
    o.amount = m.value(QStringLiteral("amount"), o.amount).toInt();
    o.text = m.value(QStringLiteral("text")).toString();
    o.imagePath = m.value(QStringLiteral("imagePath")).toString();
    o.anchor = m.value(QStringLiteral("anchor"), o.anchor).toInt();
    o.size = m.value(QStringLiteral("size"), o.size).toDouble();
    o.opacity = m.value(QStringLiteral("opacity"), o.opacity).toDouble();
    o.margin = m.value(QStringLiteral("margin"), o.margin).toDouble();
    o.jpegQuality = m.value(QStringLiteral("quality"), o.jpegQuality).toInt();
    return o;
}

QVariantMap Options::toMap() const {
    QVariantMap m;
    m[QStringLiteral("sharpen")] = sharpen;
    m[QStringLiteral("amount")] = amount;
    m[QStringLiteral("text")] = text;
    m[QStringLiteral("imagePath")] = imagePath;
    m[QStringLiteral("anchor")] = anchor;
    m[QStringLiteral("size")] = size;
    m[QStringLiteral("opacity")] = opacity;
    m[QStringLiteral("margin")] = margin;
    m[QStringLiteral("quality")] = jpegQuality;
    return m;
}

void sharpenParams(const Options &o, int longEdge, double *radius, double *amount) {
    // Screen: a tight halo that reads on a display. Print: wider, because
    // paper and the eye at arm's length swallow the fine one; scaled with
    // the size so a 4000 px print gets more than a 1200 px proof.
    const double strength[] = {0.5, 1.0, 1.6};
    const double a = strength[std::clamp(o.amount, 0, 2)];
    if (o.sharpen == 2) { *radius = std::clamp(longEdge / 2000.0, 0.8, 2.5); *amount = a * 1.2; }
    else { *radius = 0.7; *amount = a; }
}

// Separable gaussian blur + unsharp mask on float channels, rows split
// across threads. The export hands over its float pixels, so nothing is
// rounded before the writer's own quantisation.
void sharpenFloat(float *rgba, int width, int height, double radius, double amount) {
    if (!rgba || width < 3 || height < 3 || radius <= 0 || amount <= 0) return;
    const int r = std::max(1, int(std::ceil(radius * 2.5)));
    std::vector<double> k(2 * r + 1);
    double sum = 0;
    for (int i = -r; i <= r; ++i) { k[i + r] = std::exp(-(i * i) / (2 * radius * radius)); sum += k[i + r]; }
    for (double &v : k) v /= sum;
    const int w = width, h = height;
    std::vector<float> tmp(size_t(w) * h * 4), blur(size_t(w) * h * 4);
    const int threads = std::max(1, std::min(int(std::thread::hardware_concurrency()), 16));
    auto bands = [&](auto fn) {
        std::vector<std::thread> pool;
        const int step = (h + threads - 1) / threads;
        for (int t = 0; t < threads; ++t) pool.emplace_back([=, &fn] { fn(t * step, std::min(h, (t + 1) * step)); });
        for (auto &t : pool) t.join();
    };
    bands([&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const float *line = rgba + size_t(y) * w * 4;
            float *o = tmp.data() + size_t(y) * w * 4;
            for (int x = 0; x < w; ++x) {
                double acc[3] = {0, 0, 0};
                for (int i = -r; i <= r; ++i) {
                    const int xx = std::clamp(x + i, 0, w - 1);
                    const float *p = line + xx * 4;
                    acc[0] += p[0] * k[i + r]; acc[1] += p[1] * k[i + r]; acc[2] += p[2] * k[i + r];
                }
                o[x * 4] = float(acc[0]); o[x * 4 + 1] = float(acc[1]); o[x * 4 + 2] = float(acc[2]); o[x * 4 + 3] = line[x * 4 + 3];
            }
        }
    });
    bands([&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            float *o = blur.data() + size_t(y) * w * 4;
            for (int x = 0; x < w; ++x) {
                double acc[3] = {0, 0, 0};
                for (int i = -r; i <= r; ++i) {
                    const int yy = std::clamp(y + i, 0, h - 1);
                    const float *p = tmp.data() + (size_t(yy) * w + x) * 4;
                    acc[0] += p[0] * k[i + r]; acc[1] += p[1] * k[i + r]; acc[2] += p[2] * k[i + r];
                }
                o[x * 4] = float(acc[0]); o[x * 4 + 1] = float(acc[1]); o[x * 4 + 2] = float(acc[2]);
            }
        }
    });
    // unsharp mask: the original plus amount times the detail it lost
    bands([&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            float *line = rgba + size_t(y) * w * 4;
            const float *b = blur.data() + size_t(y) * w * 4;
            for (int x = 0; x < w * 4; ++x) {
                if ((x & 3) == 3) continue;
                const float v = line[x] + float(amount) * (line[x] - b[x]);
                line[x] = std::isfinite(v) ? v : line[x];
            }
        }
    });
}

QImage sharpen(const QImage &srcIn, double radius, double amount) {
    if (srcIn.isNull()) return srcIn;
    QImage work = srcIn.convertToFormat(QImage::Format_RGBA32FPx4);
    sharpenFloat(reinterpret_cast<float *>(work.bits()), work.width(), work.height(), radius, amount);
    if (srcIn.format() == QImage::Format_RGBA32FPx4) return work;
    return work.convertToFormat(srcIn.format() == QImage::Format_Invalid ? QImage::Format_RGB32 : srcIn.format());
}

static QRectF placed(const QSizeF &box, const QSize &canvas, const Options &o) {
    const double edge = std::max(canvas.width(), canvas.height());
    const double m = edge * o.margin / 100.0;
    const int col = std::clamp(o.anchor, 0, 8) % 3, row = std::clamp(o.anchor, 0, 8) / 3;
    const double x = col == 0 ? m : col == 1 ? (canvas.width() - box.width()) / 2 : canvas.width() - m - box.width();
    const double y = row == 0 ? m : row == 1 ? (canvas.height() - box.height()) / 2 : canvas.height() - m - box.height();
    return QRectF(x, y, box.width(), box.height());
}

void drawWatermark(QImage &img, const Options &o) {
    const QString text = o.text.trimmed();
    QImage mark;
    if (!o.imagePath.isEmpty()) mark.load(o.imagePath);
    if (!mark.isNull() && img.colorSpace().isValid()) {
        if (!mark.colorSpace().isValid()) mark.setColorSpace(QColorSpace::SRgb);
        // In float first: linear light at 8 bits would collapse a logo's
        // darkest codes (sRGB 0..50) into a handful of levels.
        if (mark.depth() <= 32) mark = mark.convertToFormat(QImage::Format_RGBA32FPx4);
        const QImage converted = mark.convertedToColorSpace(img.colorSpace());
        if (!converted.isNull()) mark = converted;
    }
    if (text.isEmpty() && mark.isNull()) return;
    // QPainter draws on 8-bit, 16-bit and float premultiplied surfaces;
    // keep the depth the picture came with. An opaque float export is the
    // same bytes premultiplied, so it is drawn on in place.
    const bool floating = img.format() == QImage::Format_RGBA32FPx4 || img.format() == QImage::Format_RGBA32FPx4_Premultiplied || img.format() == QImage::Format_RGBX32FPx4;
    const bool wide = img.depth() > 32;
    const QImage::Format work = floating ? QImage::Format_RGBA32FPx4_Premultiplied : wide ? QImage::Format_RGBA64_Premultiplied : QImage::Format_ARGB32_Premultiplied;
    const QImage::Format back = img.format();
    if (img.format() != work) img = img.convertToFormat(work);
    const double edge = std::max(img.width(), img.height());
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setOpacity(std::clamp(o.opacity, 0.0, 1.0));
    if (!mark.isNull()) {
        const double w = edge * o.size / 100.0 * 4.0; // an image mark is wider than a line of text
        const QSizeF box(w, w * mark.height() / std::max(1, mark.width()));
        p.drawImage(placed(box, img.size(), o), mark);
    }
    if (!text.isEmpty()) {
        QFont f;
        f.setPixelSize(std::max(8, int(edge * o.size / 100.0)));
        f.setWeight(QFont::DemiBold);
        const QFontMetricsF fm(f);
        const QSizeF box(fm.horizontalAdvance(text), fm.height());
        QRectF r = placed(box, img.size(), o);
        if (!mark.isNull()) {
            // Text sits under an image mark when both are given.
            const double w = edge * o.size / 100.0 * 4.0;
            const double mh = w * mark.height() / std::max(1, mark.width());
            if (o.anchor / 3 == 2) r.moveTop(r.top() - mh - fm.height() * 0.2); else r.moveTop(r.top() + mh + fm.height() * 0.2);
        }
        QPainterPath path;
        path.addText(r.left(), r.top() + fm.ascent(), f, text);
        // A soft dark edge keeps white text legible on a bright sky.
        p.setPen(QPen(QColor(0, 0, 0, 140), std::max(1.0, f.pixelSize() / 18.0)));
        p.setBrush(Qt::white);
        p.drawPath(path);
    }
    p.end();
    if (back != work && back != QImage::Format_Invalid) img = img.convertToFormat(back);
}

// The sRGB curve, extended through zero and above one so highlights past
// white and out-of-gamut negatives come back unchanged.
static inline float encodeSrgb(float v) { const float a = std::fabs(v); const float e = a <= 0.0031308f ? a * 12.92f : 1.055f * std::pow(a, 1.f / 2.4f) - 0.055f; return std::copysign(e, v); }
static inline float decodeSrgb(float v) { const float a = std::fabs(v); const float d = a <= 0.04045f ? a / 12.92f : std::pow((a + 0.055f) / 1.055f, 2.4f); return std::copysign(d, v); }

void applyFloat(float *rgba, int width, int height, const Options &o, const QColorSpace &output) {
    Q_UNUSED(output)
    if (!rgba || width < 1 || height < 1 || !o.active()) return;
    // The export hands over linear light (Rec. 709 primaries); the output
    // profile's encoding is applied after this, by the writer.
    const size_t count = size_t(width) * size_t(height);
    if (o.sharpen > 0) {
        double radius = 0, amount = 0;
        sharpenParams(o, std::max(width, height), &radius, &amount);
        // Sharpened on a perceptual scale, as an eye judges an edge: in linear
        // light the dark side of every halo would be far stronger than the
        // light side.
        for (size_t i = 0; i < count; ++i) for (int c = 0; c < 3; ++c) rgba[4 * i + c] = encodeSrgb(rgba[4 * i + c]);
        sharpenFloat(rgba, width, height, radius, amount);
        for (size_t i = 0; i < count; ++i) for (int c = 0; c < 3; ++c) rgba[4 * i + c] = decodeSrgb(rgba[4 * i + c]);
    }
    if (o.text.trimmed().isEmpty() && o.imagePath.isEmpty()) return;
    // A view over the export's pixels: no copy, drawn on in place. It is
    // linear, and says so, so an image mark is converted to linear before it
    // is drawn; tagging it with the output's encoding encoded the mark twice.
    QImage view(reinterpret_cast<uchar *>(rgba), width, height, width * 4 * int(sizeof(float)), QImage::Format_RGBA32FPx4_Premultiplied);
    view.setColorSpace(QColorSpace::SRgbLinear);
    drawWatermark(view, o);
}

} // namespace Finishing
