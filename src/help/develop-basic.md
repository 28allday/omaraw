# White balance, light and curves

**Light & Tone** contains White balance, Highlight recovery, Light, Tone mapping, Film tone, Tone equalizer, RGB levels and Curves, in that order. Light keeps Exposure, Black level, Contrast, Shadows and Highlights together. Specialist tools are available through **Customise tools**. Texture, clarity and dehaze are in **Detail**.

Exposure and Shadows & highlights keep their own status dot and **⋯** menu on their first slider row. Contrast shares the effect switch and adjustment presets in Colour → Vibrance & saturation. Double-click Contrast to reset just that slider. Resetting Exposure leaves Contrast and Shadows unchanged.

White balance offers Temperature and Tint, **As shot**, **Auto**, and a neutral picker. Click something grey or white in the picture after choosing the picker. **Auto** above the workflow menu estimates exposure from linear light before film looks and tone curves, with a highlight limit. It sets a new exposure rather than adding another correction each time. Import provides the camera/default starting exposure; it does not run this scene estimate. Auto is a starting point: night scenes, bright snow and small subjects can still need manual adjustment.

Unused Shadows, Highlights, Clarity and Dehaze rest at zero. Their slider resets also return to no effect. An effect switched off keeps its saved settings; switching it back on restores them. Sizes, distances, thresholds and camera settings have meaningful defaults of their own: for example, Dehaze Distance starts at 20, while white balance follows the camera. A fresh RAW also includes its base exposure and tone rendering.

## Exposure

- **Exposure** (EV): overall brightness in stops. +1 doubles the light.
- **Black level**: where black sits. Slightly negative lifts the deepest shadows for a matte look; positive crushes them.

## Shadows and highlights

- **Shadows**: lifts (+) or deepens (−) the dark areas without moving the midtones.
- **Highlights**: recovers (−) or brightens (+) the bright areas.

## Contrast

**Contrast**, in Light, strengthens or softens the overall tonal separation. It works with Tone mapping, Film tone or Print stock without replacing the chosen rendering.

## Tone mapping

Available through **Customise tools**. These settings shape the rendering curve; existing edits retain their saved values.

- **Curve steepness**: the slope of the rendering curve around middle grey.
- **Curve balance**: shifts the curve towards the shadows (−) or the highlights (+).

Using these controls selects Tone mapping in place of Film tone or Print stock. For an ordinary contrast adjustment that keeps either look, use **Light → Contrast**.

## Film tone

Available through Customise tools, and automatically visible on photos with edited Film tone settings: an alternative rendering with a filmic shoulder and toe. The picture has one rendering at a time, so moving a Film tone slider replaces Tone mapping or Print stock. Light → Contrast keeps Film tone in use.

- **Whites** (EV): how far above middle grey the picture reaches white.
- **Blacks** (EV): how far below middle grey it reaches black.

## Highlight recovery

What to do with sensor values that clipped. **Rebuild from surroundings (recommended)** rebuilds them from the surrounding colour; **Keep brightness, drop colour** keeps the brightness and lets the colour go; **Clip to white** simply clips; **Rebuild by region** and **Rebuild fine detail** are slower and finer; **Spread nearby colour** is the old method. **Clip threshold** sets the level counted as clipped.

## Dehaze

- **Amount**: cuts atmospheric haze; a little goes a long way.
- **Distance**: how far into the scene the effect reaches.

## Tone equalizer

Enable **Tone equalizer** in **Customise tools**, then open its heading. Its nine zones adjust exposure from −8 to 0 EV in a guided brightness mask. **Mask exposure** shifts the tones assigned to the zones; **Mask contrast** changes their spread. Detail preservation, smoothing and edge refinement keep fine texture while larger areas are brightened or darkened.

## Scopes and RAW clipping

The **Scope** dropdown above the graph offers Histogram, Waveform, RGB parade, Vectorscope and False colour. RGB parade shows separate red, green and blue traces, with black at the bottom and white at the top. They analyse the accepted sRGB preview without requesting another RAW render.

Use **Enlarge scopes** beside the dropdown to open a larger, movable panel over the photo. The adjustment dock remains usable. Drag the Scopes heading to reposition it and the bottom-right corner to resize it down to half size. **Compact** (the inward arrows at smaller sizes) or Escape returns to the small view. The large scope uses a higher-resolution graph and follows edits as you work.

The vectorscope includes coloured **75% targets**, labelled R, Y, G, C, B and M,
and a dashed **Skin** reference line. Traces near the centre have little colour;
traces farther out have more chroma. The skin line is a hue guide for neutral
lighting, not a requirement that every skin tone land exactly on it.

### False colour

**False colour**, in the same menu, from the palette button on the picture or with **F** (tap to switch, hold to peek), repaints the photograph by how bright each part of it is, so exposure can be read off the picture itself rather than guessed from a graph. A colour guide appears on the picture, even with the side panels hidden. Its arrow folds or expands the guide; switching false colour on opens it again. Comparing (Before/After or a snapshot) shows both sides in false colour, so the two can be compared zone for zone.

- **Green** is middle grey, the brightness of a grey card (42–51 %).
- **Pink** is a light-skin brightness reference (58–66 %). Skin tones and lighting vary; it is not a target for every face.
- **Yellow** is highlights (80–92 %) and **orange** near white (92 % and up): bright areas approaching the top of the preview's range.
- **Teal** is shadows (8–20 %) and **blue** deep shadows (under 8 %).
- **Red** is clipped highlights and **purple** clipped shadows, by the same test as the clipping indicators: any channel at the top, every channel at the bottom. This describes the edited preview; RAW detail may still be recoverable by changing the edit.
- Everything between stays a black-and-white copy of the picture, so it remains readable.

The graph becomes the legend: one column per band, as tall as that band's share of the picture. Point at a column for its name, range and share. Brightness is that of the finished sRGB picture (the figure a waveform plots), including the active soft proof, without the gamut warning paint; it follows every edit and stays sharp when you zoom in. Press the palette button again to go back to the graph you had. Exports are never affected.

**Mark Clipped Sensor Areas**, under View ▸ Exposure Overlays, marks saturated sensor samples in magenta before exposure or highlight recovery. It supports 16-bit Bayer and X-Trans sources. The bounded preview samples a neighbourhood of sensor sites; very small clipped spots can fall between samples. Ordinary red/blue clipping indicators still measure the developed image.
