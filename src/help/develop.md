# Develop

Develop renders your photo through the processing engine. What you see is the same processing an export gets, and the histogram is of that render.

## The layout

- **Left**: Presets, History, Snapshots and Soft proof.

Drag the dotted grip beside a left panel's title to change its position. Drag
the short divider below Presets, History or Snapshots to adjust the list height;
long lists scroll inside their panels. Double-click a divider to restore its
default height. Your order and heights are remembered. Right-click a grip for
Move up, Move down or Reset panel layout; a focused grip also accepts Up/Down.
Help remains available from each panel's **?** button.
- **Centre**: the viewer, with the zoom strip beneath it.
- **Right**: the histogram, **Auto** and **Reset**, then **Crop**, **Masks** and **Retouch**. The numbered menu has six sections, with tools in this order:

1. **Prepare**: Camera profile → Negative conversion † → AI denoise → Noise reduction → Capture sharpening (RAW only) → AI object removal.
2. **Lens & Geometry**: Orientation → Lens corrections → Chromatic aberration → Defringe → Transform → Crop & straighten.
3. **Light & Tone**: White balance → Highlight recovery † → Light → Tone mapping † → Film tone † → Tone equalizer † → RGB levels † → Curves.
4. **Colour & Look**: RGB primaries † → Image Match → Primary correction → Vibrance & saturation → Colour mixer → Selective colour → Lab colour → Black and white → Creative profile → Print stock → LUT.
5. **Detail**: Texture, clarity & dehaze → Contrast & texture † → Contrast equalizer † → Sharpening.
6. **Effects**: Halation → Glow → Vignette → Grain.

† Specialist tools are available through **Customise tools** and appear automatically when their settings affect the photo. Choose any section directly; you do not have to finish one before using another. This is an editing workflow, not a change to rendering order. Existing photos keep their appearance. Capture sharpening has its own RAW-only entry in Prepare; ordinary sharpening remains in Detail.

Masks and manual heal/clone/blur/fill remain available through the shortcuts above the menu. In **Masks**, use **Colour Range** to sample skin or another colour, then open **Colour** to adjust it with the local colour wheel. AI object masks become editable shapes; the Masks help page covers selection, refinement and recovery if creation fails. Retouch's AI removal shortcut opens Prepare. **Back** or Esc returns from Masks or Retouch to your previous section. Crop & straighten opens the same crop, rotation and automatic/guided perspective controls as the Crop shortcut or R.

**Customise tools** chooses which tools appear in the current section. Specialist tools start hidden and appear when their edits affect a photo. Once shown, they stay in your layout after reset, bypass or Undo. Hide them deliberately in this menu when they have no applied edits, or choose **Restore default tools**. A small mark beside a heading tracks edited settings. The status dot at the right is **green when the effect is applied** and an unlit grey outline when it is off. **One tool open at a time** is on by default within each section. Turn it off to compare several expanded tools. The section, open tools, Advanced settings and visibility are remembered.

## Working with sliders

Drag a slider, or click its figure to type a value (drag the figure sideways for fine changes). A control with a real unit shows it: stops (EV), kelvin, degrees, pixels or a percentage. The others read **0 where they start**, from −100 to +100 (Contrast, Saturation, Clarity), or from 0 for none to 100 for the most (an amount such as Sharpening). Presets, History and the command line keep the engine's own values, so nothing saved changes meaning. Those figures move in whole steps on a drag and in tenths with **Shift**, with **Alt** and the arrow keys, or typed. Once a slider has been clicked, or reached with Tab, the arrow keys step it: **Shift** for ten times the step, **Alt** for a tenth. **Ctrl** with the mouse wheel over any slider steps it the same way. **Double-click a slider or its name to put it back**; Alt-click an adjustment's heading to put the whole adjustment back; a changed control also shows a small ↺ beside its figure when the pointer is on it.

Tools with only one or two finer controls show them directly. **Advanced** is reserved for larger sets, such as Print stock's whites and channel contrast and Vibrance & saturation's extra colour controls. Tools and Advanced controls slide open and closed. Popup menus and dropdown choices also open with a short downward motion; Reduced motion makes them instant. Folding a tool, switching sections or hiding controls never changes the photo.

