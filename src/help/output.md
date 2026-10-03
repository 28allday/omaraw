# Output

Export the selected photos, or everything shown, through the engine. The render is the one you saw in Develop.

## Format and size

- **Formats**: JPEG, WebP, AVIF and JPEG XL with a quality; TIFF and PNG at 8 or 16 bit; PSD, a flat Photoshop document at 8 or 16 bit with the profile and metadata embedded, for a layered editor.
- **JPEG presets**: Web (2048 px, quality 85), Full size (quality 92), Print 4000 px (quality 95, 300 ppi), Proof (1200 px, quality 80), and Print full size (quality 95, 300 ppi). All use sRGB. The full-size print option keeps all developed pixels instead of limiting the long edge to 4000 px.
- **Editing master**: full-size 16-bit TIFF in linear ProPhoto RGB, tagged at 300 ppi, for further editing in a colour-managed editor.
- Presets set format, depth, colour, size, quality, suffix and print resolution, with output sharpening off. Choose screen/print sharpening under Finishing when needed. Your destination, metadata choices and watermark remain as set. Changing a preset's settings shows **Custom export settings**.
- Resize by long edge, short edge, width, height, megapixels or percent. Nothing is ever upsized.
- **Colour**: sRGB, RGB (1998), linear ProPhoto RGB or a custom RGB ICC profile with its intent. The profile is embedded.

## Naming and destination

**Choose folder…** opens OmaRAW's themed browser, with shortcuts to common folders and mounted drives, a directly editable path, and **New folder**. Profile and watermark file choosers use the same controls. Preset exports add a file-name field and ask before replacing an existing file.

To create an export destination, open its parent folder, choose **New folder**, enter a name and select **Create folder**. The browser opens the new folder; select **Choose folder** to use it for export. Existing files and folders are never replaced by folder creation.

Files keep their name plus the preset's suffix, and nothing is overwritten. A **filename pattern** replaces that: `{date}-{name}{suffix}`, with {name} {seq} {date} {time} {camera} {rating} {folder} {tag} and {suffix}; the row beneath shows the first file's name as it will be written. A variant exports as its own file with its name before the suffix.

## Finishing

- **Output sharpening** for screen or print, low, standard or high, sized to the export.
- **Watermark**: text and/or an image in any of nine positions with size and opacity. The preview shows where it lands.

Both act on the export's own pixels before the file is written, so every format can be finished and the file is compressed once.

## Resolution, alongside, metadata

- **Print PPI**: the print-resolution tag. **No override** (formerly shown as 0) leaves the resolution supplied by the source or encoder alone. It does not mean the photo has zero detail. Choose 150, 240, 300, 360 or 600 ppi, or **Custom** for an exact whole-number value. Changing PPI does not resize the image or change JPEG compression quality: a 6000-pixel edge tagged at 300 ppi represents 20 inches. Use the size controls to change the pixel count.
- Format, depth, size, quality, suffix, PPI, colour and sharpening are remembered when you reopen OmaRAW. Named templates below retain the broader workflow settings too.
- **Alongside**: copy the original file and its sidecar next to each export.
- **Metadata**: camera EXIF, location, keywords and the develop history are each a switch. Location is off by default.

## The queue

Every file with its state and a progress bar; Pause, Resume, Retry failed, Cancel remaining and Clear. The queue survives a restart. Hover a row for Reveal in file manager, Export again and Remove.

## Templates

Save named export and print templates in the left sidebar; a template remembers destination, naming, colour, metadata, finishing and page settings.
