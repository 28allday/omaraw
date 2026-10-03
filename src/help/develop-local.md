# Masks

Adjust part of the photo in **Develop → Masks**. Each mask has six folding
sections: **Colour range**, **Colour**, **Tone**, **Detail**, **Shapes & overlay**
and **Edge refinement**. One section opens at a time; all existing controls
remain available.

## Select a colour, such as skin

1. Choose **Colour Range**, then click on skin or drag over a small representative
   area. Escape cancels. Sampling creates a neutral mask and shows its coverage.
2. Adjust **Range width** and **Softness** to include neighbouring skin tones.
   **Sample colour…** replaces the colour selection within the current mask.
3. Open **Colour**. Drag the wheel gently towards orange for warmer skin, or
   towards pink for a rosier tone. Distance from the centre sets the strength.
   **Saturation** deepens the existing skin colour; **Lightness** brightens it.
   The selection overlay hides when you start a colour adjustment so you see
   the correction itself. Use **Preview selection** to inspect coverage again.

The wheel uses the same pointer and keyboard gestures as Primary correction: Shift
for fine control, Ctrl to lock direction, and double-click, right-click or Home
to reset the colour push. A drag is one Undo step, even if you pause. Resetting
the wheel keeps saturation, lightness and the mask intact.

**More colour controls** reveals **Hue shift**, **Warmth**, **Tint** and
**Vibrance**. Warmth and Tint are the two axes of the wheel, so both views stay
in sync, including older edits. Hue shift rotates the existing colours; the
wheel adds a colour cast. These local corrections are independent of the photo’s
white balance. The wheel uses a gentle range; the precise sliders retain their
full range for stronger adjustments.

Colour selection includes matching colours anywhere in the picture. To protect
a similar-coloured background, add a radial or pen shape in **Shapes & overlay**
so the range only applies inside it. Sampling an existing mask keeps its shapes
and luminance limits. This selects colours; it does not recognise skin.

For precise limits, expand **Refine individual ranges** within **Colour range**.
The original Luminance, Hue and Colour bands, pipettes, inversion, From, To and
Falloff controls are still there. Use **Tone** for exposure, black, contrast,
highlights and shadows; **Detail** for clarity, sharpness and colour moiré.


## AI object masks

Choose **AI object mask**. The models and tools are included and work offline. Click
an object, right-click to exclude a point, or drag a box around it. Add points
to refine the selection. **AI brush** lets you paint inside the object and
release to have AI find its outline. Right-drag over unwanted areas to exclude
them. Short strokes inside the object work best; the green stroke is a guide
and the blue overlay shows the resulting selection.

**Paint** adds exactly the area you brush, for manual corrections; right-drag
erases. **Brush size** sets the radius. **Undo selection**, **Clear** and
**Invert** work with both tools. Switching tools keeps the draft; a new AI
click, box or brush stroke replaces manual paint with a fresh AI result,
which **Undo selection** can reverse.

For a closer outline, use **Refine edges** before accepting the selection.
It follows nearby colour edges; **Undo selection** restores the previous
outline for comparison. It works with AI selection and Paint, using a more
detailed rendering to improve the outline when zoomed in. Weak or ambiguous
edges may still need manual corrections.

Choose **Create editable mask** when ready. The selection stays visible while
OmaRAW creates the paths. If conversion fails, the message beside the button
explains the problem and keeps your draft so you can refine it and retry.
On success, **Shapes & overlay** opens at the new mask with its editable points
and coverage visible. The selection becomes ordinary editable paths with local
colour and tone controls, Undo, saved history and export support. Feather/refine
the paths as needed; this is a binary object
mask, not a fine hair or transparency matte.

CPU works without a GPU. **Use GPU for selection** optionally uses Vulkan;
speed depends on hardware. Photos stay local; no assistant account is needed.
Cancel/Escape, changing the photo or changing its edit clears the draft.

## Making one

Press **Radial** or **Gradient**, then drag the shape into place on the picture. **Pen** lets you draw your own closed shape. Set its **Exposure** and **Black**, then **Contrast**, **Highlights**, **Shadows**, **Saturation**, **Vibrance**, **Clarity**, **Sharpness** and **Colour moiré radius**. The whole stack acts under the one mask. Give the local a name with the pencil; History follows the name. The row's switch takes the whole local off; the bin removes it.

