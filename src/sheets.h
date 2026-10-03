// Print and contact sheets as PDF files, drawn with Qt's own PDF writer:
// a contact sheet is a captioned grid of many photos per page; a print
// layout is one photo per page, fitted inside the margins. The PDF goes
// to the system's print dialog or a lab. Pure functions.
#pragma once

#include <QImage>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace Sheets {

struct Options {
    // Paper: "A4", "A3", "Letter", "Legal", "A5", "4x6", "5x7", "8x10".
    QString paper = QStringLiteral("A4");
    // Orientation: "auto" (per photo for print, landscape for sheets), "portrait", "landscape".
    QString orientation = QStringLiteral("auto");
    double marginMm = 10;
    int columns = 4, rows = 3;   // contact sheet grid
    bool captions = true;        // file names under the photos
    QString title;               // header line on contact sheets ("" = none)
    int dpi = 300;
    // Application-managed colour: convert every picture into this RGB
    // paper profile ("" = keep PDF pages in sRGB), with the
    // intent (0 perceptual … 3 absolute) and black point compensation.
    QString icc;
    int intent = 1;
    bool blackPoint = true;

    static Options fromMap(const QVariantMap &m);
    QVariantMap toMap() const;
    static QStringList papers();
};

// Writes a contact sheet of `images` (already-decoded previews) with
// `captions` (same length, may be empty strings) to `pdfPath`. Returns ""
// on success or a reason; *pages receives the page count.
QString contactSheet(const QList<QImage> &images, const QStringList &captions, const QString &pdfPath, const Options &o, int *pages = nullptr);

// One photo per page from files on disk (full renders), fitted inside the
// margins; orientation follows each photo when "auto".
QString printSheet(const QStringList &files, const QStringList &captions, const QString &pdfPath, const Options &o, int *pages = nullptr);

// Pixels on the long edge a render needs to fill this paper at `dpi`.
int longEdgePixels(const Options &o);

} // namespace Sheets
