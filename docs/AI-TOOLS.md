# Local AI tools

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

See the [user guide](USER-GUIDE.md) for the complete workflow.

AI object removal is described in [Retouch](../src/help/develop-retouch.md); RAW denoising has its own [guide](AI-DENOISE.md). Model sources, fixed checksums and licences are recorded in [src/ai/LICENSES.txt](../src/ai/LICENSES.txt) and [src/denoise/LICENSES.txt](../src/denoise/LICENSES.txt). All models and runtime dependencies are included in the package; no first-use download is needed. Inference runs locally. Reinstall the package if a model is missing or damaged.
