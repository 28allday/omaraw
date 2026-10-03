# Colour

## White balance

White balance is the first tool in **Light & Tone**. It offers Temperature, Tint, As shot, Auto and the neutral picker.

## RGB primaries

RGB primaries changes the working colour space's red, green and blue primaries.
Each Hue slider covers −20° to +20° for practical colour correction; Purity
controls how far the primary lies from neutral. Tint hue and Tint amount can
deliberately colour the neutral axis. These are global colour changes, so use
Colour mixer or Selective colour when you want to adjust one hue range.

## Vibrance and saturation

- **Vibrance** strengthens the muted colours first and is kinder to skin.
- **Saturation** strengthens every colour equally.

**Advanced** adds Colourfulness, Colour brightness and Hue shift. Overall Contrast has its single home in **Light**. These settings share this tool's effect switch, reset and adjustment presets.

Open **Colour mixer**, **Selective colour**, **Black and white** or **Lab colour** by its heading. Lab colour is visible in the Colour & Look section by default. Enable **RGB primaries** through **Customise tools**. Camera profiles and optional Negative conversion are in **Prepare**. Colour wheels and LUTs are in **Colour & Look**.

## Adjust skin or another selected colour

Open **Masks → Colour Range**, then click or drag across the colour to select it.
Use **Range width** and **Softness** to refine the selection. Under **Colour**, push
the wheel towards a colour to add it, use **Saturation** to strengthen the existing
colour, or **Lightness** to brighten it. Similar colours elsewhere are selected
too; add a shape to limit the correction to the person or object. The Masks help
page covers the wheel, precise sliders and range controls.

## Image Match

Open **Colour & Look → Image Match**. Drop a JPEG, PNG, TIFF or WebP onto the
reference thumbnail, choose a file, or press **Use current edit** to capture the
rendered appearance of the photo open in Develop, including RAW edits. The
reference stays fixed while you edit or change photos. **Replace** and **Clear
reference** change the reference without removing an applied match.

In **Choose** or **Replace**, click a photograph to see its preview beside the
file list. Arrow keys browse the images too. Press **Open** to use the selected
reference, or **Cancel** to keep the current one.

Choose **Consistency** for photographs from a shoot, or **Creative Look** for
the reference's palette and contrast. Select **Exposure**, **White balance**,
**Tone / contrast**, **Colour** and optional **Grain** separately. Colour, tone
and grain have independent strengths. Changing these strengths does not change
the exposure or white-balance correction.

**Protect skin-like colours** limits colour shifts in skin and similarly
coloured subjects, including some wood and fur. Turn it off when those colours
should follow a stronger treatment. It affects the draft before Apply and
the saved match afterward, with ordinary Undo.

Press **Preview match** to compare the reference and target. **Before / After**
changes the target view without writing an edit. **100%**, or a click on the
fitted target, shows native pixels for checking grain and detail; drag the
target to inspect another area. **Fit** returns to the whole image. Both sides
use OmaRAW's viewing transform and monitor colour profile.

**Apply to photo** saves one editable Image Match adjustment. Your other tools
keep their settings. After Apply, the five component switches, **Colour strength**,
**Tone strength**, **Grain strength** and **Grain adjustment** controls update the
photo directly, without another Preview or Apply. Switching a component off
keeps its settings and strength; switching it on restores them. Switching all
five off shows the photo as it was before the match. Each switch or slider drag
can be undone. They read the
saved settings when you return to a matched photo, even without a reference.
**Preview match** starts a new draft; its switches and strengths affect the comparison until
you apply it. Open **Edit applied match** to change its exposure, warmth,
tint, tone, colour and grain controls. The heading's effect switch hides or
shows the entire match; its reset removes only Image Match. Ordinary **Undo**
and **Redo** restore the complete previous match. Repeating a match replaces
this adjustment rather than stacking another correction over it.

