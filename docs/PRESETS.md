# Curated presets

Develop's **Presets** panel includes 24 curated recipes in five collections,
alongside the original eight built-in adjustments and your saved presets.
Collections open as collapsed folders, with counts beside their names. Open a
folder, choose a category from the dropdown, or search for a look or subject,
then click a preset to apply it. Favourites and Recent have their own folders.
The preset browser scrolls within a limited height, keeping History below it
within reach; the dock itself scrolls when a small window needs more room.
Drag the dotted panel grip to reorder the dock, or the divider below the list
to resize it. Order and sizes are remembered; right-click a grip to reset them.
Hover for a description; star your favourites.
Each application is one undo step. All controls remain editable afterwards.

| Collection | Recipes |
| --- | --- |
| Film | Portrait 160 · Gentle; Portrait 400 · Everyday; Portrait 800 · Evening; Fine Grain 100 · Colour; Warm 200 · Sunshine; Vivid 400 · Weekend; Portrait 400 · Soft Fade; Warm 200 · Nostalgia |
| Portrait | Clean; Soft Light; Warm Editorial; Quiet Contrast |
| Landscape | Natural; Vivid; Muted Earth; Coastal |
| Cinematic | Classic 2383; Deep 2393; Cool Print; Soft Evening |
| Black & white | Fine Art; Soft Silver; Street; Silver Print |

The Film recipes combine OmaRAW's existing print stocks with restrained grain
and finishing effects. Cinematic recipes use the current cinema release
prints; Silver Print uses Cinema Mono 2302. These are creative recipes around the
existing simulations, not additional measured film stocks. No downloaded LUT,
camera profile, AI runtime or particular GPU is required.

## What changes

The new recipes set a shared selection of creative controls: print stock and
its refinements, tone-mapper contrast, global saturation/chroma/vibrance,
shadow/highlight tints, grain, halation, glow and vignette. Unused effects are
switched off. Moving between curated recipes replaces these settings, so a
monochrome look, tint or coarse grain does not carry over from the last recipe.
Partial-strength prints use a consistent sigmoid baseline.

They preserve exposure and white balance, curves, sharpening/noise reduction,
lens corrections, crop, local masks and retouch. Other colour edits, including
Colour mixer and Lab colour, remain part of the photograph's edit.
Correct exposure and white balance for the photograph, then judge the look;
skin, lighting and the source camera all affect the result. The original eight
presets retain their existing behaviour, including Warm/Cool's white balance.

## Make a variation

Apply a recipe, adjust its controls, and click **Presets → +** to save your own
version with a name, category and tags. Select the adjustment groups it should
include. **Film** alone captures the print and finishing effects; include
**Colour** and **Basic tone** when your variation also needs those adjustments.
Basic tone includes exposure, so a saved group preset is broader than the
curated recipes. **Print stock → ⋯ → Save adjustment preset** saves only that
tool. Built-in recipes remain available unchanged.

Saved presets can be shared through **Adjustments → Presets → Export/Import**.
XMP `.xmp` and `.lrtemplate` import remains approximate; see
[XMP conversion](XMP-IMPORT.md).

For an independent colour look with an Amount control, use
**Colour & Look → Creative profile**. Its six original looks and imported CUBE LUTs leave
the other edit controls in place. Include the **Creative profile** group when
saving the selection and amount in a preset. See [Creative profiles](CREATIVE-PROFILES.md)
for supported LUTs and transferring imported assets.
