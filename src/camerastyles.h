#pragma once
#include <QString>
#include <QVariantMap>

// Adapt the tone and saturation portion of the private engine's GPL camera
// styles. Exposure, local contrast, masks, lens and detail settings are kept.
namespace CameraStyles {
QVariantMap forCamera(int image, const QString &prefix, const QString &camera, const QString &exifCamera);
}