For a batch, select photographs in the Library or filmstrip, open a target in
Develop and press **Match selection**. Each target is analysed separately using
the same reference and choices. A reference captured with **Use current edit**
is skipped if it is in the selection. **Stop matching** leaves completed photos
editable. **Undo last batch** reverses the completed matches, skipping a photo
if you have edited it since. Individual photo Undo remains available afterward.

**Grain adjustment** edits the proposed amount, size, roughness, coloured
character and shadow/midtone/highlight distribution before applying. Grain size
is relative to a 3000-pixel long edge so differently sized photos have a similar
apparent scale at equal output size. Check at 100% as well as Fit. After applying,
the same settings remain available in **Edit applied match**.

Analysis uses quiet areas at native resolution and adds only the estimated
grain missing from the target, including noise revealed by the proposed tone
correction. A clean reference adds no grain. If the target is already grainier,
the panel explains that a separate denoise step is needed. Image Match never
automatically denoises or smooths detail. Uncertain texture or compression
samples are rejected and reported; you can still set grain manually.

The first version runs locally using statistical colour, tone and grain
analysis. It works best with related subject matter and lighting. Consistency
keeps corrections conservative for different compositions. Creative Look
transfers contrast even when exposure cannot be estimated reliably; use
**Tone strength** to control the effect. Bright highlights limit nearby tone
adjustments without suppressing the entire shadow lift. Mostly grey references keep
small coloured subjects coloured. Extreme looks can need manual refinement;
check the warning and comparison before applying. Re-preview existing matches
to use the revised estimate; saved settings keep their original appearance.
Grain and
skin protection are estimates, so review difficult textures, mixed lighting,
strong colour treatments and compressed references before applying a batch.

## HSL by colour

Eight bands, Red through Magenta, each with **Hue**, **Saturation** and **Luminance**. Darken a blue sky with its Luminance, shift the greens with Hue, or pull the saturation of one colour without touching the rest.

## Lab colour

Richer, better separated colour without the picture getting lighter, darker or harsher. Lab describes a colour as a lightness and two colour axes, green to magenta and blue to yellow; these sliders work on the two colour axes only, so brightness never moves. Every slider rests at 0 and runs from −100 to 100; double-click one to put it back.

- **Colour separation** pulls the picture's colours apart from each other and from grey. Muted colours gain the most, strong ones are held back so nothing clips, and greys stay grey. To the left it draws them together.
- **By colour family**: **Greens**, **Reds & magentas**, **Blues** and **Yellows & warm tones** make one family richer or quieter and leave the others alone. Lift the greens of foliage without touching skin, or quieten a loud blue sky.
- **Remove a cast**: **Green–magenta** and **Blue–yellow** slide every colour along one axis. Move the slider away from the cast you see; the second one also serves as a last touch of cooler or warmer.

**Reset Lab colour** puts every slider back to zero.

### Show curves

The sliders only place points on two curves, one for each colour axis. **Show curves** opens them for shaping by hand, the way Lab curves work in other editors.

- **a · green–magenta** and **b · blue–yellow**: the strips along the bottom and the left show which end is which. The readout is in Lab's own units, −128 to +127.
- **The centre of the square is neutral grey.** Keep the line through it and greys stay grey.
- **Steepen** the line through the centre to pull colours apart on that axis; do it on one side only to enrich, say, the warm colours and leave the cool ones. **Move the centre point** to shift the whole picture along the axis.
- Most real colours sit close to the centre, so small moves go a long way.

A curve shaped by hand is no longer something the sliders can describe, so they read zero and a note says so; moving a slider then replaces the hand-made shape. ↺ beside the axis buttons straightens the curve shown.

Lab colour shares the engine's Lab tone curve with **Tone regions** (under Curves), so presets, snapshots and copy/paste carry them together. While Lab colour is in use, Tone regions act on lightness alone (their colour looks a touch less saturated than otherwise); put Lab colour back to zero and they return to their usual behaviour.

## Selective colour

Eight hue bands with hue, saturation and brightness, plus **Smoothing and neutral protection** under the disclosure: smoothing stops adjacent bands from tearing, and neutral protection keeps greys grey.