An adjustment that is not applied shows an unlit dot instead of a figure. Moving one of its sliders brings it in. Click its **status dot** to switch the effect off or on while keeping its settings. Every tool heading has a visible **↺ Reset** button, including Black and white. It restores that tool’s starting settings and applied state, keeping edits in other tools. Black and white returns the picture to colour. **Ctrl+Z** brings the reset effect back in one step. The **⋯** menu also offers reset, copy, paste and save as preset. Combined headings such as Light report whether any contained adjustment is applied; use each adjustment’s dot to switch it. The functional green indicator keeps its meaning in Colour Critical mode.

**Auto** above the workflow menu calculates exposure from the picture while protecting highlights. The image stays steady during the calculation and updates once with the finished result. Pressing Auto again without editing the photo leaves it unchanged. The exposure supplied on import is a fixed starting correction, not this measured Auto result, so the first Auto calculation can choose a different brightness.

The tool heading’s **↺** resets that complete tool. Light keeps colour changes in Vibrance & saturation, Curves keeps Lab colour, and Perspective keeps your rotation. Individual adjustments within grouped tools can also be reset from their ⋯ menus. **Reset** returns the whole photo to how it looked after import: its starting exposure, colour and tone, with the automatic sharpening, colour noise reduction and lens correction. **Reset to Camera Original** (Adjustments menu, or **Back to original** in History) goes all the way back to the raw file with nothing on; Auto on a photo there brings the starting tone back with it. Both full-photo resets discard previous development edits, local masks and redo steps. They cannot be undone. Only the starting processing steps remain; the import starting settings are retained separately so Reset can restore them after Camera Original. Individual tool resets remain undoable.

Resetting Local masks clears their adjustments and switches them off while keeping their shapes. Retouch resets all its spots; Undo restores them. AI denoise Reset clears the preview and restores its controls. A saved denoised DNG is a separate photo: choose **Photo versions → Original** to return to its source.

## The first edit

A newly imported RAW gets a starting edit the first time it opens here: **capture sharpening**, **colour noise** taken out (the grain stays), and the **lens profile** when the lens is recognised. It keeps the camera/default exposure and colour rendering rather than running measured Auto exposure. It happens once. **Reset** keeps it, as part of how the photo looked after import; **Reset to Camera Original** removes it, and it is not given again. Photos a preset rule has already given a starting point keep that instead. Turn it off in Preferences ▸ Processing ▸ Automatic first edit.

The graph at the top is a histogram by default. Use the **Scope** dropdown above it to switch to **Waveform**, **RGB parade**, **Vectorscope** or **False colour** (the picture repainted by brightness). RGB parade shows the red, green and blue channels side by side, with black at the bottom and white at the top. It follows your adjustments without changing the photo. A line under the graph reports clipped highlights or shadows.

Click **Enlarge scopes** beside the dropdown for a larger panel over the photo. Drag its **Scopes** heading to move it, or its bottom-right corner to resize it down to half size. You can keep adjusting the photo with it open. Both scope selectors stay in sync. Click **Compact** (the inward arrows at smaller sizes), click the expand button again, or press Escape to return to the small view. Your panel size is kept when you reopen it during the session. The panel stays inside the viewer when you resize the window, and closes when you leave Develop or enter Lights out.

The vectorscope shows labelled colour targets at 75% and a dashed skin-tone
reference in both sizes. Use the skin line as a hue guide; skin and lighting vary.

## The viewer

- **Zoom**: roll the mouse wheel over the picture to zoom in or out around the pointer; drag to pan. Fit, Fill, 50%, 100% and 200% are in the strip, Ctrl + wheel steps between them, and double-click toggles Fit and 100% when no on-picture tool is active. At 100% and above the picture is rendered in tiles from the full-resolution pipe.
- **Live adjustments**: sliders and curves show an updating preview while you drag, then restore sharp detail on release. With Faster fitted previews enabled, the moving preview uses a smaller image to stay responsive on large RAW files. Reset buttons remain available while the preview renders. The filmstrip keeps its current thumbnail visible until the updated preview is ready.
- **Before and after** (\ or the compare button): the untouched photo against the current render, split or side by side.
- **Clipping** (J): blown highlights and crushed shadows painted over the picture, with the percentages in the pill.
- **Crop and straighten** (R): see the Lens page.
- **Lights out** (Shift+L) clears the panels; View ▸ Viewer Background changes the surround.

