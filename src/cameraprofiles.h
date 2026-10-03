#pragma once
#include <QByteArray>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

// Included community and user-supplied DCP files, selected in Colour or resolved
// by name for imported XMP presets. Camera compatibility is checked
// before baking the profile into print tables. It renders through the print
// stock module in place of the tone mapper (docs/CAMERA-PROFILES.md).
//
// Included profiles have a pinned licence/hash manifest in profiles/camera.
// Imported profiles retain a managed source copy.
namespace CameraProfiles {

struct Dcp {
    QString camera, name;
    // Hue/saturation map (the camera's colour correction) and look table
    // (the profile's look): hue, saturation, value divisions and, per
    // entry, hue shift (degrees), saturation scale, value scale.
    int mapDims[3] = {0, 0, 0}, lookDims[3] = {0, 0, 0};
    QVector<float> map, look;
    bool mapSrgb = false, lookSrgb = false;   // value axis sRGB-encoded
    QVector<float> tone;                      // x, y pairs, 0..1
    double exposureOffset = 0;                // stops
    // The profile's tables correct the colour its own forward matrix gives,
    // not the colour matrix the engine renders with: this takes the
    // engine's colour (linear ProPhoto) to that colour first. Identity for
    // a profile with no forward matrix.
    double toForward[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    // The preset's own curves, which the source editor applies after the profile:
    // its parametric curve (as Tone regions) and its red, green and blue
    // point curves, x, y pairs on sRGB-encoded ProPhoto. Empty: none.
    QVector<float> finishRegions, finishCurves[3];
};

// A DCP file's contents. `error` says why a file is refused.
bool parse(const QByteArray &bytes, Dcp &out, QString &error);
// One pixel through the profile: linear ProPhoto (D50) scene light in,
// linear ProPhoto display light out. The reference the baked tables are
// checked against.
void render(const Dcp &dcp, const double in[3], double out[3]);
// The same from and to the engine's working space, linear Rec. 2020 (D50):
// what the baked tables hold.
void renderWorking(const Dcp &dcp, const double in[3], double out[3]);
// Takes a preset's curve rows (a "tonecurveset", a "curveset") as the
// profile's finish; true when there were any.
bool setFinish(Dcp &dcp, const QVariantList &rows);
// The profile as print tables (the print module's OMPT format): linear
// working Rec. 2020 in, display light out. `size` is the cube's edge.
QByteArray bake(const Dcp &dcp, int size = 81);

// The folder the person keeps camera profiles in (searched with its
// subfolders); empty when none is set.
QString folder();
void setFolder(const QString &path);
// Profiles in the managed library and chosen folder (searched now and
// remembered for the next lookup), and the number of cameras they cover.
int profileCount(int *cameras = nullptr);
// Compatible profiles, validated on the worker and cached until a refresh.
// Rows retain unsupported matches with an error so the UI can explain omissions.
QVariantList available(const QString &camera, const QString &exifCamera);
void refresh();
bool matchesCamera(const QString &profileCamera, const QString &camera, const QString &exifCamera);
// Copy an independently validated DCP into the managed library. Does not edit a photo.
QString importFile(const QString &source, QString *error = nullptr);
QString tablesFromFile(const QString &path, const QString &camera, const QString &exifCamera,
                       const QVariantList &finish = {}, QString *error = nullptr);
QVariantMap tableDetails(const QString &tables);
// Built-in source-editor profiles that OmaRAW's
// camera colour stands in for.
bool isBuiltIn(const QString &profile);
// The baked tables for `profile` on a photo from `camera` (the engine's
// normalised name, then the EXIF one), baking them once; empty with
// `error` set when there is no such profile for that camera.
// `finish`: the preset's curve rows, baked in after the profile.
QString tablesFor(const QString &camera, const QString &exifCamera, const QString &profile, const QVariantList &finish = {}, QString *error = nullptr);
}
