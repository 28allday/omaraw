# Image Match

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

See the [user guide](USER-GUIDE.md) for the complete workflow.
