#!/usr/bin/env python3
"""Rebuild the static UI wheel (Pillow required only when regenerating).

Hue is the pinned colorbalancergb engine's Filmlight Yrg angle + 30 degrees.
The swatch is converted through LMS/XYZ D65 to sRGB; it is an orientation
reference, not a simulation of the effect on a photograph. Pre-rendering
keeps all pixel work out of Qt's UI thread while dragging and resizing.
"""
from pathlib import Path
import math
import colorsys
from PIL import Image


def colour(hue):
    a = math.radians(hue - 30)
    r, g = .21902143 + .16 * math.cos(a), .54371398 + .16 * math.sin(a)
    b = 1 - r - g
    l, m, s = .95*r + .38*g, .05*r + .62*g + .03*b, .97*b
    x = 1.80794659*l - 1.29971660*m + .34785879*s
    y = .61783960*l + .39595453*m - .04104687*s
    z = -.12546960*l + .20478038*m + 1.74274183*s
    rgb = [3.2404542*x - 1.5371385*y - .4985314*z,
           -.969266*x + 1.8760108*y + .041556*z,
           .0556434*x - .2040259*y + 1.0572252*z]
    peak = max(rgb)
    def encode(v):
        v = max(0, v / peak)
        return 12.92*v if v <= .0031308 else 1.055*v**(1/2.4)-.055
    return [encode(v) for v in rgb]


def main():
    size = 512
    palette = [colour(h / 4) for h in range(1440)]
    image = Image.new('RGBA', (size, size))
    pixels = image.load()
    radius = size/2 - 1
    for y in range(size):
        for x in range(size):
            dx, dy = x+.5-size/2, size/2-y-.5
            distance = math.hypot(dx, dy)
            if distance > radius+.5:
                continue
            amount = min(1, distance/radius)
            hue = math.degrees(math.atan2(dy, dx)) % 360
            rgb = palette[round(hue*4) % 1440]
            pixels[x,y] = tuple(round(255*(.55*(1-amount)+v*amount)) for v in rgb) + (round(255*min(1,radius+.5-distance)),)
    image.save(Path(__file__).resolve().parents[1] / 'src/qml/OmaRaw/Develop/colour-wheel.png', optimize=True)


def primary():
    """Dark primary wheel, conventional RGB hue, with a narrow colour ring."""
    size = 512
    image = Image.new('RGBA', (size, size))
    pixels = image.load()
    radius = size/2-1
    for y in range(size):
        for x in range(size):
            dx, dy = x+.5-size/2, size/2-y-.5
            distance = math.hypot(dx, dy)
            if distance > radius+.5:
                continue
            amount = min(1, distance/radius)
            hue = math.atan2(dy, dx)/(2*math.pi) % 1
            rgb = colorsys.hsv_to_rgb(hue, 1, 1)
            ring = max(0, min(1, (amount-.955)*radius))
            face = [.16 + amount*.10*(v-.5) for v in rgb]
            pixels[x,y] = tuple(round(255*(face[c]*(1-ring)+rgb[c]*ring)) for c in range(3)) + (round(255*min(1,radius+.5-distance)),)
    image.save(Path(__file__).resolve().parents[1] / 'src/qml/OmaRaw/Develop/primary-wheel.png', optimize=True)


if __name__ == '__main__':
    main()
    primary()
