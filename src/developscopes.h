#pragma once
#include <QImage>
#include <QPainter>
#include <array>
#include <vector>
#include <cmath>

namespace DevelopScopes {
// Full-range BT.709 chroma: blue difference to the right, red difference up.
// Targets and traces use the same transform, including its screen-Y reversal.
inline QPointF vectorPoint(double r, double g, double b, int w, int h) {
    const double y = .2126 * r + .7152 * g + .0722 * b;
    return QPointF(w / 2. + (b - y) / 1.8556 * h * .84,
                   h / 2. - (r - y) / 1.5748 * h * .84);
}
inline void vectorGuides(QImage &image, bool large) {
    const int w = image.width(), h = image.height();
    const QPointF centre(w / 2., h / 2.);
    const double radius = h * .42;
    QPainter p(&image); p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(140, 150, 170, 90), large ? 1.5 : 1));
    p.drawEllipse(centre, radius, radius);
    p.drawEllipse(centre, radius / 2, radius / 2);
    const double cross = large ? 9 : 4;
    p.drawLine(centre - QPointF(cross, 0), centre + QPointF(cross, 0));
    p.drawLine(centre - QPointF(0, cross), centre + QPointF(0, cross));

    // A hue reference, not a skin detector or a universal correction target.
    // Conventional warm skin guide at approximately 123 degrees from +Cb.
    const double angle = 123. * 3.14159265358979323846 / 180.;
    const QPointF end = centre + QPointF(std::cos(angle), -std::sin(angle)) * radius;
    p.setPen(QPen(QColor(218, 176, 112), large ? 2 : 1, Qt::DashLine));
    p.drawLine(centre, end);
    QFont font = p.font(); font.setPixelSize(large ? 22 : 13); p.setFont(font);
    p.drawText(QRectF(end.x() - (large ? 65 : 36), end.y() - (large ? 24 : 15),
                     large ? 60 : 34, large ? 24 : 15), Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("Skin"));

    // 75% colour-bar targets derived from the same BT.709 coefficients.
    struct Target { double r, g, b; const char *name; QRgb colour; };
    const Target targets[] = {
        {.75, 0, 0, "R", qRgb(224, 108, 108)}, {.75, .75, 0, "Y", qRgb(218, 200, 90)},
        {0, .75, 0, "G", qRgb(95, 191, 102)}, {0, .75, .75, "C", qRgb(90, 193, 204)},
        {0, 0, .75, "B", qRgb(108, 143, 224)}, {.75, 0, .75, "M", qRgb(202, 121, 211)}
    };
    const double box = large ? 8 : 3;
    for (const auto &t : targets) {
        const QPointF at = vectorPoint(t.r, t.g, t.b, w, h);
        const QColor colour(t.colour);
        p.setPen(QPen(colour, large ? 2 : 1));
        p.setBrush(QColor(colour.red(), colour.green(), colour.blue(), 35));
        p.drawRect(QRectF(at.x() - box, at.y() - box, box * 2, box * 2));
        const double labelY = at.y() < centre.y() ? at.y() - box - (large ? 28 : 17) : at.y() + box + 2;
        p.drawText(QRectF(at.x() - 18, labelY, 36, large ? 26 : 15), Qt::AlignCenter, QString::fromLatin1(t.name));
    }
}
// Bounded analysis of the accepted sRGB preview; no additional RAW rendering.
// 1 luminance waveform, 2 RGB parade, 3 BT.709 Cb/Cr vectorscope.
inline QImage render(const QImage &image, int mode, bool large = false) {
    if (image.isNull() || mode < 1 || mode > 3) return {};
    const int w = large ? 900 : 300, h = large ? 480 : 160;
    const QImage input = image.scaled(large ? 1200 : 600, large ? 800 : 400,
        Qt::KeepAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32);
    // Bounded heap storage: 576 KB compact, about 5 MB for the large view.
    std::vector<std::array<unsigned, 3>> counts(w * h, std::array<unsigned, 3>{});
    auto plot = [&](int x, int y, int channel) { ++counts[qBound(0, y, h - 1) * w + qBound(0, x, w - 1)][channel]; };
    for (int y = 0; y < input.height(); ++y) {
        const auto *row = reinterpret_cast<const QRgb *>(input.constScanLine(y));
        for (int x = 0; x < input.width(); ++x) {
            const double r = qRed(row[x]) / 255., g = qGreen(row[x]) / 255., b = qBlue(row[x]) / 255.;
            const double luma = .2126 * r + .7152 * g + .0722 * b;
            if (mode == 1) for (int c = 0; c < 3; ++c) plot(x * w / input.width(), qRound((1 - luma) * (h - 1)), c);
            else if (mode == 2) {
                const double values[] = {r, g, b};
                for (int c = 0; c < 3; ++c) plot(c * (w / 3) + x * (w / 3) / input.width(), qRound((1 - values[c]) * (h - 1)), c);
            } else {
                const QPointF at = vectorPoint(r, g, b, w, h);
                for (int c = 0; c < 3; ++c) plot(qRound(at.x()), qRound(at.y()), c);
            }
        }
    }
    QImage output(w, h, QImage::Format_RGB32); output.fill(QColor(20, 24, 30));
    if (mode == 3) vectorGuides(output, large);
    for (int y = 0; y < h; ++y) {
        auto *row = reinterpret_cast<QRgb *>(output.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const auto &v = counts[y * w + x];
            auto intensity = [](unsigned n) { return n ? qMin(255, 80 + int(38 * std::log1p(n))) : 20; };
            if (v[0] || v[1] || v[2]) row[x] = qRgb(intensity(v[0]), intensity(v[1]), intensity(v[2]));
        }
    }
    QPainter p(&output); p.setPen(QColor(140, 150, 170, 90));
    if (mode != 3) {
        for (int y : {0, h/2, h-1}) p.drawLine(0,y,w-1,y);
        if (mode == 2) { p.drawLine(w/3,0,w/3,h-1); p.drawLine(w*2/3,0,w*2/3,h-1); }
    }
    return output;
}
}
