# OmaRAW user guide

This guide contains the same instructions as Help (F1) in OmaRAW.

## Welcome to OmaRAW

OmaRAW is a photography library, a RAW developer, a tethered capture desk and an output page in one window. Your photos stay where they are on disk; a **catalog** holds what OmaRAW knows about them: ratings, keywords, albums and every edit.

When you reopen a catalog, OmaRAW returns to the last selected photo or variant, with its collection, filters and sort order. The filmstrip brings that photo into view. Each catalog remembers its own place. If the photo has been removed, a remaining photo is selected; offline originals stay selected.

The launch screen shows your OmaRAW version, a looping Spectrum animation and
a photographer's quote. Click **Continue** or press **Enter** to open the program.
Close the screen or press **Escape** to quit. The quote changes between launches
and works offline; click the photographer's name to visit its source. You can
pause the animation, and the Reduced motion preference uses a still image.

AI masking, object removal, RAW denoising and subject tagging include their models
and runtime libraries. They work offline after installation, without further
downloads or setup. Photos are processed on your computer.

### The workflow

1. **Browse** folders in the Library (Ctrl+I), preview the photos and tick those you want. **Import N photos** adds them in place, or copies, moves or verifies them into a destination you choose.
2. **Cull** in Library: stars, picks and rejects, colour labels and keywords, one key per frame with Auto-Advance on.
3. **Develop** the keepers: white balance, tone, colour, detail and lens on the right, **Masks** and **Retouch** above the tool menu, presets and history on the left.
4. **Output**: export photos, make a print PDF or create a contact sheet. Photo exports use the same processing as Develop; contact sheets use the edited Library thumbnails.

The four workspaces sit across the top: Ctrl+1 Library, Ctrl+2 Develop, Ctrl+3 Capture, Ctrl+4 Output. The filmstrip along the bottom follows you between them.

### Finding your way

- **Help**: press F1 anywhere for this window opened at the current workspace, or click the ? in a panel header for that panel's page. Use **A−** and **A+** to change the reading size, and **Expand** to fill the application window. Your reading size is remembered.
- **Tooltips**: rest the pointer on a control, menu command or status-bar item for an explanation. Adjustment sliders say what moving them changes.
- **Command search** (Ctrl+K): type the name of any menu item and press Return. Hover a result for the same explanation as its menu entry; dimmed results are unavailable for the current selection or workspace.
- **Keyboard shortcuts** (?): search all shortcuts and change their keys in one window. Escape closes it, as it does command search and menus.

### Where things live

**Catalog location** is chosen when creating a catalog. **Photo storage** is chosen when importing: Add links to existing files; Copy or Move uses a destination you choose. The catalog records file locations and edits, while photographs remain separate files. The catalog and photos can be on different drives. Edits are kept beside the catalog. Catalog backups run daily and before every upgrade; File ▸ Catalog Maintenance ▸ Reveal Backups shows them.

### Finding commands

The menus group commands by task:

- **File:** create or open catalogs, import, export, catalog maintenance and quit.
- **Edit:** undo, redo, selection, shortcuts and Preferences.
- **Library:** albums, Quick Collection, stacks, duplicates, sidecars and offline files.
- **Photo:** ratings, flags, labels, variants, rename, move, reveal, remove and trash.
- **Adjustments:** automatic corrections, copy/paste/sync settings, masks, crop, retouch, presets, snapshots and reset.
- **View:** browsing, comparison, exposure overlays, Light interface, Colour Critical, viewer background, panels and workspaces.
- **Camera:** connection, live view and capture.
- **Help:** guide, command search, shortcuts and About.

Preferences holds accessibility, automatic sidecar and backup policies, label names, camera profiles and colour management.

Press **Ctrl+K** to search for a command by name or menu group. Commands keep their existing keyboard shortcuts when their menu location changes.

### Using an agent

Installation includes OmaRAW's command-line skill and links it for your user.
Start a new agent session to use it. An agent can inspect edits, adjust photos,
organise albums and keywords, and export your chosen photographs. Close the
OmaRAW window before an agent opens its catalog. Ratings, flags and colour
labels remain choices you make in the Library.

Run `omaraw skill` to check the links, or `omaraw skill --link` to repair them
or set them up for another user. Updating the app updates the packaged skill;
existing custom skills are preserved.

## Keyboard

Press **?** for the searchable keyboard shortcuts window. Edit ▸ Keyboard Shortcuts and Help ▸ Keyboard Shortcuts open the same window. Search by action or key, click a binding to change it, or use its reset button to restore the default. Hold-to-peek keys are listed for reference and cannot be reassigned. **Escape** closes the window, or cancels an active key capture. The most used:

- **Ctrl+1 to Ctrl+4**: Library, Develop, Capture, Output.
- **Ctrl+I** import, **Ctrl+Shift+E** export, **Ctrl+K** command search, **F1** help.
- **G** grid, **L** list, **E** loupe, **C** compare, **N** survey; **Left** and **Right** move between photos.
- **0 to 5** stars, **P** pick, **X** reject, **U** unflag, **6 to 9** red, yellow, green, blue labels, **B** Quick Collection.
- **Ctrl+G** stack, **S** expand or collapse, **Ctrl+'** variant.
- **R** crop and straighten, **J** clipping, **F** false colour, **\** before and after, **M** mask overlay, **Shift+L** lights out, **Ctrl+N** snapshot.
- **Ctrl+Z** and **Ctrl+Shift+Z** undo and redo a develop step; **Ctrl+Alt+C**, **Ctrl+Alt+V**, **Ctrl+Alt+S** copy, paste and sync settings.

## Library

The Library is where you look at everything you have, choose what is worth developing, and keep it organised.

The left panel has **Library** and **Browse** tabs. Library shows your catalog; Browse explores folders and drives, previews their photos in the main grid, and imports only the photos you tick. Ctrl+I goes directly to Browse. Import settings appear on the right, with no separate photo import window.

### The layout

- **Sources** (left): All Photographs, Quick Collection, Previous Import, Recently Added, Recently Edited, Recently Exported, Rejected, Offline, the folder tree, albums, smart albums, keywords and mounted devices. Every count is live.
- **Browser** (centre): the grid of cards. The toolbar above it holds search, the star, flag and label filters, the funnel for more filters, saved filters, sort order and the card size.
- **Inspector** (right): the histogram, every metadata group, keywords and the sidecar state. Switch to **Develop** to adjust a photo or apply a preset.
- **Filmstrip** (bottom): the same photos as a strip, with the current one marked. Drag the scrollbar beneath the previews to browse left or right; the arrow buttons select the previous or next photo.

### Views

- **Grid** (G): cards with thumbnail, stars, flag, label dot and badges. A badge shows the format, an edited mark, what the edit holds (crop, local adjustment, retouch), a pin for a place and a warning triangle for a sidecar changed outside OmaRAW.
- **Loupe** (E or double-click): one photo large. Roll the mouse wheel over the picture to zoom around the pointer; drag to pan. Wheel zoom also works on individual pictures in Compare and Survey.
- **Compare** (C): the current photo beside the other selected one, or the next.
- **Survey** (N): every selected photo at once.
- **Detail list** (L): the browser as a table. Click a column header to sort by it.

### Selecting photos

Click a photo to select it. Hold **Ctrl** while clicking to add or remove
individual photos. Click the first photo, then **Shift-click** the last to
select the range between them. These controls work in Grid, Detail list and
the Filmstrip. **Ctrl+A** selects every photo in the current view;
**Ctrl+D** clears the selection. Selected photos have an accent-coloured border.

### Albums

Select one or more photos, right-click a selected photo and choose **Add to Album**,
then choose an existing album. The same command is under **Library ▸ Albums**.
Nested albums include their parent names, for example **Travel / Italy**.
**New Album from Selection…** creates an album and adds the selected photos.
You can also right-click an album in Sources and choose **Add Selection**.
Smart albums collect matching photos automatically through their rules.

### Transferring adjustments

Right-click an edited photo and choose **Copy Settings…**, tick the adjustment
groups to copy, then click **Copy**. Select the destination photos, right-click
one of them and choose **Paste Settings**.

For a single batch, select the source and destination photos together,
right-click the source and choose **Sync Settings…**. The dialog names the
source and the other selected photos. Choose the adjustment groups and click
**Sync**. These commands work directly in Library, including Detail list.
Groups you do not choose keep their settings on each destination photo. Check
copied masks and crop positions when the photos have different compositions.
These commands act on catalog photos; return from **Browse** to **Library** first.

### Finding photos

Search covers file name, camera, lens, path, keywords, title and caption. The star, flag and label buttons filter; the funnel adds type (RAW or not), edited state, place and camera. Sort by capture time, import time, file name, rating or edit time, and by Custom Order inside an album. **Saved filters** (the bookmark) keep a whole set under a name.

### The status bar

Real totals for what is shown and selected, the current catalog, the engine's state and the colour configuration in use.

Photos with embedded GPS show latitude and longitude in the inspector. **Open location in browser** opens OpenStreetMap and shares those coordinates with the site only when you click it. Output’s **Location (GPS)** switch controls whether exported files include location data.

### Automatic tags

The inspector's **Automatic tagging** switch enables or disables offline subject
scanning. It also controls tagging on new folder imports. Off stops the worker
and keeps existing tags, corrections and queued scans. Turn it back on to resume.
Use **Scan selected** for existing photos while enabled, or Pause/Resume to stop
work temporarily without changing the import switch.

Detected tags appear as removable labels. Click a label's cross to exclude it, or
use **Add / correct…** to search 80 supported subjects and tick missed ones.
Corrections survive rescanning. **Use detections** restores the last scan's labels.
Manual corrections work even while scanning is off. Virtual copies share their
original's tags; your keywords, edits, originals and exported metadata are unchanged.

