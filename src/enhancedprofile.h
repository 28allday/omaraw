// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QByteArray>
#include <QString>
namespace EnhancedProfile {
struct Result { QByteArray profile; QString name, error; };
Result read(const QByteArray &xmp);
}
