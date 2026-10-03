// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "imagematchmath.h"
#include <QImage>
#include <QVariantMap>
#include <QPoint>
#include <atomic>

namespace ImageMatch {
struct Options {
    bool creative=false, exposure=true, whiteBalance=true, tone=true, colour=true, grain=false, protectSkin=true;
    double colourStrength=1, toneStrength=1, grainStrength=1;
    static Options fromMap(const QVariantMap &map);
    QVariantMap toMap() const;
};
struct Source {
    QImage overview;             // straight float linear Rec.709, no display/view baked in
    QList<QImage> grainTiles;    // unscaled native-resolution samples, not resized thumbnails
    QSize fullSize;
};
struct Grain {
    double amount=0, size=1, roughness=.5, colour=0, confidence=0;
    double zones[3]={0,0,0};
    int samples=0;
    bool compression=false, textured=false;
    QVariantMap report() const;
};
struct Result {
    oma_match_params params;
    Grain referenceGrain,targetGrain;
    QStringList warnings;
    double confidence=0;
    QVariantMap fit;             // diagnostics for the automatic estimate, not a probability
    Result() { oma_match_defaults(&params); }
    QVariantMap report() const;
};
Grain grain(const Source &source, const std::atomic<int> *cancel=nullptr, int ticket=0);
Result analyse(const Source &reference,const Source &target,const Options &options,
               const std::atomic<int> *cancel=nullptr,int ticket=0);
QImage render(const QImage &linear,const oma_match_params &params,const QSize &fullSize,
              const QPoint &origin={},double scale=0,const std::atomic<int> *cancel=nullptr,int ticket=0);
QVariantMap parameters(const oma_match_params &params);
bool parameters(const QVariantMap &map,oma_match_params *params);
QVariantList controls();
Source fromImage(const QImage &image);
}