**Settings…** chooses automatic collections for this catalog. People, Animals,
Transport, Sports equipment and Food are selected initially and appear only once
matches exist. Other objects and individual collections can be enabled separately.
Unticking hides a collection and preserves its name and tags. Custom smart albums
can use individual subjects or whole groups in the **Auto tag** rule.

Detection is a sorting aid: small, hidden or unusual subjects may be missed, and
pictures or toys can be mistaken for real objects. Scenery such as landscapes,
beaches and sunsets is outside this model's supported subjects.

### Catalog maintenance and browsing speed

Open **File ▸ Catalog Maintenance ▸ Overview…** to see the active catalog's
location and size, the latest backup folder, and browsing-cache usage.
**Check Catalog Integrity…** runs in the background and leaves a result in this
window. **Show report** includes the check time, locations and individual results;
**Copy report** copies it for diagnosis. The check covers catalog structure and
references plus the Develop databases' structure. It does not verify original
photos or how edits look, and it makes no repairs. If errors are reported, keep
the current catalog and restore a verified backup into a separate folder.

**Back up now** leaves a visible success or failure message, including the saved
location. Backups include catalog and Develop data; original photos and external
profiles need separate backups.

OmaRAW keeps browsing previews on disk between sessions and recent photo records
in memory. **Cache and memory settings…** opens Preferences ▸ Performance, where
you can see the cache folder, usage and size limit (4 GiB by default). Older
browsing previews are removed automatically. Clearing this cache keeps photos,
edits, edited preview masters and Smart Previews; browsing previews rebuild as
needed, so clearing it is not a routine speed-up step.

## Importing

Ctrl+I, the Import button, or `omaraw ~/Pictures/job` on the command line. Subfolders are walked. RAW files from every major camera maker plus JPEG, TIFF, PNG, WebP, HEIF, AVIF and JPEG XL are catalogued.

### Browse and import in the Library

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

### Other ways in

- **Devices**: the sidebar lists mounted cards and drives with their free space; the arrow imports from the card's DCIM folder.
- **Watched folders**: right-click a folder ▸ Watch for New Photos. Anything that lands there is imported a couple of seconds after the disk goes quiet.
- **Capture**: tethered shots go straight into the catalog.

### Auto-apply presets

Right-click any preset ▸ Auto-apply on import… and say which photos get it: camera or lens contains, an ISO range, aperture, shutter and format. Matching imports get the preset as they land. If several rules match, the last matching rule wins; presets do not stack. Adjustments ▸ Presets ▸ Apply Auto Presets to Selection runs the rules over photos already in the catalog.

### Sidecars

An XMP sidecar beside a photo supplies its rating, colour label, keywords, title, caption, creator and copyright on import, and its edit: OmaRAW's own exactly, or the source editor's converted as a starting point. A RAW's sidecar uses the photo's base name (DSC_0833.NEF → DSC_0833.xmp), so other compatible editors can share it. Writing is off by default: Preferences ▸ Write metadata sidecars automatically keeps a sidecar current (metadata and edit), Library ▸ Sidecars ▸ Write Sidecars for Selection Now writes once, and Read Sidecars for Selection takes them back. Write Into DNG, JPEG and TIFF Files puts those formats' metadata inside the file. When another program has changed a sidecar, the inspector's Sidecar row lists each difference with a button to take it.

### Smart Previews

Enable **Build Smart Previews after import** to prepare smaller editable sources beside the catalog. Wait for the preview job to finish before disconnecting originals. You can also build or discard them later through **Library → Smart Previews**. Develop marks offline preview editing clearly; reconnect an original for full detail and export. Previews are not backups of your photos.

A visible thumbnail does not guarantee that a Smart Preview can be built: the camera's embedded picture can be readable while its RAW compression or sensor format is unsupported. A failed build leaves the original and any existing Smart Preview intact. After updating OmaRAW, select the affected photos and choose **Library → Smart Previews → Build Smart Previews for Selection** to retry.

### Automatic subject tags

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

## Culling and rating

The cull is one key per frame. With Photo ▸ Auto-Advance After Rating on, every rating, flag or label moves you to the next photo.

- **Stars**: 0 to 5, keys 0 to 5.
- **Flags**: P pick, X reject, U unflag. Rejected photos leave every ordinary view and collect under Rejected.
- **Colour labels**: six colours from the dots on a card or the Edit menu; 6 to 9 set red, yellow, green and blue. Edit ▸ Label Names… gives each colour a meaning (Client, Print…) that the menus and tooltips show.
- **Keywords**: type in the inspector. `Travel/Italy/Venice` makes a level for each part, shown as a tree in the sidebar. Recent keywords sit under the field as chips; synonyms (right-click a keyword ▸ Synonyms…) make search find a photo by any of its words.

All of these act on one photo or the whole selection.

### Stacks

Select a burst and press Ctrl+G to stack it. The grid shows the top photo with a count; S expands or collapses, Shift+S makes the current photo the top, Ctrl+Shift+G unstacks. Auto-Stack by Capture Time in the card menu groups shots taken within a chosen number of seconds.

### Variants

Ctrl+' makes a virtual copy on the same file that starts from the photo's current edit and then goes its own way. Rename, Promote to Master and Delete Variant are in the card menu. A variant exports as its own file.

### Duplicates and batch metadata

Photo ▸ Duplicates ▸ Checksum Selection reads the files once; Select Duplicates then selects every shown photo that shares a checksum with another. With several photos selected, the inspector's Title, Caption, Copyright and Creator fields apply to all of them and read Mixed when they differ.

## Organising

### Albums

Select one or more photos, right-click a selected photo and choose **Add to Album**,
then choose the album you created. The same command is under **Library ▸ Albums**.
Nested destinations show their parent names, such as **Travel / Italy**.
**New Album from Selection…** creates an album containing the selected photos.
One photo can belong to several albums; adding it does not move or copy its file.

Right-click an album to add or remove the selection, rename or delete it, or make a New Album Inside it. Albums nest; the sidebar folds each branch. With an album as the source, Sort offers Custom Order: drag a card where it belongs, and Set as Album Cover puts a photo on the album's row.

### Smart albums

Press + beside the heading and build rules: stars, flag, label, keyword, file name, camera, lens, format, edited or not, a date range, stack tops or **Auto tag**, matching all or any rule. Auto tag can match a detected subject or a subject group. The count is live. Right-click to edit or delete.

### Quick Collection

B adds the current photo or selection to the Quick Collection, a scratch set that survives until you clear it.

### Files on disk

- **Rename File…** and **Move Selection to Folder…** (right-click a card, or File) act on the disk. The sidecar, variants, edits and thumbnails follow, and nothing is overwritten. Moves are journaled, so a crash half way is settled on the next start.
- **Move to Trash** (Shift+Del) sends the files and their sidecars to the system trash. **Remove from Catalog** (Del) keeps the files where they are.
- **Offline volumes**: folders whose drive is unplugged are greyed and their photos collect under Offline; everything comes back with the drive. Right-click a moved folder ▸ Relink Folder… to point the catalog at its new place.
- **Smart Previews** (Library ▸ Smart Previews) save smaller editable sources beside the catalog. Build them before disconnecting a drive, or enable building after import. The inspector shows their size. Develop switches back when the original returns. Export needs the original or a full offline copy. Discard previews when a matching full source is available; edits stay saved.
- **Offline originals** (Library ▸ Offline Originals) keep verified full-quality working copies for the selection, so you can develop and export with the drive disconnected.
- **Relink**: an offline photo's inspector offers Locate… and Search a folder…, which finds every offline photo under a folder by name, size and capture time.

### Catalogs

To create a separate database, choose **File ▸ New Catalog…**:

1. Give the catalog a name, such as **Travel 2026**.
2. Under **Catalog location**, click **Choose…** to select its parent folder. OmaRAW shows the complete path it will create: a new folder named after the catalog, containing `catalog.db`.
3. Select **Create & Open** (or press Return). OmaRAW creates an empty catalog and switches to it. Cancel closes the dialog without creating anything. Existing catalogs and non-empty folders are not overwritten.
4. In the empty Library, choose **Import Folder…** to add photos. The import screen shows the active catalog location, source folder, import method and **Photo storage** folder.

Original photographs remain ordinary files in folders on your drives. The catalog stores links to those files and their editing information. Catalog location and photo storage can be on different drives.

Creating the catalog does not move your photos or transfer edits from the previous catalog. **Open Catalog…** and **Open Recent** switch back to an existing catalog. If an import or export is running, OmaRAW asks before switching.

OmaRAW normally reopens the last catalog. In **Edit ▸ Preferences**, enable **Choose a library at startup** to choose each time. On a first launch without that preference, OmaRAW creates its default catalog automatically.

Backups run before upgrades and once a day, keeping the newest seven. Back Up Catalog Now makes one on demand; Restore Backup… recovers a bundle into a new folder. Bundles hold the catalog and the engine's databases, including Develop history and masks, not your photos.

**File ▸ Catalog Maintenance ▸ Overview…** shows the catalog, backup and cache
locations. **Check Catalog Integrity…** leaves its result there, with **Show
report** and **Copy report** for details. It checks database structure and catalog
references without making repairs; it does not check the original photo files.
The Library help page explains the report and cache controls.

## Develop

Develop renders your photo through the processing engine. What you see is the same processing an export gets, and the histogram is of that render.

### The layout

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

### Working with sliders

