# Importing

Ctrl+I, the Import button, or `omaraw ~/Pictures/job` on the command line. Subfolders are walked. RAW files from every major camera maker plus JPEG, TIFF, PNG, WebP, HEIF, AVIF and JPEG XL are catalogued.

## Browse and import in the Library

Choose **Browse** at the top of the Library's left panel, or press **Ctrl+I**. Import takes place in the main Library workspace; there is no separate photo import window.

The left panel lists Pictures, Home, Downloads, Computer and mounted drives. Click a folder to see its photos in the central grid and its subfolders on the left. The up arrow opens its parent; Ctrl+L lets you enter a path directly. The star saves the current folder in **Favourites**. Your last source folder and favourites are remembered.

**New folder**, below the path field, creates and opens a subfolder in the current location. Enter a name and choose **Create folder**. The prompt shows where it will be created. The photo-storage and second-copy pickers have the same **New folder** button; after creating a destination, choose **Choose folder** to use it.

Tick the photos you want. **All** selects every eligible photo; **None** clears the selection. Double-click a photo or choose **Preview** for a larger view. Arrow keys move between photos; Space ticks or unticks the current photo. Shift-click a photo selects or clears the range from your last tick. **Include subfolders** controls whether nested folders' photos are shown. Refresh updates the file list and resets picks.

Nothing enters the library until you choose **Import N photos**. Only the ticked files are imported, including when copying or moving. Files arriving in the source folder after review are not added to that selection. Previews show the original file or embedded camera preview; developed RAWs may look different. A missing preview does not prevent selection.

Photos already linked from this exact location are marked **In library** and excluded when duplicate skipping is enabled. Other duplicate checks run during import, so the final imported count may be smaller. Import settings sit in the right panel; narrow layouts use **Photos** and **Import settings** tabs within the workspace.

A file link that points outside the source folder is shown with an explanation and cannot be selected. Browse the photo's original folder to import it. The other eligible photos remain selected and can be imported normally.

The **Library** tab returns to your catalog without importing anything. Switching between Library and Browse preserves your picks. While browsing, Select All/None and photo-navigation shortcuts act on the review; catalog rating, removal and adjustment actions are unavailable. A successful import returns to **Previous Import** in the Library. Errors stay beside the import controls, and **Stop import** stops an active import after its current safe stopping point.

File imports use the same browser, including presets, profiles, LUTs and masks. **File type** filters the list; folders remain visible for navigation. Ctrl-click or Shift-click selects several preset files. **New folder** creates a folder in the displayed location. Every browser follows the current appearance and Colour Critical settings.

The workflow is **source → choose photos → import settings → import**. The settings panel names the active catalog and shows its location separately from the photo folder. Original photographs remain separate files; the catalog stores links and editing information.

- **Copy:** copy photos to a folder you choose, then link the catalog to the copies. Source files stay where they are.
- **Move:** move photos and their sidecars to a folder you choose, then link to them. Files leave the source folder.
- **Add:** keep photos in their current folders and add links to them. Photo storage shows the source folder.
- **Verified copy:** copy photos, read each copy back and compare its checksum, then link to it.
- **Photo storage:** choose the destination for Copy, Move or Verified copy. It can be on a different drive from the catalog. **Subfolders** can organise files by year, year/date or date of capture.
- **Naming**: keep the original names, or rename to date + name or prefix + number. A name clash never overwrites.
- **Skip photos already in the catalog**: same name, size and capture time. With the checksum switch on, the same bytes under any name are skipped too.
- **Second copy**: a backup of every file as it comes in, under the same subfolders and names, verified when the import verifies.
- **Eject**: when the source is a card or drive, power it off after a clean verified import.
- **Metadata preset**: creator, copyright, title, caption and keywords applied to the new entries.

Your last import settings are remembered; the photo selection starts afresh for each source review. Review **Method** and **Photo storage** before importing. **Import N photos** remains visible while you scroll through the options. Catalog backups contain editing information; back up your photo folders separately.

Library previews build in the background as each photo arrives, while the rest
continue importing. Small grid previews take priority; larger Loupe previews
follow. You can switch to **Library → Previous Import** to see the photos already
added. Any remaining previews continue after the file import finishes. Stopping
an import keeps the photos already added and lets their previews finish.

## Other ways in

- **Devices**: the sidebar lists mounted cards and drives with their free space; the arrow imports from the card's DCIM folder.
- **Watched folders**: right-click a folder ▸ Watch for New Photos. Anything that lands there is imported a couple of seconds after the disk goes quiet.
- **Capture**: tethered shots go straight into the catalog.

## Auto-apply presets

Right-click any preset ▸ Auto-apply on import… and say which photos get it: camera or lens contains, an ISO range, aperture, shutter and format. Matching imports get the preset as they land. If several rules match, the last matching rule wins; presets do not stack. Adjustments ▸ Presets ▸ Apply Auto Presets to Selection runs the rules over photos already in the catalog.

## Sidecars

An XMP sidecar beside a photo supplies its rating, colour label, keywords, title, caption, creator and copyright on import, and its edit: OmaRAW's own exactly, or the source editor's converted as a starting point. A RAW's sidecar uses the photo's base name (DSC_0833.NEF → DSC_0833.xmp), so other compatible editors can share it. Writing is off by default: Preferences ▸ Write metadata sidecars automatically keeps a sidecar current (metadata and edit), Library ▸ Sidecars ▸ Write Sidecars for Selection Now writes once, and Read Sidecars for Selection takes them back. Write Into DNG, JPEG and TIFF Files puts those formats' metadata inside the file. When another program has changed a sidecar, the inspector's Sidecar row lists each difference with a button to take it.

## Smart Previews

Enable **Build Smart Previews after import** to prepare smaller editable sources beside the catalog. Wait for the preview job to finish before disconnecting originals. You can also build or discard them later through **Library → Smart Previews**. Develop marks offline preview editing clearly; reconnect an original for full detail and export. Previews are not backups of your photos.

A visible thumbnail does not guarantee that a Smart Preview can be built: the camera's embedded picture can be readable while its RAW compression or sensor format is unsupported. A failed build leaves the original and any existing Smart Preview intact. After updating OmaRAW, select the affected photos and choose **Library → Smart Previews → Build Smart Previews for Selection** to retry.

## Automatic subject tags

Enable **Automatically tag imported photos** to scan new photos locally after
import. The bundled model supports 80 subject classes and needs neither a
network connection nor a GPU. This is the same remembered switch as Library's
**Automatic tagging**. Off stops scanning and keeps existing tags and corrections.

**Tagging settings…** chooses which automatic collections appear in this catalog.
People, Animals, Transport, Sports equipment and Food are selected by default;
collections are created when matches exist. Other objects and individual
collections such as Birds or Bicycles can be enabled separately. Unticking a
collection hides it without deleting its name or tags.

In Library, use **Add / correct…** to fix detections, **Scan selected** for existing
photos, and Pause/Resume for queued work. Corrections and pending scans survive
reopening. Tags stay separate from keywords and exported metadata. Small or
obscured subjects may be missed; landscapes and other scene types are not covered.