## History, undo and snapshots

History lists every step, oldest first. Click a step to go back to it; the next change continues from there. Original is the untouched render. Ctrl+Z and Ctrl+Shift+Z walk the same list. Back to original resets the photo and discards its edit and redo history.

Snapshots keep a named copy of every setting plus a preview of the render at that moment (Ctrl+N for a quick one, + to name it). Click one to restore it; the eye compares it against the live render, split or side by side.

Zoom to 100% to compare native detail from the saved settings, including its crop, masks and retouching. Pan to inspect another area. Both sides use the same pixel scale, even when their crops differ. Comparing leaves the current settings and Undo/Redo history intact. Detail uses the original, a verified offline working copy, or the available Smart Preview. Smart Preview detail is limited to its reduced resolution. If it cannot render, the saved overview stays visible with an explanation.

## Presets

Presets start in collapsed category folders. Open one to browse its looks;
Favourites and Recent offer shortcuts to the ones you use most. Search also
finds presets inside closed folders. The list has its own scrollbar and a
limited height, so History stays below it. On smaller windows, the left dock
also scrolls to reach Snapshots and Soft Proof.

The category dropdown offers 24 curated recipes across **Film**, **Portrait**, **Landscape**, **Cinematic** and **Black & white**. Hover over a recipe for its description. These combine print stocks, colour, contrast and finishing effects while preserving exposure, white balance, detail, crop and local corrections. Switching presets replaces the previous preset, including its film look, white balance, grain or curves when those were part of it. Adjustments made before the first preset, and later edits to other tools, are retained. Later tweaks to a tool controlled by the previous preset are replaced too. Each switch is one Undo step, and selecting an unchanged preset again does nothing. Adjust the controls afterwards to make the look your own.

The original eight built-in looks remain available. Save your own from the current sliders with +. Choose which adjustment groups a preset includes; a colour-only look leaves tone and detail alone. Search by name, description, category or tag; a star lifts a favourite to the top. Right-click a preset for its category, tags and an auto-apply rule for imports. Adjustments ▸ Presets exports and imports them as JSON. Import also accepts XMP .xmp and .lrtemplate Develop presets, with several files selected at once. Extract ZIP packs first. The report lists converted settings and anything untranslated; Last Import Report reopens it. XMP looks are approximate. A film-look preset that names a camera profile (a .dcp file from the same pack) uses the one for your photo's camera once Preferences ▸ Camera profiles folder… points at the pack's profiles: it then renders under Colour & Look ▸ Print stock in place of the tone mapper, like a print stock. Proprietary profiles, masks and many advanced adjustments are not converted, so check the result on a photo.

## Settings across photos

Adjustments ▸ Copy Settings… (Ctrl+Alt+C) asks which groups travel and puts them on a clipboard; Paste Settings (Ctrl+Alt+V) lays them on the open photo or every selected photo. Sync Settings… (Ctrl+Alt+S) sends the chosen groups from the current photo to the rest of the selection, developed in the background. The same commands are available in Library's photo right-click menu: for Sync, select the batch and right-click the source photo. The dialog names the source and destinations before applying anything.

For example, to reuse only an edited photo's colour adjustments, right-click it,
choose **Copy Settings…**, select the colour groups you need and click **Copy**.
Select the destination photos and use **Paste Settings**. Groups you leave out
keep their destination settings. If you include masks or crop, check their
placement on each destination photo.

## Soft proof

Pick a printer's or lab's ICC profile and the viewer shows the render as that profile would print it, with an optional gamut warning. Exports and thumbnails are never proofed.

## Editing Smart Previews

When the original and full offline copy are disconnected, Develop uses a saved Smart Preview and shows a notice above the photo. Edits stay with the same catalog photo and variants. Reconnecting the drive restores the original automatically. Check sharpening, noise and fine retouching on the original before exporting. Sensor clipping and full-quality output require a full source.