Drag a slider, or click its figure to type a value (drag the figure sideways for fine changes). A control with a real unit shows it: stops (EV), kelvin, degrees, pixels or a percentage. The others read **0 where they start**, from −100 to +100 (Contrast, Saturation, Clarity), or from 0 for none to 100 for the most (an amount such as Sharpening). Presets, History and the command line keep the engine's own values, so nothing saved changes meaning. Those figures move in whole steps on a drag and in tenths with **Shift**, with **Alt** and the arrow keys, or typed. Once a slider has been clicked, or reached with Tab, the arrow keys step it: **Shift** for ten times the step, **Alt** for a tenth. **Ctrl** with the mouse wheel over any slider steps it the same way. **Double-click a slider or its name to put it back**; Alt-click an adjustment's heading to put the whole adjustment back; a changed control also shows a small ↺ beside its figure when the pointer is on it.

Tools with only one or two finer controls show them directly. **Advanced** is reserved for larger sets, such as Print stock's whites and channel contrast and Vibrance & saturation's extra colour controls. Tools and Advanced controls slide open and closed. Popup menus and dropdown choices also open with a short downward motion; Reduced motion makes them instant. Folding a tool, switching sections or hiding controls never changes the photo.

An adjustment that is not applied shows an unlit dot instead of a figure. Moving one of its sliders brings it in. Click its **status dot** to switch the effect off or on while keeping its settings. Every tool heading has a visible **↺ Reset** button, including Black and white. It restores that tool’s starting settings and applied state, keeping edits in other tools. Black and white returns the picture to colour. **Ctrl+Z** brings the reset effect back in one step. The **⋯** menu also offers reset, copy, paste and save as preset. Combined headings such as Light report whether any contained adjustment is applied; use each adjustment’s dot to switch it. The functional green indicator keeps its meaning in Colour Critical mode.

**Auto** above the workflow menu calculates exposure from the picture while protecting highlights. The image stays steady during the calculation and updates once with the finished result. Pressing Auto again without editing the photo leaves it unchanged. The exposure supplied on import is a fixed starting correction, not this measured Auto result, so the first Auto calculation can choose a different brightness.

The tool heading’s **↺** resets that complete tool. Light keeps colour changes in Vibrance & saturation, Curves keeps Lab colour, and Perspective keeps your rotation. Individual adjustments within grouped tools can also be reset from their ⋯ menus. **Reset** returns the whole photo to how it looked after import: its starting exposure, colour and tone, with the automatic sharpening, colour noise reduction and lens correction. **Reset to Camera Original** (Adjustments menu, or **Back to original** in History) goes all the way back to the raw file with nothing on; Auto on a photo there brings the starting tone back with it. Both full-photo resets discard previous development edits, local masks and redo steps. They cannot be undone. Only the starting processing steps remain; the import starting settings are retained separately so Reset can restore them after Camera Original. Individual tool resets remain undoable.

Resetting Local masks clears their adjustments and switches them off while keeping their shapes. Retouch resets all its spots; Undo restores them. AI denoise Reset clears the preview and restores its controls. A saved denoised DNG is a separate photo: choose **Photo versions → Original** to return to its source.

### The first edit

A newly imported RAW gets a starting edit the first time it opens here: **capture sharpening**, **colour noise** taken out (the grain stays), and the **lens profile** when the lens is recognised. It keeps the camera/default exposure and colour rendering rather than running measured Auto exposure. It happens once. **Reset** keeps it, as part of how the photo looked after import; **Reset to Camera Original** removes it, and it is not given again. Photos a preset rule has already given a starting point keep that instead. Turn it off in Preferences ▸ Processing ▸ Automatic first edit.

The graph at the top is a histogram by default. Use the **Scope** dropdown above it to switch to **Waveform**, **RGB parade**, **Vectorscope** or **False colour** (the picture repainted by brightness). RGB parade shows the red, green and blue channels side by side, with black at the bottom and white at the top. It follows your adjustments without changing the photo. A line under the graph reports clipped highlights or shadows.

Click **Enlarge scopes** beside the dropdown for a larger panel over the photo. Drag its **Scopes** heading to move it, or its bottom-right corner to resize it down to half size. You can keep adjusting the photo with it open. Both scope selectors stay in sync. Click **Compact** (the inward arrows at smaller sizes), click the expand button again, or press Escape to return to the small view. Your panel size is kept when you reopen it during the session. The panel stays inside the viewer when you resize the window, and closes when you leave Develop or enter Lights out.

The vectorscope shows labelled colour targets at 75% and a dashed skin-tone
reference in both sizes. Use the skin line as a hue guide; skin and lighting vary.

### The viewer

- **Zoom**: roll the mouse wheel over the picture to zoom in or out around the pointer; drag to pan. Fit, Fill, 50%, 100% and 200% are in the strip, Ctrl + wheel steps between them, and double-click toggles Fit and 100% when no on-picture tool is active. At 100% and above the picture is rendered in tiles from the full-resolution pipe.
- **Live adjustments**: sliders and curves show an updating preview while you drag, then restore sharp detail on release. With Faster fitted previews enabled, the moving preview uses a smaller image to stay responsive on large RAW files. Reset buttons remain available while the preview renders. The filmstrip keeps its current thumbnail visible until the updated preview is ready.
- **Before and after** (\ or the compare button): the untouched photo against the current render, split or side by side.
- **Clipping** (J): blown highlights and crushed shadows painted over the picture, with the percentages in the pill.
- **Crop and straighten** (R): see the Lens page.
- **Lights out** (Shift+L) clears the panels; View ▸ Viewer Background changes the surround.

### History, undo and snapshots

History lists every step, oldest first. Click a step to go back to it; the next change continues from there. Original is the untouched render. Ctrl+Z and Ctrl+Shift+Z walk the same list. Back to original resets the photo and discards its edit and redo history.

Snapshots keep a named copy of every setting plus a preview of the render at that moment (Ctrl+N for a quick one, + to name it). Click one to restore it; the eye compares it against the live render, split or side by side.

Zoom to 100% to compare native detail from the saved settings, including its crop, masks and retouching. Pan to inspect another area. Both sides use the same pixel scale, even when their crops differ. Comparing leaves the current settings and Undo/Redo history intact. Detail uses the original, a verified offline working copy, or the available Smart Preview. Smart Preview detail is limited to its reduced resolution. If it cannot render, the saved overview stays visible with an explanation.

### Presets

Presets start in collapsed category folders. Open one to browse its looks;
Favourites and Recent offer shortcuts to the ones you use most. Search also
finds presets inside closed folders. The list has its own scrollbar and a
limited height, so History stays below it. On smaller windows, the left dock
also scrolls to reach Snapshots and Soft Proof.

The category dropdown offers 24 curated recipes across **Film**, **Portrait**, **Landscape**, **Cinematic** and **Black & white**. Hover over a recipe for its description. These combine print stocks, colour, contrast and finishing effects while preserving exposure, white balance, detail, crop and local corrections. Switching presets replaces the previous preset, including its film look, white balance, grain or curves when those were part of it. Adjustments made before the first preset, and later edits to other tools, are retained. Later tweaks to a tool controlled by the previous preset are replaced too. Each switch is one Undo step, and selecting an unchanged preset again does nothing. Adjust the controls afterwards to make the look your own.

The original eight built-in looks remain available. Save your own from the current sliders with +. Choose which adjustment groups a preset includes; a colour-only look leaves tone and detail alone. Search by name, description, category or tag; a star lifts a favourite to the top. Right-click a preset for its category, tags and an auto-apply rule for imports. Adjustments ▸ Presets exports and imports them as JSON. Import also accepts XMP .xmp and .lrtemplate Develop presets, with several files selected at once. Extract ZIP packs first. The report lists converted settings and anything untranslated; Last Import Report reopens it. XMP looks are approximate. A film-look preset that names a camera profile (a .dcp file from the same pack) uses the one for your photo's camera once Preferences ▸ Camera profiles folder… points at the pack's profiles: it then renders under Colour & Look ▸ Print stock in place of the tone mapper, like a print stock. Proprietary profiles, masks and many advanced adjustments are not converted, so check the result on a photo.

### Settings across photos

Adjustments ▸ Copy Settings… (Ctrl+Alt+C) asks which groups travel and puts them on a clipboard; Paste Settings (Ctrl+Alt+V) lays them on the open photo or every selected photo. Sync Settings… (Ctrl+Alt+S) sends the chosen groups from the current photo to the rest of the selection, developed in the background. The same commands are available in Library's photo right-click menu: for Sync, select the batch and right-click the source photo. The dialog names the source and destinations before applying anything.

For example, to reuse only an edited photo's colour adjustments, right-click it,
choose **Copy Settings…**, select the colour groups you need and click **Copy**.
Select the destination photos and use **Paste Settings**. Groups you leave out
keep their destination settings. If you include masks or crop, check their
placement on each destination photo.

### Soft proof

Pick a printer's or lab's ICC profile and the viewer shows the render as that profile would print it, with an optional gamut warning. Exports and thumbnails are never proofed.

### Editing Smart Previews

When the original and full offline copy are disconnected, Develop uses a saved Smart Preview and shows a notice above the photo. Edits stay with the same catalog photo and variants. Reconnecting the drive restores the original automatically. Check sharpening, noise and fine retouching on the original before exporting. Sensor clipping and full-quality output require a full source.

## Prepare

Start with the camera rendering and cleanup before colour and tonal work.
The tools appear in this order:

1. **Camera profile**: camera/input calibration and the starting camera look.
2. **Negative conversion**: optional film-negative processing; show it with Customise tools.
3. **AI denoise**: optional RAW denoising with an editable DNG result.
4. **Noise reduction**: Everything or Colour only, Amount, Keep fine detail and Hot pixels.
5. **Capture sharpening**: corrects capture softness at the RAW demosaic stage. This switch appears only for RAW photos; moving it here has not changed its processing.
6. **AI object removal**: select or brush distractions, preview and Apply. Each repair stays editable on the same photo. Apply prepares the next selection, including overlapping repairs.