Drag the centre handle to move a radial, gradient or brush shape. Its outline follows the mouse; the mask and photo update when you release. Each drag is one undo step. Clicking a handle selects it without changing the photo.

## Pen masks

Choose **Pen** to start a new mask, or the pen icon under **Shapes & overlay** to add a shape
to the active mask. Click to place corners; click and drag to create a curved
point with handles. Close the path by clicking its first point, pressing Enter,
double-clicking the last point, or choosing **Close shape**. You need at least
three points. Backspace removes the last draft point; Escape or right-click cancels.
An unfinished path does not change the photo.

Select a point to reveal its curve handles. Drag a point to move it together with
its handles, or drag a handle to reshape the curve. Alt-drag a handle to move it
independently. **Smooth** gives the selected point matching handles; **Corner**
removes them. Double-click an edge to insert a point without changing the curve.
Delete removes a selected point while retaining at least three points. Drag the
centre cross to move the whole shape. Point positions follow crop and rotation.

**Feather** softens the edge. Its slider gives extra room to small changes and
shows the width as a percentage of the photo's short side: 0.10% is 5 pixels
on a 5000-pixel short side. Start low for a close object edge; click the figure
to type an exact amount. **Opacity** sets how much adjustment passes through.
Pen shapes also support Add, Intersect, Subtract, Exclude, inversion, bypass,
duplication and the usual range and edge controls below. Turn on **Show the mask**
to see the actual feathered coverage. Shapes remain editable after saving, undo,
copy/paste and reopening the photo, and apply to exported pictures.

## The mask

Use **Hide mask** near the top of Masks to hide outlines, editable points and
the colour overlay while you adjust the photo. The mask's adjustments stay
active. **Show mask** brings the display back, including your previous colour
overlay setting. Starting a new drawing tool shows the mask again.

Under **Shapes & overlay**, add more radials, gradients or pen shapes to the same local, or press the brush and drag on the picture to lay a stroke. Every shape past the first carries an operator: **Add**, **Intersect**, **Subtract**, **Exclude**. Each shape can be inverted on its own. Click a shape for its sliders: radius and feather, rotation and softness, stroke width, hardness and flow, opacity. The eye bypasses a shape without removing it. A gradient's centre line marks half coverage; its two outer guides mark approximately 10% and 90%.

A click paints a single dab. Short brush movements work at 100% zoom too.
Switching away from Local or changing photos discards an unfinished stroke.
The brush and range picker act only while Local is open.

- **Colour range → Refine individual ranges**: narrow the mask by pixel value, **Luminance**, **Hue** or **Colour**, with From, To and Falloff. The pipette picks the band from a spot on the picture. With a range on, the last shape can go and the mask becomes range only.
- **Edge refinement**: **Blur** softens the finished mask; **Refine edge** lets it hug detail; **Mask contrast** and **Mask brightness** reshape it.
- **Show what the mask covers** (M, or the eye under Shapes & overlay): paints what the mask covers, in your choice of colour and strength. Holding M temporarily shows coverage and restores the previous display on release. The eye beside an adjustment or shape bypasses its effect instead.

## Copy and paste

The copy button under Shapes & overlay (Ctrl+Shift+C) takes the active local, shapes, ranges, switches and sliders; paste (Ctrl+Shift+V) lays it onto whatever photo is open as a new local. Shapes are placed as fractions of the frame, so check their positions on a photo of a different shape.

## External masks

Under **External mask**, select the adjustment to limit and press **Import**. White selects, black protects, and grey gives partial selection. Transparent pixels protect the image. Use a mask aligned to the uncropped, unrotated source; it follows later lens, rotation and crop changes.

A managed greyscale PNG copy is saved with the catalogue, with a maximum edge of 4096 pixels. The same mask can control exposure, tone equalizer, colour grading, contrast & texture, clarity, sharpening, contrast equalizer or RGB primaries. **Use mask** connects another adjustment, **Invert** swaps the selection and **Disconnect** restores that adjustment's global effect. Importing a replacement changes the shared mask for every connected adjustment.

Snapshots and presets retain the connections and managed file path. Preset JSON does not embed the mask file: copy the library's masks folder with backups, and make the referenced file available when sharing a preset.

Catalogue backups include imported masks. Keep the catalogue’s `masks` folder if you move it manually. Mask presets retain the file reference and connections; they do not embed the image.
