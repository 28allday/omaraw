# Organising

## Albums

Select one or more photos, right-click a selected photo and choose **Add to Album**,
then choose the album you created. The same command is under **Library ▸ Albums**.
Nested destinations show their parent names, such as **Travel / Italy**.
**New Album from Selection…** creates an album containing the selected photos.
One photo can belong to several albums; adding it does not move or copy its file.

Right-click an album to add or remove the selection, rename or delete it, or make a New Album Inside it. Albums nest; the sidebar folds each branch. With an album as the source, Sort offers Custom Order: drag a card where it belongs, and Set as Album Cover puts a photo on the album's row.

## Smart albums

Press + beside the heading and build rules: stars, flag, label, keyword, file name, camera, lens, format, edited or not, a date range, stack tops or **Auto tag**, matching all or any rule. Auto tag can match a detected subject or a subject group. The count is live. Right-click to edit or delete.

## Quick Collection

B adds the current photo or selection to the Quick Collection, a scratch set that survives until you clear it.

## Files on disk

- **Rename File…** and **Move Selection to Folder…** (right-click a card, or File) act on the disk. The sidecar, variants, edits and thumbnails follow, and nothing is overwritten. Moves are journaled, so a crash half way is settled on the next start.
- **Move to Trash** (Shift+Del) sends the files and their sidecars to the system trash. **Remove from Catalog** (Del) keeps the files where they are.
- **Offline volumes**: folders whose drive is unplugged are greyed and their photos collect under Offline; everything comes back with the drive. Right-click a moved folder ▸ Relink Folder… to point the catalog at its new place.
- **Smart Previews** (Library ▸ Smart Previews) save smaller editable sources beside the catalog. Build them before disconnecting a drive, or enable building after import. The inspector shows their size. Develop switches back when the original returns. Export needs the original or a full offline copy. Discard previews when a matching full source is available; edits stay saved.
- **Offline originals** (Library ▸ Offline Originals) keep verified full-quality working copies for the selection, so you can develop and export with the drive disconnected.
- **Relink**: an offline photo's inspector offers Locate… and Search a folder…, which finds every offline photo under a folder by name, size and capture time.

## Catalogs

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
