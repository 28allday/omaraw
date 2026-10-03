# Retouch

Remove blemishes, sensor dust and small distractions.

## AI object removal

Open **Prepare**, or use the AI object removal shortcut here.

Choose **AI object removal**, downloading local AI tools on first use. Click
an object, right-click to exclude, or drag a box. Switch to **Brush** to include
shadows, reflections or missed parts; right-drag erases. You can also brush a
removal without first selecting an object. **Undo selection** restores the
previous draft. Choose **Preview removal**,
compare with **Show removal preview**, then **Apply removal**. Stay on the same
photo and select another object, or continue editing. Each removal has its own
Undo step. Existing adjustments remain editable, and later colour, crop and
perspective changes also affect repaired areas. **Object removals** switches
all repairs off/on; **Reset removals** clears them with Undo available.

**Save rendered DNG…** optionally creates a separate DNG with the current look
included. Keep editing the original for editable repairs. Repairs are saved
with the catalogue and included in its full backups; they are not cache files.

Best for small distractions. Generated pixels come from an 8-bit model with
a 512-pixel working size, so inspect the preview. The adjustable **Edge coverage**
margin covers edges and blends into the surroundings. Multiple removals can
overlap. Generated areas cannot recover the original scene behind an object.
Removal uses CPU on every GPU vendor. Photos stay local; no assistant login
is needed. Reconnect the original/full offline copy if only a Smart Preview
is available.

## Heal, clone, blur and fill

1. Pick a tool: **Heal** blends the source's texture into the spot's surroundings; **Clone** copies the source as it is; **Blur** softens the spot; **Fill** paints it with a brightness.
2. Set a **Size** and a **Feather**.
3. Press **Place spots** and click a blemish.

Heal and clone read their pixels from a source circle that starts beside the spot; drag either circle on the picture. Every spot stays in the list, editable: tool, size, feather, opacity, and blur radius or fill brightness. Spots ride in snapshots and appear in History under Retouch.

You can leave **Place spots** enabled while moving an existing spot or its source. Click elsewhere to add another spot. Each drag updates the outline immediately and applies one undoable edit on release. Use the zoom strip or Ctrl + wheel while these tools are open.

## Brush and polygon shapes

Choose Circle, Brush stroke or Polygon path above the tool buttons. Brush strokes follow a drag. For polygons, click corners and double-click to close; right-click cancels the unfinished polygon. Heal and clone have a draggable source anchor. Dragging a shape or source shows its outline immediately and applies the edit on release. Shapes, source positions, algorithms, opacity and layers are retained in snapshots and Retouch presets.

Switching away from Retouch or changing photos discards an unfinished stroke
or polygon. The guides remain responsive at high zoom.

## Frequency separation

Set **Frequency layers** above zero, then choose which layer receives new shapes. Detail layer 1 contains the finest texture; higher layers hold broader detail. **Residual tone** holds the broad colour and lighting. Each shape can be assigned to a different layer in its settings. Whole image edits affect the combined image.

**Preview selected layer** shows the selected layer at fitted-preview resolution. The finest layers may be invisible at that scale. Toggle it off to see the complete image. This diagnostic preview never changes exports.

## Keeping the original after AI

**Apply removal** adds an editable repair to the current photo. It keeps your
adjustments and prepares the next selection. No new photo is needed.

**Save rendered DNG…** creates a separate version with the displayed appearance
baked in. **Photo versions** opens that copy or returns to its original. Keep
using the original to revise editable repairs and grading. Nothing is deleted
when you switch. If using both AI tools, apply denoise before object removal.