Use AI denoise before object removal when using both. All tools remain optional;
you can return here after changing other adjustments. Creative profiles are
under Colour & Look, and ordinary output sharpening is under Detail.

For full controls, see the Colour & look, Detail and Retouch pages in Help.

## White balance, light and curves

**Light & Tone** contains White balance, Highlight recovery, Light, Tone mapping, Film tone, Tone equalizer, RGB levels and Curves, in that order. Light keeps Exposure, Black level, Contrast, Shadows and Highlights together. Specialist tools are available through **Customise tools**. Texture, clarity and dehaze are in **Detail**.

Exposure and Shadows & highlights keep their own status dot and **⋯** menu on their first slider row. Contrast shares the effect switch and adjustment presets in Colour → Vibrance & saturation. Double-click Contrast to reset just that slider. Resetting Exposure leaves Contrast and Shadows unchanged.

White balance offers Temperature and Tint, **As shot**, **Auto**, and a neutral picker. Click something grey or white in the picture after choosing the picker. **Auto** above the workflow menu estimates exposure from linear light before film looks and tone curves, with a highlight limit. It sets a new exposure rather than adding another correction each time. Import provides the camera/default starting exposure; it does not run this scene estimate. Auto is a starting point: night scenes, bright snow and small subjects can still need manual adjustment.

Unused Shadows, Highlights, Clarity and Dehaze rest at zero. Their slider resets also return to no effect. An effect switched off keeps its saved settings; switching it back on restores them. Sizes, distances, thresholds and camera settings have meaningful defaults of their own: for example, Dehaze Distance starts at 20, while white balance follows the camera. A fresh RAW also includes its base exposure and tone rendering.

### Exposure

- **Exposure** (EV): overall brightness in stops. +1 doubles the light.
- **Black level**: where black sits. Slightly negative lifts the deepest shadows for a matte look; positive crushes them.

### Shadows and highlights

- **Shadows**: lifts (+) or deepens (−) the dark areas without moving the midtones.
- **Highlights**: recovers (−) or brightens (+) the bright areas.

### Contrast

**Contrast**, in Light, strengthens or softens the overall tonal separation. It works with Tone mapping, Film tone or Print stock without replacing the chosen rendering.

### Tone mapping

Available through **Customise tools**. These settings shape the rendering curve; existing edits retain their saved values.

- **Curve steepness**: the slope of the rendering curve around middle grey.
- **Curve balance**: shifts the curve towards the shadows (−) or the highlights (+).

Using these controls selects Tone mapping in place of Film tone or Print stock. For an ordinary contrast adjustment that keeps either look, use **Light → Contrast**.

### Film tone

Available through Customise tools, and automatically visible on photos with edited Film tone settings: an alternative rendering with a filmic shoulder and toe. The picture has one rendering at a time, so moving a Film tone slider replaces Tone mapping or Print stock. Light → Contrast keeps Film tone in use.

- **Whites** (EV): how far above middle grey the picture reaches white.
- **Blacks** (EV): how far below middle grey it reaches black.

### Highlight recovery

What to do with sensor values that clipped. **Rebuild from surroundings (recommended)** rebuilds them from the surrounding colour; **Keep brightness, drop colour** keeps the brightness and lets the colour go; **Clip to white** simply clips; **Rebuild by region** and **Rebuild fine detail** are slower and finer; **Spread nearby colour** is the old method. **Clip threshold** sets the level counted as clipped.

### Dehaze

- **Amount**: cuts atmospheric haze; a little goes a long way.
- **Distance**: how far into the scene the effect reaches.

### Tone equalizer

Enable **Tone equalizer** in **Customise tools**, then open its heading. Its nine zones adjust exposure from −8 to 0 EV in a guided brightness mask. **Mask exposure** shifts the tones assigned to the zones; **Mask contrast** changes their spread. Detail preservation, smoothing and edge refinement keep fine texture while larger areas are brightened or darkened.

### Scopes and RAW clipping

The **Scope** dropdown above the graph offers Histogram, Waveform, RGB parade, Vectorscope and False colour. RGB parade shows separate red, green and blue traces, with black at the bottom and white at the top. They analyse the accepted sRGB preview without requesting another RAW render.

Use **Enlarge scopes** beside the dropdown to open a larger, movable panel over the photo. The adjustment dock remains usable. Drag the Scopes heading to reposition it and the bottom-right corner to resize it down to half size. **Compact** (the inward arrows at smaller sizes) or Escape returns to the small view. The large scope uses a higher-resolution graph and follows edits as you work.

The vectorscope includes coloured **75% targets**, labelled R, Y, G, C, B and M,
and a dashed **Skin** reference line. Traces near the centre have little colour;
traces farther out have more chroma. The skin line is a hue guide for neutral
lighting, not a requirement that every skin tone land exactly on it.

#### False colour

**False colour**, in the same menu, from the palette button on the picture or with **F** (tap to switch, hold to peek), repaints the photograph by how bright each part of it is, so exposure can be read off the picture itself rather than guessed from a graph. A colour guide appears on the picture, even with the side panels hidden. Its arrow folds or expands the guide; switching false colour on opens it again. Comparing (Before/After or a snapshot) shows both sides in false colour, so the two can be compared zone for zone.

- **Green** is middle grey, the brightness of a grey card (42–51 %).
- **Pink** is a light-skin brightness reference (58–66 %). Skin tones and lighting vary; it is not a target for every face.
- **Yellow** is highlights (80–92 %) and **orange** near white (92 % and up): bright areas approaching the top of the preview's range.
- **Teal** is shadows (8–20 %) and **blue** deep shadows (under 8 %).
- **Red** is clipped highlights and **purple** clipped shadows, by the same test as the clipping indicators: any channel at the top, every channel at the bottom. This describes the edited preview; RAW detail may still be recoverable by changing the edit.
- Everything between stays a black-and-white copy of the picture, so it remains readable.

The graph becomes the legend: one column per band, as tall as that band's share of the picture. Point at a column for its name, range and share. Brightness is that of the finished sRGB picture (the figure a waveform plots), including the active soft proof, without the gamut warning paint; it follows every edit and stays sharp when you zoom in. Press the palette button again to go back to the graph you had. Exports are never affected.

**Mark Clipped Sensor Areas**, under View ▸ Exposure Overlays, marks saturated sensor samples in magenta before exposure or highlight recovery. It supports 16-bit Bayer and X-Trans sources. The bounded preview samples a neighbourhood of sensor sites; very small clipped spots can fall between samples. Ordinary red/blue clipping indicators still measure the developed image.

## Colour

### White balance

White balance is the first tool in **Light & Tone**. It offers Temperature, Tint, As shot, Auto and the neutral picker.

### RGB primaries

RGB primaries changes the working colour space's red, green and blue primaries.
Each Hue slider covers −20° to +20° for practical colour correction; Purity
controls how far the primary lies from neutral. Tint hue and Tint amount can
deliberately colour the neutral axis. These are global colour changes, so use
Colour mixer or Selective colour when you want to adjust one hue range.

### Vibrance and saturation

- **Vibrance** strengthens the muted colours first and is kinder to skin.
- **Saturation** strengthens every colour equally.

**Advanced** adds Colourfulness, Colour brightness and Hue shift. Overall Contrast has its single home in **Light**. These settings share this tool's effect switch, reset and adjustment presets.

Open **Colour mixer**, **Selective colour**, **Black and white** or **Lab colour** by its heading. Lab colour is visible in the Colour & Look section by default. Enable **RGB primaries** through **Customise tools**. Camera profiles and optional Negative conversion are in **Prepare**. Colour wheels and LUTs are in **Colour & Look**.

### Adjust skin or another selected colour

Open **Masks → Colour Range**, then click or drag across the colour to select it.
Use **Range width** and **Softness** to refine the selection. Under **Colour**, push
the wheel towards a colour to add it, use **Saturation** to strengthen the existing
colour, or **Lightness** to brighten it. Similar colours elsewhere are selected
too; add a shape to limit the correction to the person or object. The Masks help
page covers the wheel, precise sliders and range controls.

### Image Match

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

### HSL by colour

Eight bands, Red through Magenta, each with **Hue**, **Saturation** and **Luminance**. Darken a blue sky with its Luminance, shift the greens with Hue, or pull the saturation of one colour without touching the rest.

### Lab colour

Richer, better separated colour without the picture getting lighter, darker or harsher. Lab describes a colour as a lightness and two colour axes, green to magenta and blue to yellow; these sliders work on the two colour axes only, so brightness never moves. Every slider rests at 0 and runs from −100 to 100; double-click one to put it back.

- **Colour separation** pulls the picture's colours apart from each other and from grey. Muted colours gain the most, strong ones are held back so nothing clips, and greys stay grey. To the left it draws them together.
- **By colour family**: **Greens**, **Reds & magentas**, **Blues** and **Yellows & warm tones** make one family richer or quieter and leave the others alone. Lift the greens of foliage without touching skin, or quieten a loud blue sky.
- **Remove a cast**: **Green–magenta** and **Blue–yellow** slide every colour along one axis. Move the slider away from the cast you see; the second one also serves as a last touch of cooler or warmer.

**Reset Lab colour** puts every slider back to zero.

#### Show curves

The sliders only place points on two curves, one for each colour axis. **Show curves** opens them for shaping by hand, the way Lab curves work in other editors.

- **a · green–magenta** and **b · blue–yellow**: the strips along the bottom and the left show which end is which. The readout is in Lab's own units, −128 to +127.
- **The centre of the square is neutral grey.** Keep the line through it and greys stay grey.
- **Steepen** the line through the centre to pull colours apart on that axis; do it on one side only to enrich, say, the warm colours and leave the cool ones. **Move the centre point** to shift the whole picture along the axis.
- Most real colours sit close to the centre, so small moves go a long way.

