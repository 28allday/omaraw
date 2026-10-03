# Curves

Open **Adjust ▸ Curves** for two ways to shape tone by hand. They sit at different points in the processing: the **Curve** acts on the scene's light before Tone's Contrast turns it into a picture, and **Tone regions** act on the finished picture after it.

## Curves

A tone curve over the picture's RGB. Click to add a node, drag it, right-click a node to remove it. Its scale is lightness: middle grey (18% of white) sits at the centre, 0.5 in is 0.5 out until you move it, and the right-hand end is diffuse white. Behind the curve sits its input histogram: the light as the curve receives it, before Contrast, so the nodes land on the tones they act on. Anything brighter than white (a bright sky, a lamp) gathers at the right-hand end; Contrast still brings it into the picture afterwards.

- **Shared**, or **R**, **G** and **B** apart. Splitting copies the shared curve into each channel.
- **Interpolation**: Monotone never overshoots; Catmull-Rom and Cubic spline are smoother between nodes.

## Tone regions

Four sliders that nudge the lightness curve in bands: **Highlights**, **Lights**, **Darks** and **Shadows**. Drag a row to lift or lower that band; the bands blend into each other. They work on the finished picture's lightness and leave its hues where they are.

The ellipsis on either heading copies, pastes or saves that curve as a preset.

## Lab colour curves

The curves for Lab's two colour axes are part of **Lab colour**, under Colour: open it and choose **Show curves**. They share the engine's Lab tone curve with **Tone regions**, so that heading's eye and ⋯ menu cover both; while Lab colour is in use, Tone regions act on lightness alone.

## RGB levels

Enable **RGB levels** in **Adjust ▸ Customise tools**, then open its heading. Set black point, midpoint and white point, linked for all RGB channels or separately for red, green and blue. Points stay ordered; moving a point stops before its neighbour. These settings support the same copy, paste, reset and preset actions as other adjustments.
