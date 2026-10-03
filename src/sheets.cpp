#include "sheets.h"

#include <QColorSpace>
#include <QDateTime>
#include <QFileInfo>
#include <QFont>
#include <QFontMetricsF>
#include <QImageReader>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QUrl>
#include "proof.h"
#include <memory>
#include <QPagedPaintDevice>
#include <algorithm>

namespace Sheets {

Options Options::fromMap(const QVariantMap &m) {
    Options o;
    o.paper = m.value(QStringLiteral("paper"), o.paper).toString();
    o.orientation = m.value(QStringLiteral("orientation"), o.orientation).toString();
    o.marginMm = m.value(QStringLiteral("margin"), o.marginMm).toDouble();
    o.columns = std::clamp(m.value(QStringLiteral("columns"), o.columns).toInt(), 1, 12);
    o.rows = std::clamp(m.value(QStringLiteral("rows"), o.rows).toInt(), 1, 12);
    o.captions = m.value(QStringLiteral("captions"), o.captions).toBool();
    o.title = m.value(QStringLiteral("title")).toString();
    o.dpi = std::clamp(m.value(QStringLiteral("dpi"), o.dpi).toInt(), 72, 600);
    QString icc = m.value(QStringLiteral("icc")).toString();
    if (icc.startsWith(QLatin1String("file:"))) icc = QUrl(icc).toLocalFile();
    o.icc = icc;
    o.intent = std::clamp(m.value(QStringLiteral("intent"), o.intent).toInt(), 0, 3);
    o.blackPoint = m.value(QStringLiteral("blackPoint"), o.blackPoint).toBool();
    return o;
}

QVariantMap Options::toMap() const {
    QVariantMap m;
    m[QStringLiteral("paper")] = paper; m[QStringLiteral("orientation")] = orientation;
    m[QStringLiteral("margin")] = marginMm; m[QStringLiteral("columns")] = columns; m[QStringLiteral("rows")] = rows;
    m[QStringLiteral("captions")] = captions; m[QStringLiteral("title")] = title; m[QStringLiteral("dpi")] = dpi;
    m[QStringLiteral("icc")] = icc; m[QStringLiteral("intent")] = intent; m[QStringLiteral("blackPoint")] = blackPoint;
    return m;
}

QStringList Options::papers() {
    return {QStringLiteral("A4"), QStringLiteral("A3"), QStringLiteral("A5"), QStringLiteral("Letter"), QStringLiteral("Legal"),
            QStringLiteral("4x6"), QStringLiteral("5x7"), QStringLiteral("8x10")};
}

static QPageSize pageSizeFor(const QString &paper) {
    if (paper == QLatin1String("A3")) return QPageSize(QPageSize::A3);
    if (paper == QLatin1String("A5")) return QPageSize(QPageSize::A5);
    if (paper == QLatin1String("Letter")) return QPageSize(QPageSize::Letter);
    if (paper == QLatin1String("Legal")) return QPageSize(QPageSize::Legal);
    if (paper == QLatin1String("4x6")) return QPageSize(QSizeF(4, 6), QPageSize::Inch, QStringLiteral("4 × 6 in"));
    if (paper == QLatin1String("5x7")) return QPageSize(QSizeF(5, 7), QPageSize::Inch, QStringLiteral("5 × 7 in"));
    if (paper == QLatin1String("8x10")) return QPageSize(QSizeF(8, 10), QPageSize::Inch, QStringLiteral("8 × 10 in"));
    return QPageSize(QPageSize::A4);
}

int longEdgePixels(const Options &o) {
    const QSizeF in = pageSizeFor(o.paper).size(QPageSize::Inch);
    return int(std::ceil(std::max(in.width(), in.height()) * o.dpi));
}

namespace {
// Shared PDF page painter for contact sheets and full-resolution layouts.
struct Writer {
    std::unique_ptr<QPdfWriter> pdfOwned;
    QPagedPaintDevice *dev = nullptr;
    QPainter painter;
    int pages = 0;
    Writer(const QString &path, const Options &o, QPageLayout::Orientation orient) : pdfOwned(new QPdfWriter(path)), dev(pdfOwned.get()) {
        pdfOwned->setResolution(o.dpi);
        pdfOwned->setCreator(QStringLiteral("OmaRAW"));
        pdfOwned->setTitle(o.title.isEmpty() ? QStringLiteral("OmaRAW") : o.title);
        setup(o, orient);
    }
    void setup(const Options &o, QPageLayout::Orientation orient) {
        dev->setPageSize(pageSizeFor(o.paper));
        dev->setPageOrientation(orient);
        dev->setPageMargins(QMarginsF(o.marginMm, o.marginMm, o.marginMm, o.marginMm), QPageLayout::Millimeter);
    }
    bool begin() { const bool ok = painter.begin(dev); if (ok) { pages = 1; painter.setRenderHint(QPainter::SmoothPixmapTransform); } return ok; }
    void newPage(QPageLayout::Orientation orient) { dev->setPageOrientation(orient); dev->newPage(); ++pages; }
    int resolution() const { return dev->logicalDpiX(); }
    // In the painter's own coordinates: the PDF writer already puts
    // the origin at the top-left of the area inside the margins, so the
    // page's paint rectangle (in page coordinates) would add them twice.
    QRectF paintRect() const { return QRectF(QPointF(0, 0), QSizeF(dev->pageLayout().paintRectPixels(resolution()).size())); }
    void end() { painter.end(); }
    QPaintDevice *fontDevice() const { return dev; }
};

QRectF fitted(const QSize &img, const QRectF &box) {
    if (img.isEmpty() || box.isEmpty()) return QRectF();
    const double s = std::min(box.width() / img.width(), box.height() / img.height());
    const QSizeF sz(img.width() * s, img.height() * s);
    return QRectF(box.left() + (box.width() - sz.width()) / 2, box.top() + (box.height() - sz.height()) / 2, sz.width(), sz.height());
}

QPageLayout::Orientation orientFor(const Options &o, const QSize &img, bool sheet) {
    if (o.orientation == QLatin1String("portrait")) return QPageLayout::Portrait;
    if (o.orientation == QLatin1String("landscape")) return QPageLayout::Landscape;
    if (sheet) return QPageLayout::Landscape;
    return img.width() >= img.height() ? QPageLayout::Landscape : QPageLayout::Portrait;
}
} // namespace

static QString toPaper(QList<QImage> &images, const Options &o);

QString contactSheet(const QList<QImage> &images, const QStringList &captions, const QString &pdfPath, const Options &o, int *pagesOut) {
    QList<QImage> paper = images;
    if (const QString e = toPaper(paper, o); !e.isEmpty()) return e;
    const QList<QImage> &pics = paper;
    if (pics.isEmpty()) return QStringLiteral("nothing to lay out");
    Writer w(pdfPath, o, orientFor(o, QSize(), true));
    if (!w.begin()) return QStringLiteral("cannot write %1").arg(pdfPath);
    const double pt = o.dpi / 72.0; // device pixels per point
    QFont caption; caption.setPointSizeF(7);
    QFont head; head.setPointSizeF(10); head.setWeight(QFont::DemiBold);
    const int perPage = o.columns * o.rows;
    for (int i = 0; i < pics.size(); ++i) {
        const int slot = i % perPage;
        if (i > 0 && slot == 0) w.newPage(orientFor(o, QSize(), true));
        const QRectF page = w.paintRect();
        double top = page.top();
        // Every page has a header line (title, if any, left; date and page
        // right), drawn once per page; every cell on the page sits below it.
        const double hh = QFontMetricsF(head, w.fontDevice()).height();
        if (slot == 0) {
            w.painter.setFont(head);
            w.painter.setPen(Qt::black);
            if (!o.title.isEmpty()) w.painter.drawText(QRectF(page.left(), page.top(), page.width() * 0.7, hh), Qt::AlignLeft | Qt::AlignVCenter, o.title);
            w.painter.setFont(caption);
            w.painter.setPen(QColor(90, 90, 90));
            w.painter.drawText(QRectF(page.left(), page.top(), page.width(), hh), Qt::AlignRight | Qt::AlignVCenter,
                               QStringLiteral("%1 · %2 / %3").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd"))).arg(w.pages).arg((pics.size() + perPage - 1) / perPage));
        }
        top += hh + 6 * pt;
        const double gap = 4 * pt;
        const double cellW = (page.width() - gap * (o.columns - 1)) / o.columns;
        const double cellH = (page.bottom() - top - gap * (o.rows - 1)) / o.rows;
        const int col = slot % o.columns, row = slot / o.columns;
        const QRectF cell(page.left() + col * (cellW + gap), top + row * (cellH + gap), cellW, cellH);
        const double capH = o.captions ? QFontMetricsF(caption, w.fontDevice()).height() + 2 * pt : 0;
        const QRectF imgBox(cell.left(), cell.top(), cell.width(), cell.height() - capH);
        const QImage &img = pics[i];
        if (!img.isNull()) w.painter.drawImage(fitted(img.size(), imgBox), img);
        else { w.painter.setPen(QColor(200, 200, 200)); w.painter.drawRect(imgBox); }
        if (o.captions && i < captions.size()) {
            w.painter.setFont(caption);
            w.painter.setPen(QColor(60, 60, 60));
            const QString text = QFontMetricsF(caption, w.fontDevice()).elidedText(captions[i], Qt::ElideMiddle, cell.width());
            w.painter.drawText(QRectF(cell.left(), imgBox.bottom() + 2 * pt, cell.width(), capH), Qt::AlignHCenter | Qt::AlignTop, text);
        }
    }
    w.end();
    if (pagesOut) *pagesOut = w.pages;
    return QString();
}

// Into the paper profile when one is set; the reason when it cannot be.
static QString toPaper(QList<QImage> &images, const Options &o) {
    if (o.icc.isEmpty()) {
        // No paper profile: the page is sRGB, so a wide-gamut print render
        // is converted into it rather than laid down as if it already were.
        for (QImage &img : images)
            if (img.colorSpace().isValid() && img.colorSpace() != QColorSpace(QColorSpace::SRgb))
                img = img.convertedToColorSpace(QColorSpace::SRgb).convertToFormat(QImage::Format_RGB32);
        return QString();
    }
    for (QImage &img : images) {
        QString err;
        img = Proof::convert(img, o.icc, o.intent, o.blackPoint, &err);
        if (!err.isEmpty()) return err;
    }
    return QString();
}

static QString readAll(const QStringList &files, QList<QImage> &images) {
    for (const QString &f : files) {
        QImageReader r(f);
        r.setAutoTransform(true);
        images << r.read();
        if (images.last().isNull()) return QStringLiteral("cannot read %1").arg(QFileInfo(f).fileName());
    }
    return QString();
}

static void paintPrintPages(Writer &w, const QList<QImage> &images, const QStringList &captions, const Options &o);

QString printSheet(const QStringList &files, const QStringList &captions, const QString &pdfPath, const Options &o, int *pagesOut) {
    if (files.isEmpty()) return QStringLiteral("nothing to print");
    QList<QImage> images;
    if (const QString e = readAll(files, images); !e.isEmpty()) return e;
    if (const QString e = toPaper(images, o); !e.isEmpty()) return e;
    Writer w(pdfPath, o, orientFor(o, images.first().size(), false));
    if (!w.begin()) return QStringLiteral("cannot write %1").arg(pdfPath);
    paintPrintPages(w, images, captions, o);
    w.end();
    if (pagesOut) *pagesOut = w.pages;
    return QString();
}

static void paintPrintPages(Writer &w, const QList<QImage> &images, const QStringList &captions, const Options &o) {
    const double pt = w.resolution() / 72.0;
    QFont caption; caption.setPointSizeF(8);
    for (int i = 0; i < images.size(); ++i) {
        if (i > 0) w.newPage(orientFor(o, images[i].size(), false));
        const QRectF page = w.paintRect();
        const double capH = o.captions && i < captions.size() && !captions[i].isEmpty() ? QFontMetricsF(caption, w.fontDevice()).height() + 4 * pt : 0;
        const QRectF box(page.left(), page.top(), page.width(), page.height() - capH);
        w.painter.drawImage(fitted(images[i].size(), box), images[i]);
        if (capH > 0) {
            w.painter.setFont(caption);
            w.painter.setPen(QColor(60, 60, 60));
            w.painter.drawText(QRectF(page.left(), box.bottom() + 4 * pt, page.width(), capH), Qt::AlignHCenter | Qt::AlignTop, captions[i]);
        }
    }
}

} // namespace Sheets