A curve shaped by hand is no longer something the sliders can describe, so they read zero and a note says so; moving a slider then replaces the hand-made shape. ↺ beside the axis buttons straightens the curve shown.

Lab colour shares the engine's Lab tone curve with **Tone regions** (under Curves), so presets, snapshots and copy/paste carry them together. While Lab colour is in use, Tone regions act on lightness alone (their colour looks a touch less saturated than otherwise); put Lab colour back to zero and they return to their usual behaviour.

### Selective colour

Eight hue bands with hue, saturation and brightness, plus **Smoothing and neutral protection** under the disclosure: smoothing stops adjacent bands from tearing, and neutral protection keeps greys grey.

### Advanced colour controls

Open **Advanced** in Vibrance & saturation.

- **Colourfulness**: perceptual colour intensity independent of brightness.
- **Colour brightness**: brightness of the coloured areas.
- **Hue shift**: rotates every hue.

### Camera profile

Shows the photo's camera. OmaRAW calibrates its colour automatically, with no download or setup.

**Apply camera look** appears when an included preset matches your camera. It adjusts contrast and saturation for a look inspired by the camera's JPEGs. Exposure, white balance, crop, local edits, local contrast and detail stay. It replaces Tone and Colour balance settings and switches off an active Print stock or DCP. Undo restores your previous look.

The **Colour profile** menu offers **Automatic colour** and, for supported cameras, **Community colour · included**. Community colour is an alternative supplied by RawTherapee contributors; it works offline. Only matching camera profiles appear. A DCP supplies its own colour and contrast, replaces **Print stock** and parks the regular tone controls. Your other adjustments stay. Choose **Automatic colour** to restore those controls; this does not reset them or undo a camera-look preset. **Colour & Look → Creative profile** can add a separate look on top.

**Additional profiles → Import profile…** adds a DCP you own and retains a copy. **Profiles folder…** scans an existing collection and its subfolders; **Refresh** picks up changes. Search appears for larger collections. Unavailable profiles explain why they cannot be used. The technical input-colour controls remain under **Advanced input colour**.

The 54 included community profiles and camera-look presets are freely redistributable; their licences and credits are supplied with the app. These are community interpretations, not exact manufacturer picture styles. DCP rendering is approximate and currently uses daylight calibration rather than blending lighting calibrations. JPEGs already contain their camera rendering, so these choices are for RAWs.

**Advanced input colour** contains the original calibration controls:

- **Input profile**: how the camera's colours are interpreted. The standard matrix is right for nearly every file.
- **Rendering intent** and **Gamut clipping**: how out-of-range colours are handled on the way in.
- **Working profile**: the colour space the edit runs in; linear Rec.2020 is the default.

### Black and white

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

### LUT

The LUT runs after Tone or Film tone, and before a print stock. With normal tone mapping, choose a LUT intended for a rendered picture and set the colour space its maker specifies. Choosing a log colour space converts the LUT's input into that encoding; it does not undo tone mapping. With a print stock on, the regular tone mapper is parked, so the LUT feeds the print and the same LUT can look different.

- **File**: a .cube, .png, .3dl or .gmz look-up table.
- **Colour space**: the space the LUT expects its input in; the LUT's maker says which.
- **Interpolation**: tetrahedral is the usual choice.

### RGB primaries

Choose **RGB primaries** through **Customise tools**. Hue rotates each primary; purity controls how saturated it is. Tint adds a hue to the neutral axis. These controls affect the overall colour rendering.

### Negative conversion

Choose **Negative conversion** through **Customise tools** for photographed or scanned film. **Prepare negative** enables conversion and disables Tone, Film tone and the base curve. Select colour or monochrome stock, set the film-base RGB values from an unexposed border, then adjust density range and scan exposure bias. Shadow/highlight RGB corrections handle colour casts; paper black, grade, gloss and print exposure shape the positive image. Film-base values are set manually.

## Colour grading

Open **Colour & Look** for Primary correction, Print stock and LUTs. Creative profile is in the same section; grain, halation, glow and vignette are in **Effects**. Click a tool heading to expand it.

Use the colour wheels to shape the look after correcting the photo. They start at neutral and leave the picture alone until moved.

Primary correction affects the whole photo. For a colour push restricted to
skin or another area, create a **Masks → Colour Range** selection and use the
wheel under that mask's **Colour** section. The Masks help page explains how to
refine the selection and keep similar-coloured backgrounds out of it.

### Creative profile

Choose **Warm Portrait**, **Clear Landscape**, **Soft Colour**, **Cinema Dusk**, **Silver Monochrome** or **Warm Monochrome** for an independent colour look. **Amount** runs from 0 (no effect) through 100 (the designed look) to 200 (stronger). The switch bypasses it; **None** removes the selection. Exposure, white balance and film settings stay in place. Profiles work after film rendering and can be combined with presets.

**Import profile…** adds a validated 3D `.cube` table to the profile menu and keeps a copy in OmaRAW's profile library. Set **LUT colour space** to the space the table expects; sRGB is the default. Supported tables have 2–64 entries per axis and no 1D shaper. Camera-log transforms need conversion to a supported space first.

Use the tool menu to save a profile adjustment preset, or include **Creative profile** when saving a broader preset. An imported LUT is referenced by the saved edit, so transfer the LUT too when moving an edit to another machine. If it is missing, import and select it again before exporting. The same button imports self-contained enhanced `.xmp` profiles with RGB or HSV tables. Their recorded colour space and Amount limits are automatic; Amount 0 can be a nonzero minimum, and a fixed-amount profile hides that slider. Use the switch for complete bypass. OmaRAW uses its own base rendering. Profiles needing unsupported source adjustments or missing external tables are rejected with an explanation.

### Colour wheels: Lift, Gamma, Gain and Offset

The four wheels correct colour and brightness with overlapping effects:

- **Lift** sets the black level. It has most influence in darker tones and keeps white fixed when the other controls are neutral.
- **Gamma** shapes the midtones while keeping black and white fixed.
- **Gain** scales brightness towards the white end while keeping black fixed.
- **Offset** shifts the whole picture, including both black and white.

Drag the puck towards a colour to add that colour; distance from the centre sets **Chroma** (strength). The centre is neutral. **Hue** and **Chroma** can also be typed precisely. The **Level** control beneath each wheel adjusts brightness. Neutral Level is **0.000** for Lift/Offset and **1.000** for Gamma/Gain.

**Lum Mix**, beneath the wheels, controls how colour pushes affect brightness. At **100**, where it starts, a colour push also changes brightness: a strong push towards red lightens the picture as well as warming it. Lower it and the colour pushes keep the brightness the Level controls give; at **0** they change colour only. The Level controls work the same at any setting.

For a starting workflow, adjust Offset for the overall balance, Lift for the blacks, Gain for the brighter tones, then Gamma for the midtones. Make small moves and check the picture and scopes. White balance and RAW exposure are in **Light & Tone**.

#### Layout and gestures

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

## Curves

Open **Adjust ▸ Curves** for two ways to shape tone by hand. They sit at different points in the processing: the **Curve** acts on the scene's light before Tone's Contrast turns it into a picture, and **Tone regions** act on the finished picture after it.

### Curves

A tone curve over the picture's RGB. Click to add a node, drag it, right-click a node to remove it. Its scale is lightness: middle grey (18% of white) sits at the centre, 0.5 in is 0.5 out until you move it, and the right-hand end is diffuse white. Behind the curve sits its input histogram: the light as the curve receives it, before Contrast, so the nodes land on the tones they act on. Anything brighter than white (a bright sky, a lamp) gathers at the right-hand end; Contrast still brings it into the picture afterwards.

- **Shared**, or **R**, **G** and **B** apart. Splitting copies the shared curve into each channel.
- **Interpolation**: Monotone never overshoots; Catmull-Rom and Cubic spline are smoother between nodes.

### Tone regions

Four sliders that nudge the lightness curve in bands: **Highlights**, **Lights**, **Darks** and **Shadows**. Drag a row to lift or lower that band; the bands blend into each other. They work on the finished picture's lightness and leave its hues where they are.

The ellipsis on either heading copies, pastes or saves that curve as a preset.

### Lab colour curves

The curves for Lab's two colour axes are part of **Lab colour**, under Colour: open it and choose **Show curves**. They share the engine's Lab tone curve with **Tone regions**, so that heading's eye and ⋯ menu cover both; while Lab colour is in use, Tone regions act on lightness alone.

### RGB levels

Enable **RGB levels** in **Adjust ▸ Customise tools**, then open its heading. Set black point, midpoint and white point, linked for all RGB channels or separately for red, green and blue. Points stay ordered; moving a point stops before its neighbour. These settings support the same copy, paste, reset and preset actions as other adjustments.

## Detail

Judge fine detail at 100% zoom. **Detail** contains Texture, clarity & dehaze, optional Contrast & texture and Contrast equalizer, then ordinary Sharpening. **Prepare** contains AI denoise, manual Noise reduction and the RAW-only Capture sharpening switch. Chromatic aberration is in Lens & Geometry.

### Sharpening

- **Amount**: how strongly edges are emphasised. The handle rests at zero while sharpening is off; double-click returns its amount to zero.
- **Radius** (px): how wide the halo is. Under 1 px for fine detail.

### Capture sharpening

Every lens and sensor softens the picture slightly. **Capture sharpening** undoes that at the very start, before any other edit, with a strength it measures from the photo itself, so there is nothing to set. Leave it on for most pictures and judge it at 100%.

