# Colour grading

Open **Colour & Look** for Primary correction, Print stock and LUTs. Creative profile is in the same section; grain, halation, glow and vignette are in **Effects**. Click a tool heading to expand it.

Use the colour wheels to shape the look after correcting the photo. They start at neutral and leave the picture alone until moved.

Primary correction affects the whole photo. For a colour push restricted to
skin or another area, create a **Masks → Colour Range** selection and use the
wheel under that mask's **Colour** section. The Masks help page explains how to
refine the selection and keep similar-coloured backgrounds out of it.

## Creative profile

Choose **Warm Portrait**, **Clear Landscape**, **Soft Colour**, **Cinema Dusk**, **Silver Monochrome** or **Warm Monochrome** for an independent colour look. **Amount** runs from 0 (no effect) through 100 (the designed look) to 200 (stronger). The switch bypasses it; **None** removes the selection. Exposure, white balance and film settings stay in place. Profiles work after film rendering and can be combined with presets.

**Import profile…** adds a validated 3D `.cube` table to the profile menu and keeps a copy in OmaRAW's profile library. Set **LUT colour space** to the space the table expects; sRGB is the default. Supported tables have 2–64 entries per axis and no 1D shaper. Camera-log transforms need conversion to a supported space first.

Use the tool menu to save a profile adjustment preset, or include **Creative profile** when saving a broader preset. An imported LUT is referenced by the saved edit, so transfer the LUT too when moving an edit to another machine. If it is missing, import and select it again before exporting. The same button imports self-contained enhanced `.xmp` profiles with RGB or HSV tables. Their recorded colour space and Amount limits are automatic; Amount 0 can be a nonzero minimum, and a fixed-amount profile hides that slider. Use the switch for complete bypass. OmaRAW uses its own base rendering. Profiles needing unsupported source adjustments or missing external tables are rejected with an explanation.

## Colour wheels: Lift, Gamma, Gain and Offset

The four wheels correct colour and brightness with overlapping effects:

- **Lift** sets the black level. It has most influence in darker tones and keeps white fixed when the other controls are neutral.
- **Gamma** shapes the midtones while keeping black and white fixed.
- **Gain** scales brightness towards the white end while keeping black fixed.
- **Offset** shifts the whole picture, including both black and white.

Drag the puck towards a colour to add that colour; distance from the centre sets **Chroma** (strength). The centre is neutral. **Hue** and **Chroma** can also be typed precisely. The **Level** control beneath each wheel adjusts brightness. Neutral Level is **0.000** for Lift/Offset and **1.000** for Gamma/Gain.

**Lum Mix**, beneath the wheels, controls how colour pushes affect brightness. At **100**, where it starts, a colour push also changes brightness: a strong push towards red lightens the picture as well as warming it. Lower it and the colour pushes keep the brightness the Level controls give; at **0** they change colour only. The Level controls work the same at any setting.

For a starting workflow, adjust Offset for the overall balance, Lift for the blacks, Gain for the brighter tones, then Gamma for the midtones. Make small moves and check the picture and scopes. White balance and RAW exposure are in **Light & Tone**.

### Layout and gestures

A wide sidebar shows four dark wheels with colour rings in a 2×2 grid. Choose **Single wheel**, or click a range name, to enlarge a wheel. A narrow sidebar automatically uses the enlarged view with range tabs.

- **Shift-drag** makes finer moves. **Ctrl-drag** locks hue while changing strength; combine them for fine strength changes.
- **Scroll** over a wheel to nudge strength, or **Shift-scroll** to nudge hue. Scroll over a slider or number to nudge that value; Shift makes it finer.
- A focused wheel accepts arrow keys. Hold Ctrl to adjust strength with hue locked.
- **Double-click**, right-click or Home on a wheel clears its colour while keeping Level. Double-click a slider resets only that value.
- A wheel's reset button clears its colour and restores its neutral Level. The panel reset restores all four wheels.
- **After / Before** compares with primary correction bypassed and keeps all settings. Earlier adjustments remain applied.

The image updates while dragging; at high zoom it briefly uses a softer preview, then restores sharp detail when you release. Each completed drag or numeric scrub is one undo step. The panel menu copies, pastes and saves all four wheels as a preset. Opening the panel and switching views preserve stronger settings.

Built-in looks and imported split-toning tints use these same primary wheels, so their colour adjustments remain visible and editable. Imported split toning is an approximation because Lift and Gain overlap differently from tonal ranges.

Primary correction works after Tone or Film tone. With a print stock on, it works before the print instead. Strong settings can push colours beyond the output range; use the preview and scopes to judge the result.