## Advanced colour controls

Open **Advanced** in Vibrance & saturation.

- **Colourfulness**: perceptual colour intensity independent of brightness.
- **Colour brightness**: brightness of the coloured areas.
- **Hue shift**: rotates every hue.

## Camera profile

Shows the photo's camera. OmaRAW calibrates its colour automatically, with no download or setup.

**Apply camera look** appears when an included preset matches your camera. It adjusts contrast and saturation for a look inspired by the camera's JPEGs. Exposure, white balance, crop, local edits, local contrast and detail stay. It replaces Tone and Colour balance settings and switches off an active Print stock or DCP. Undo restores your previous look.

The **Colour profile** menu offers **Automatic colour** and, for supported cameras, **Community colour · included**. Community colour is an alternative supplied by RawTherapee contributors; it works offline. Only matching camera profiles appear. A DCP supplies its own colour and contrast, replaces **Print stock** and parks the regular tone controls. Your other adjustments stay. Choose **Automatic colour** to restore those controls; this does not reset them or undo a camera-look preset. **Colour & Look → Creative profile** can add a separate look on top.

**Additional profiles → Import profile…** adds a DCP you own and retains a copy. **Profiles folder…** scans an existing collection and its subfolders; **Refresh** picks up changes. Search appears for larger collections. Unavailable profiles explain why they cannot be used. The technical input-colour controls remain under **Advanced input colour**.

The 54 included community profiles and camera-look presets are freely redistributable; their licences and credits are supplied with the app. These are community interpretations, not exact manufacturer picture styles. DCP rendering is approximate and currently uses daylight calibration rather than blending lighting calibrations. JPEGs already contain their camera rendering, so these choices are for RAWs.

**Advanced input colour** contains the original calibration controls:

- **Input profile**: how the camera's colours are interpreted. The standard matrix is right for nearly every file.
- **Rendering intent** and **Gamut clipping**: how out-of-range colours are handled on the way in.
- **Working profile**: the colour space the edit runs in; linear Rec.2020 is the default.

## Black and white

Switch the tool on to convert the photo to monochrome. The eight colour sliders
control how bright the original **Reds, Oranges, Yellows, Greens, Aquas, Blues,
Purples and Magentas** become. Move left to darken or right to brighten; zero
keeps the neutral mix. For example, lower Blues for darker skies, raise Oranges
for lighter skin, or adjust Greens for foliage. Moving a slider enables the tool.
Neighbouring colours blend smoothly and neutral greys are protected.

Open **Filter controls** for **Filter size** (a broader response gives gentler
filtering) and **Highlights** (preserves brightness in lighter areas). Each
slider can be reset individually; the tool menu resets the complete conversion.
These settings are included in adjustment presets, copy/paste, undo and exports.

## LUT

The LUT runs after Tone or Film tone, and before a print stock. With normal tone mapping, choose a LUT intended for a rendered picture and set the colour space its maker specifies. Choosing a log colour space converts the LUT's input into that encoding; it does not undo tone mapping. With a print stock on, the regular tone mapper is parked, so the LUT feeds the print and the same LUT can look different.

- **File**: a .cube, .png, .3dl or .gmz look-up table.
- **Colour space**: the space the LUT expects its input in; the LUT's maker says which.
- **Interpolation**: tetrahedral is the usual choice.

## RGB primaries

Choose **RGB primaries** through **Customise tools**. Hue rotates each primary; purity controls how saturated it is. Tint adds a hue to the neutral axis. These controls affect the overall colour rendering.

## Negative conversion

Choose **Negative conversion** through **Customise tools** for photographed or scanned film. **Prepare negative** enables conversion and disables Tone, Film tone and the base curve. Select colour or monochrome stock, set the film-base RGB values from an unexposed border, then adjust density range and scan exposure bias. Shadow/highlight RGB corrections handle colour casts; paper black, grade, gloss and print exposure shape the positive image. Film-base values are set manually.