The raw conversion itself (turning the sensor's mosaic of red, green and blue into pixels) is not a choice here: OmaRAW picks the best method for the sensor. Tested side by side at 100% on Fujifilm X-Trans and ordinary (Bayer) files, with and without noise reduction, the ones it uses gave the least false colour along fine edges and the calmest skies, for the same detail.

### AI denoise

The models and tools are included and work offline. Start around **60% Strength**.
**Preview denoise** shows a selected area with your current colour and exposure
adjustments, including Auto. **Fit** centres the whole area; **100%** lets you
judge fine texture at one image pixel per screen pixel. Drag directly on the
preview to move through the photo; it refreshes automatically when you stop.
Scroll to zoom under the pointer, or double-click to switch Fit / 100%. Ctrl+scroll
steps through zoom presets. Before / After keeps the same position and zoom. Crop, masks and other detail effects are
excluded from this comparison. Lower Strength
retains more grain. **Use GPU when faster** checks Vulkan against CPU and falls
back automatically; untick it to force CPU.

**Apply AI denoise** creates a new, full-size DNG beside the original, keeps your
current edits, stacks the pair and opens the copy for continued editing. An
unused filename is chosen automatically. **Save copy as…** lets you choose its
name and folder. The original stays untouched; edits to the copy are independent.
White balance, colour, crop and masks remain editable. Demosaicing and chosen
denoise strength are baked in; return to the original to change those. Float32 DNGs are large (about 12 MB per megapixel).

Bayer and X-Trans RAW photographs are detected automatically on preview or save.
X-Trans uses Restormer; Bayer uses RawForge Heavy. The detected sensor appears
in the panel. X-Trans does not need an ISO override. Restormer can take several
minutes on a GPU or much longer on CPU; you can cancel while it works.
Already-developed linear DNGs and DNGs with unsupported calibration maps are refused. Photos stay
on this computer. If a Bayer photo’s ISO metadata is missing, enter the capture ISO in the
override box; otherwise leave it blank. You can cancel during processing.

### Noise reduction

The standard Noise reduction tool adjusts the current develop recipe. For supported cameras it uses noise measurements matched to the capture ISO; otherwise it uses a generic profile, so the same amount may have a different effect. Turn it on with its status dot, or just move a slider. A denoised DNG usually needs little or none of this additional smoothing.

- **Everything** reduces brightness grain and coloured blotches together. It compares small patches across the picture and averages the ones that match.
- **Colour only** takes out the coloured blotches and leaves the grain, for a filmic look.
- **Amount**: rests at zero while noise reduction is off. Zero or a slider reset switches it off without discarding the saved amount. Start at 50, then adjust at 100% zoom. Lower values leave more noise; higher values can soften texture. The two modes use different methods, so equal amounts do not promise identical colour smoothing.
- **Keep fine detail** (Everything only): preserves texture such as skin, hair and fabric by reducing grain smoothing. Start at 0 to assess noise removal, then raise it as needed. High values leave more grain and can make Everything look similar to Colour only.
- **Remove hot pixels**: replaces single bright pixels stuck on, common in long exposures.

Judge it at 100%: the fitted view hides both the noise and the smoothing.

### Clarity

Local contrast in the midtones: + for punch, − for a softer, dreamier look.

### Texture

Fine detail: + brings it out, − smooths it (skin). Slow on big files.

### Chromatic aberration

Corrects the colour fringes of the raw data. **Avoid colour shift** keeps the overall colour balance while doing so.

### More tools

- **Contrast & texture** enhances or softens a chosen detail size. Amount 0% is neutral. Edge protection limits halos; noise protection limits amplification of shadow noise. More filter passes cost more processing time.
- **Contrast equalizer** adjusts luminance contrast, colour contrast and edges at six detail sizes. Select a channel, then shape its bands. 0.5 is neutral. (Noise reduction lives in its own block.)

## Colour grading and finishing effects

Print stock and LUTs are in **Colour & Look**, alongside Primary correction. Grain, Halation, Glow and Vignette are in **Effects**. Each tool has its own heading. Grain, Halation and Vignette show all their controls directly; Print stock keeps its larger set of fine controls under Advanced.

A film look and finishing effects: print stock, grain, halation, glow and vignette. These effects feed the print stock when it is on. Each starts from nothing and leaves the picture alone until moved. Judge grain and halation at 100% zoom.

For a starting recipe, open **Presets** and choose **Film** or **Cinematic** from the category dropdown. The **Black & white** collection also includes a Cinema Mono 2302 Silver Print recipe. Each recipe sets a print and/or tonal look with matching finishing effects. To save your own film recipe, use **Presets → + → None → Film**; to save just the print, use **Print stock → ⋯ → Save adjustment preset**.

### Print stock

The film simulations are independently developed by OmaRAW, based on published manufacturer technical data. No manufacturer affiliation or endorsement is claimed. Source publications, modelling approximations and credits accompany the application.

Start with **Film stock**, then set **Strength**. When Print stock is off, the dropdown reads **Select stock…**. Each choice includes a short description of its colour and contrast. Choose a stock to apply it; the dropdown then shows that stock's name. Use the status dot beside **Print stock** to compare with the effect off, keeping your settings. At 0% strength the effect is invisible; use 100% to judge the stock itself. The **Film** and **Cinematic** presets add finishing effects for a more styled starting point.

Cinema stocks create a finished digital film look. RAW colour is converted to the look's Cineon input, then the stock supplies its colour, contrast and highlight response for the display. There is no extra camera-negative simulation and no physical printing is required. The still films are the photograph shot on that film and printed on photographic paper. The model uses published characteristic curves and available spectral data, with documented approximations where measurements are missing. It gives each stock its own tone and colour response; it is not a measured match to a complete physical film workflow or a finished movie grade.

- **Film stock**: cinema looks first, then still-film looks. **Cinema 2383** has dense blacks and a smooth highlight shoulder; **Cinema 2393** has deeper blacks and stronger contrast. **Cinema 3510**, **3513DI**, **3521XD** and **3523XD** run cooler, from softer to crisper. **Cinema CP30** has cooler magentas and greens. **Cinema Mono 2302** applies monochrome contrast to scene luminance. Current cinema choices use the digital film workflow; older versions remain available on photos already using them.
- **Still films**: **Portrait 160**, **400** and **800** give soft contrast and gentle colour. **Fine Grain 100** is clean and vivid, **Warm 200** adds warmth, and **Vivid 400** balances lively colour with visible grain. These are OmaRAW interpretations with documented spectral approximations, not exact physical-film reproductions. Source publications and data credits accompany the application.
- **Strength**: how much of the print shows. Below 100 it blends with the complete plain rendering, using the tone mapper that was on before the print. At 0 it matches switching the print off, including all colour tools and effects.
- **Colour balance**: move **Red ↔ Cyan**, **Green ↔ Magenta** or **Blue ↔ Yellow** left or right towards the named colour. Centre keeps the stock's own balance. These adjust the film look's colour balance, with names that describe their visible result.
- **Viewing warmth** (Advanced → Viewing): **Neutral (D65)** keeps the display white; **Slightly warm (D60)**, **Warm (D55)** and **Warmest (D50)** progressively warm the whole print, like changing the viewing light.
- **White rendering** (Advanced → Viewing, still films): cinema looks include their own display rendering, so this control is hidden for them. For still films, **Balanced print**, the default for new edits, keeps the film-base white reference and corrects unwanted colour drift along the model's neutral grey scale. It retains the stock's luminance curve and headroom above a white card. **Bright whites (legacy)** boosts the whole print and can clip highlights; **Film-base whites (legacy)** retains the earlier unbalanced response. Saved edits keep their previous rendering. Click **Use balanced print** below Strength to update one; Undo restores it. This does not change white balance, exposure, stock, strength or printer-light settings.
- **Channel contrast** (Advanced): **Red contrast**, **Green contrast** and **Blue contrast** refine each colour channel's contrast by scaling its printing density. They can also shift colour; 0 keeps the stock's own response.
- **Reset refinements** (Advanced) restores Colour balance, Print whites and Channel contrast in one undo step, keeping the stock, Strength and any imported camera profile. The heading's **⋯ → Reset** still resets the whole Print stock tool.
- **Imported camera profile** (Advanced) appears when a camera profile is selected or attached by a preset. Choose or import one through **Prepare → Camera profile**. It shares this tool's stock slot, so choosing a film stock replaces it. Keep its tables file to preserve the look. You do not need a profile file to use the built-in stocks.
- With a print on, the colour tools and effects (wheels, tone regions, grain, halation, glow, vignette) work on the scene before it is printed, before the film look: the print's toe and shoulder then shape their result. The same setting can therefore look stronger or softer than it does without a print; judge it with the print on.
- Choosing a stock switches the tone mapper off for the photo, in the same history step: the print is the rendering. **Light → Contrast** adjusts contrast while keeping the stock. Choosing or editing **Tone mapping** or **Film tone** replaces the print. Turning the print off puts back the tone mapper that was on before it, and leaves one you had switched off, off.

### Grain

- **Amount**: film-like grain, applied before a print stock.
- **Size**: how coarse the grain is. Fine suits 35 mm; coarse, 8 mm.
- **Mid-tone bias**: keeps grain out of the darkest and brightest parts as it rises.

### Glow

- **Strength**, **Size**, **Threshold**: a soft bloom around the brightest areas. Threshold sets how bright something must be to glow.

Strength rests at zero while Glow is off. Setting it to zero or resetting it switches Glow off and keeps the saved recipe for the effect switch. Size has its own default of 20; it controls the spread, not whether glow is applied.

### Halation

On film, light passes the emulsion, bounces off the base and exposes it again from behind, so a bright area throws a red-to-orange glow around itself, strongest at hard bright edges.

- **Amount**: how strong the halo is; 0 leaves the picture alone.
- **Scatter**: how far the halo spreads from the bright edge.
- **Dye transmission**: the halo's colour, from red towards orange.
- **Boost**: how saturated the halo is; 0 is a white glow.
- **Threshold**: how bright a part must be before it halates.

### Vignette

The vignette follows the frame's shape and the crop: it is centred on what you keep, and the four corners always match.

- **Amount**: darkens (−) or lightens (+) the corners. Lightening scales the light, so blacks stay black; −100 takes the very corners to black.
- **Midpoint**: how far from the centre the fade begins.
- **Feather**: how gradually it fades in; the fade is always smooth, with no edge.
- **Corner saturation**: takes colour out of the corners (−) or adds it (+). It starts at 0, so Amount alone keeps the corners' colour.
- **Shape**: at 1 an oval fitted to the frame; lower is squarer, higher more pointed, towards a diamond.

## Lens & composition

The **Lens & Geometry** section holds lens correction, transform, defringe, chromatic aberration and orientation. The Crop toolbar has **Precise crop edges** for numeric boundaries.

### Lens profile

At the top: what the lens database matched for the camera body and the lens the file names, each marked when it is not in the database. Type part of the maker or lens name to filter the lenses listed for the body's mount, for example **Canon 28**. Typing only searches. Click a result to apply it, or press Enter to choose the first match. Clear the search or press Escape to cancel it. The arrow beside the heading goes back to the file's own lens.

### Lens corrections

- **Correction data**: Camera data uses what the maker wrote into the file; Lens database uses the matched profile.
- **Corrections**: which of distortion, vignetting and chromatic aberration to apply.
- **Scale**: zooms in to hide the edges that distortion correction pulls into view.

### Orientation

Set the whole picture's orientation. Each choice appears once: **As shot**,
**Unrotated**, **Rotate 90° left**, **Rotate 90° right**, **Rotate 180°**,
**Flip horizontally**, **Flip vertically**, and the two combined rotate-and-flip
choices.

**As shot** follows the camera's orientation tag. **Unrotated** uses the file's
original pixel order. The other choices also start from that original order;
choosing a rotation again does not add another turn. The combined choices rotate
90° right, then flip in the displayed direction. For small angle corrections,
use **Crop & straighten**.

### Defringe

Removes purple and green fringing at high-contrast edges. **Method**, **Radius** and **Threshold** control how it finds the fringes.

The separate **Chromatic aberration** tool works on Bayer RAW sensor data only.
It has no effect on X-Trans RAWs, linear DNGs or JPEG/PNG/TIFF images. For those
images, use **Lens corrections** with a matching profile, or **Defringe**.

### Transform

- **Vertical perspective** and **Horizontal perspective**: manual correction for converging lines, such as buildings photographed from below.

**Crop & straighten** opens the single set of rotation, automatic/guided correction and crop-edge controls. **Straighten** and **Crop edges** live there, rather than being repeated in Transform.

### Crop

**Left**, **Top**, **Right**, **Bottom** as fractions of the frame. The crop tool (R) is the easier way: drag the handles or edges, pick a ratio, or straighten the photo. The **Straighten** slider covers −10° to 10° for small corrections. Hold Shift while dragging for finer control, or click the angle to type a value up to ±45°. Double-click the track to reset rotation. The composition grid offers thirds, golden, diagonals or none. Escape closes the tool.

With a ratio locked, dragging an edge keeps the crop centred on the other
axis. A corner responds to horizontal and vertical movement, and the ratio
stays locked at the photo boundaries. Each completed drag is one undo step;
closing the tool during a drag cancels that unfinished movement.
**Original** follows the photo's selected orientation. Choosing a ratio or
swapping landscape and portrait fits the frame in one undoable step.

### Automatic geometry and guides

Open **Crop and straighten** (R):

- **Auto level** finds clear horizontal and vertical edges and levels the photo. If it cannot find reliable lines, the photo stays unchanged and **Draw level line** switches on automatically.
- **Draw level line**: click the button, then drag along a horizon or an edge that should be horizontal or vertical. Release to straighten. Only one line is needed. Click the button again to cancel.
- **Auto perspective** offers Vertical, Horizontal and Full. Use Vertical for leaning buildings; Full needs clear lines in both directions.
- **Draw perspective guides**: click the button, then drag along two edges that should be vertical or two that should be horizontal. Draw four guides to correct both directions, then choose **Apply guides**. This mode also opens when Auto perspective cannot find a correction. Clear guides starts again; Cancel guides leaves the photo unchanged. Resizing the window preserves completed guides.
- **Crop edges** trims the empty corners produced by rotation and perspective. Largest keeps the largest usable area; Original keeps the original aspect ratio. Off shows the full transformed frame. The policy also follows manual rotation and perspective changes.
- **Reset geometry** resets rotation and perspective while preserving the crop frame and other adjustments. **Reset crop frame** restores the whole frame and Free aspect ratio while keeping geometry.

Corrections appear in the preview and each is one undoable history step. Automatic correction needs clear straight edges; portraits and organic scenes may need **Draw level line** or manual rotation instead.

## Masks

Adjust part of the photo in **Develop → Masks**. Each mask has six folding
sections: **Colour range**, **Colour**, **Tone**, **Detail**, **Shapes & overlay**
and **Edge refinement**. One section opens at a time; all existing controls
remain available.

### Select a colour, such as skin

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


### AI object masks

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

### Making one

Press **Radial** or **Gradient**, then drag the shape into place on the picture. **Pen** lets you draw your own closed shape. Set its **Exposure** and **Black**, then **Contrast**, **Highlights**, **Shadows**, **Saturation**, **Vibrance**, **Clarity**, **Sharpness** and **Colour moiré radius**. The whole stack acts under the one mask. Give the local a name with the pencil; History follows the name. The row's switch takes the whole local off; the bin removes it.

Drag the centre handle to move a radial, gradient or brush shape. Its outline follows the mouse; the mask and photo update when you release. Each drag is one undo step. Clicking a handle selects it without changing the photo.

### Pen masks

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

### The mask

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

### Copy and paste

The copy button under Shapes & overlay (Ctrl+Shift+C) takes the active local, shapes, ranges, switches and sliders; paste (Ctrl+Shift+V) lays it onto whatever photo is open as a new local. Shapes are placed as fractions of the frame, so check their positions on a photo of a different shape.

### External masks

Under **External mask**, select the adjustment to limit and press **Import**. White selects, black protects, and grey gives partial selection. Transparent pixels protect the image. Use a mask aligned to the uncropped, unrotated source; it follows later lens, rotation and crop changes.

A managed greyscale PNG copy is saved with the catalogue, with a maximum edge of 4096 pixels. The same mask can control exposure, tone equalizer, colour grading, contrast & texture, clarity, sharpening, contrast equalizer or RGB primaries. **Use mask** connects another adjustment, **Invert** swaps the selection and **Disconnect** restores that adjustment's global effect. Importing a replacement changes the shared mask for every connected adjustment.

Snapshots and presets retain the connections and managed file path. Preset JSON does not embed the mask file: copy the library's masks folder with backups, and make the referenced file available when sharing a preset.

Catalogue backups include imported masks. Keep the catalogue’s `masks` folder if you move it manually. Mask presets retain the file reference and connections; they do not embed the image.

## Retouch

Remove blemishes, sensor dust and small distractions.

### AI object removal

Open **Prepare**, or use the AI object removal shortcut here.

Choose **AI object removal**. Its models and tools are included and work offline. Click
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

### Heal, clone, blur and fill

1. Pick a tool: **Heal** blends the source's texture into the spot's surroundings; **Clone** copies the source as it is; **Blur** softens the spot; **Fill** paints it with a brightness.
2. Set a **Size** and a **Feather**.
3. Press **Place spots** and click a blemish.

Heal and clone read their pixels from a source circle that starts beside the spot; drag either circle on the picture. Every spot stays in the list, editable: tool, size, feather, opacity, and blur radius or fill brightness. Spots ride in snapshots and appear in History under Retouch.

You can leave **Place spots** enabled while moving an existing spot or its source. Click elsewhere to add another spot. Each drag updates the outline immediately and applies one undoable edit on release. Use the zoom strip or Ctrl + wheel while these tools are open.

### Brush and polygon shapes

Choose Circle, Brush stroke or Polygon path above the tool buttons. Brush strokes follow a drag. For polygons, click corners and double-click to close; right-click cancels the unfinished polygon. Heal and clone have a draggable source anchor. Dragging a shape or source shows its outline immediately and applies the edit on release. Shapes, source positions, algorithms, opacity and layers are retained in snapshots and Retouch presets.

Switching away from Retouch or changing photos discards an unfinished stroke
or polygon. The guides remain responsive at high zoom.

### Frequency separation

Set **Frequency layers** above zero, then choose which layer receives new shapes. Detail layer 1 contains the finest texture; higher layers hold broader detail. **Residual tone** holds the broad colour and lighting. Each shape can be assigned to a different layer in its settings. Whole image edits affect the combined image.

**Preview selected layer** shows the selected layer at fitted-preview resolution. The finest layers may be invisible at that scale. Toggle it off to see the complete image. This diagnostic preview never changes exports.

### Keeping the original after AI

**Apply removal** adds an editable repair to the current photo. It keeps your
adjustments and prepares the next selection. No new photo is needed.

**Save rendered DNG…** creates a separate version with the displayed appearance
baked in. **Photo versions** opens that copy or returns to its original. Keep
using the original to revise editable repairs and grading. Nothing is deleted
when you switch. If using both AI tools, apply denoise before object removal.

## Capture

Shoot tethered over USB.

- **Cameras**: the page lists what is connected; click one to connect. If another program has the camera (a file manager that mounted it), the page says so.
- **Session**: a folder and a name. Files are named from a template with {session} {seq} {seq:N} {date} {time} {model}, keeping the camera's own extension. Existing names are never overwritten.
- **Shutter** (the button, or Space): shoots and downloads every file the camera makes; RAW+JPEG gives two. With the switch on, shots taken on the body are pulled in as well. Each file goes into the catalog at once, becomes current and shows in the filmstrip.
- **Live view**: the camera's preview stream with its size and rate.
- **Camera controls**: every setting the camera exposes, exposure first, as menus, switches, text or sliders. A rejected value reloads the camera's actual one.

Downloads are written to temporary files, flushed and checksummed from disk before they appear. Optionally delete each camera copy only after its verified file is safe.

## Output

Export the selected photos, or everything shown, through the engine. The render is the one you saw in Develop.

### Format and size

- **Formats**: JPEG, WebP, AVIF and JPEG XL with a quality; TIFF and PNG at 8 or 16 bit; PSD, a flat Photoshop document at 8 or 16 bit with the profile and metadata embedded, for a layered editor.
- **JPEG presets**: Web (2048 px, quality 85), Full size (quality 92), Print 4000 px (quality 95, 300 ppi), Proof (1200 px, quality 80), and Print full size (quality 95, 300 ppi). All use sRGB. The full-size print option keeps all developed pixels instead of limiting the long edge to 4000 px.
- **Editing master**: full-size 16-bit TIFF in linear ProPhoto RGB, tagged at 300 ppi, for further editing in a colour-managed editor.
- Presets set format, depth, colour, size, quality, suffix and print resolution, with output sharpening off. Choose screen/print sharpening under Finishing when needed. Your destination, metadata choices and watermark remain as set. Changing a preset's settings shows **Custom export settings**.
- Resize by long edge, short edge, width, height, megapixels or percent. Nothing is ever upsized.
- **Colour**: sRGB, RGB (1998), linear ProPhoto RGB or a custom RGB ICC profile with its intent. The profile is embedded.

### Naming and destination

**Choose folder…** opens OmaRAW's themed browser, with shortcuts to common folders and mounted drives, a directly editable path, and **New folder**. Profile and watermark file choosers use the same controls. Preset exports add a file-name field and ask before replacing an existing file.

To create an export destination, open its parent folder, choose **New folder**, enter a name and select **Create folder**. The browser opens the new folder; select **Choose folder** to use it for export. Existing files and folders are never replaced by folder creation.

Files keep their name plus the preset's suffix, and nothing is overwritten. A **filename pattern** replaces that: `{date}-{name}{suffix}`, with {name} {seq} {date} {time} {camera} {rating} {folder} {tag} and {suffix}; the row beneath shows the first file's name as it will be written. A variant exports as its own file with its name before the suffix.

### Finishing

- **Output sharpening** for screen or print, low, standard or high, sized to the export.
- **Watermark**: text and/or an image in any of nine positions with size and opacity. The preview shows where it lands.

Both act on the export's own pixels before the file is written, so every format can be finished and the file is compressed once.

### Resolution, alongside, metadata

- **Print PPI**: the print-resolution tag. **No override** (formerly shown as 0) leaves the resolution supplied by the source or encoder alone. It does not mean the photo has zero detail. Choose 150, 240, 300, 360 or 600 ppi, or **Custom** for an exact whole-number value. Changing PPI does not resize the image or change JPEG compression quality: a 6000-pixel edge tagged at 300 ppi represents 20 inches. Use the size controls to change the pixel count.
- Format, depth, size, quality, suffix, PPI, colour and sharpening are remembered when you reopen OmaRAW. Named templates below retain the broader workflow settings too.
- **Alongside**: copy the original file and its sidecar next to each export.
- **Metadata**: camera EXIF, location, keywords and the develop history are each a switch. Location is off by default.

### The queue

Every file with its state and a progress bar; Pause, Resume, Retry failed, Cancel remaining and Clear. The queue survives a restart. Hover a row for Reveal in file manager, Export again and Remove.

### Templates

Save named export and print templates in the left sidebar; a template remembers destination, naming, colour, metadata, finishing and page settings.

## Print and contact sheets

Both make PDFs in the destination folder, from the same Output page.

### Contact sheet

The source photos as a captioned grid: columns × rows, paper, orientation, an optional title. Built from the Library thumbnails, edits included.

### Print layout

Every photo is rendered at 300 dpi for the chosen paper, then placed one per page inside 10 mm margins, the page turning to suit each photo.

- **Paper profile**: an RGB ICC with an intent and black point compensation converts the PDF pages into the paper's colour. When printing that PDF, avoid applying the same conversion again. Without a paper profile, pages stay sRGB.
- **Print PDF, one per page**: writes the finished layout to the destination folder. Open the PDF in another application to print it, or send it to your lab.

### Soft proof

In Develop's left dock: pick the printer's or lab's profile and the viewer shows the render as it would print, with an optional magenta warning for colours the paper cannot hold.

## Preferences and colour

Edit ▸ Preferences… has four pages. Settings save as you change them; Close returns to your photos. Each page scrolls independently of the page buttons and Close button.

- **Appearance**: Dark or Light interface, Colour Critical mode, high contrast, reduced motion, viewer background, colour management, keyboard shortcuts and panel layout.
- **Library**: startup, daily backups, automatic advance, colour label names and metadata writing. Leave **Choose a library at startup** off to reopen the last library.
- **Performance**: automatic or custom memory allocation, a shared disk cache size, usage and free-space readouts, and cache cleanup. Changes are remembered between launches.
- **Processing**: the first edit for new RAW photos, imported camera profiles, preview speed and export quality.
- **Light interface**: light panels and dark text, from View ▸ Light Interface or Preferences ▸ Appearance. It is remembered. The photograph keeps a neutral dark surround, so the picture does not change with the interface. Colour Critical overrides Light until Colour Critical is turned off.
- **Colour Critical**: a single switch for a neutral dark-grey interface. The canvas surround is `#262626`, the main interface `#2D2D2D`, panels `#353535`, raised controls `#404040`, input fields `#202020`, hover states `#454545`, text `#D6D6D6`, muted text `#909090`, and dividers `#484848`. Decorative accents become grey, including selections and focus indicators. High contrast also stays neutral. The switch takes effect immediately and is remembered between launches. Turning it off restores Dark or Light, and your chosen viewer background.
- **Photos and colour tools**: Colour Critical changes the interface only. Photo previews, edits, exports, RGB histograms, colour wheels and photo-label swatches keep their colours. The mode is separate from colour management and monitor calibration.
- **Desktop theme**: in Dark and Light, the accent (buttons, selection, focus) follows your Omarchy theme and changes with it while OmaRAW is open. The remaining interface colours stay fixed for that appearance. Without Omarchy, the accent is OmaRAW's own blue. Colour Critical uses neutral accents regardless of the desktop theme.
- **Keyboard**: every shortcut, editable.
- **Processing → Speed and quality**: **Faster fitted previews** (on by default) speeds up the fitted view and uses a smaller live preview while you drag a control. The normal fitted resolution and full zoomed detail return on release; exports keep full quality. **Process exports at full resolution** runs every adjustment at the source size and downsizes last, slower on large files. GPU processing can be switched off; every job has a CPU fallback.

If GPU processing runs out of graphics resources, the affected processing path
switches to CPU for the session; its adjustments still apply. If the interface
also fails, OmaRAW releases the catalog and attempts to reopen with CPU
processing and software rendering. The status message explains the recovery.

### Memory and cache

**Automatic memory allocation** uses a quarter of detected RAM, up to 8 GiB. Turn it off to type or drag a custom **Memory budget**, from 1 GiB up to three quarters of system RAM (at most 64 GiB). This is a processing and reusable-image budget, not a hard limit on the entire process. Large images, non-tileable processing, graphics drivers and AI workers can need additional memory. Changes apply between engine jobs without restarting.

**Maximum cache size** controls 1–1024 GiB shared by disposable browsing previews and temporary image-processing buffers, with a 4 GiB default. Large engine buffers stay in RAM while there is room. When memory is under pressure, or an upcoming effect needs more working space, the engine gives those buffers disk backing at a safe processing boundary. Their active pages can stay in RAM, while the system can reclaim them when needed. Pixels retain their full precision. Working files reserve disk space before processing and are released with their buffers; abandoned files are reclaimed after a restart. Active working data is included in the usage readout. GPU memory, AI workers and other temporary allocations have separate limits, so this is not a hard cap on all application memory.

Space is used as needed. Background preview cleanup runs on startup, after changing the limit, and every 30 seconds, allowing a busy import to temporarily exceed the target. New working buffers must fit within the remaining cache allowance and leave free disk space. If neither RAM nor disk can accommodate a buffer, processing reports a failure instead of ignoring the limits. Lowering the cache limit does not remove working data that is still in use.

**Clear preview cache** removes disposable sizes only. Edited preview masters, photos, catalogues, editing history, Smart Previews and offline originals are preserved. New size previews are rebuilt from edited masters where available, including when an original is offline. Older installations' preview files are migrated into the managed cache as they are accessed; untouched legacy files remain outside this quota. **Open cache folder** shows the actual location for the current library.

### Colour management

View ▸ Colour Management… selects the OCIO configuration, display, view and preview exposure. The edit runs in 32-bit float, linear Rec.2020. Viewing transforms never change edits or exports.

The same dialog controls monitor calibration: on X11 the active colord profile is used; on Wayland the desktop manages the monitor; a manual RGB ICC is available for unmanaged displays. Physical monitor and printer verification is still yours to do.


Label names, the camera profiles folder, colour management and automatic metadata-writing policies are available here. The View menu keeps the Light interface and Colour Critical switches, and the panel visibility controls. All these settings remain searchable with Ctrl+K.
