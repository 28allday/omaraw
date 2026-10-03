# Welcome to OmaRAW

OmaRAW is a photography library, a RAW developer, a tethered capture desk and an output page in one window. Your photos stay where they are on disk; a **catalog** holds what OmaRAW knows about them: ratings, keywords, albums and every edit.

When you reopen a catalog, OmaRAW returns to the last selected photo or variant, with its collection, filters and sort order. The filmstrip brings that photo into view. Each catalog remembers its own place. If the photo has been removed, a remaining photo is selected; offline originals stay selected.

The launch screen shows your OmaRAW version, a looping Spectrum animation and
a photographer's quote. Click **OK** or press **Enter** to open the program.
Close the screen or press **Escape** to quit. The quote changes between launches
and works offline; click the photographer's name to visit its source. You can
pause the animation, and the Reduced motion preference uses a still image.

AI masking, object removal, RAW denoising and subject tagging include their models
and runtime libraries. They work offline after installation, without further
downloads or setup. Photos are processed on your computer.

## The workflow

1. **Browse** folders in the Library (Ctrl+I), preview the photos and tick those you want. **Import N photos** adds them in place, or copies, moves or verifies them into a destination you choose.
2. **Cull** in Library: stars, picks and rejects, colour labels and keywords, one key per frame with Auto-Advance on.
3. **Develop** the keepers: white balance, tone, colour, detail and lens on the right, **Masks** and **Retouch** above the tool menu, presets and history on the left.
4. **Output**: export photos, make a print PDF or create a contact sheet. Photo exports use the same processing as Develop; contact sheets use the edited Library thumbnails.

The four workspaces sit across the top: Ctrl+1 Library, Ctrl+2 Develop, Ctrl+3 Capture, Ctrl+4 Output. The filmstrip along the bottom follows you between them.

## Finding your way

- **Help**: press F1 anywhere for this window opened at the current workspace, or click the ? in a panel header for that panel's page.
- **Tooltips**: rest the pointer on a control, menu command or status-bar item for an explanation. Adjustment sliders say what moving them changes.
- **Command search** (Ctrl+K): type the name of any menu item and press Return. Hover a result for the same explanation as its menu entry; dimmed results are unavailable for the current selection or workspace.
- **Keyboard cheat sheet** (?): every shortcut on one sheet. Edit ▸ Preferences ▸ Keyboard changes them.

## Where things live

**Catalog location** is chosen when creating a catalog. **Photo storage** is chosen when importing: Add links to existing files; Copy or Move uses a destination you choose. The catalog records file locations and edits, while photographs remain separate files. The catalog and photos can be on different drives. Edits are kept beside the catalog. Catalog backups run daily and before every upgrade; File ▸ Catalog Maintenance ▸ Reveal Backups shows them.

## Finding commands

The menus group commands by task:

- **File:** create or open catalogs, import, export, catalog maintenance and quit.
- **Edit:** undo, redo, selection, shortcuts and Preferences.
- **Library:** albums, Quick Collection, stacks, duplicates, sidecars and offline files.
- **Photo:** ratings, flags, labels, variants, rename, move, reveal, remove and trash.
- **Adjustments:** automatic corrections, copy/paste/sync settings, masks, crop, retouch, presets, snapshots and reset.
- **View:** browsing, comparison, exposure overlays, Colour Critical, viewer background, panels and workspaces.
- **Camera:** connection, live view and capture.
- **Help:** guide, command search, shortcuts and About.

Preferences holds accessibility, automatic sidecar and backup policies, label names, camera profiles and colour management.

Press **Ctrl+K** to search for a command by name or menu group. Commands keep their existing keyboard shortcuts when their menu location changes.

## Using an agent

Installation includes OmaRAW's command-line skill and links it for your user.
Start a new agent session to use it. An agent can inspect edits, adjust photos,
organise albums and keywords, and export your chosen photographs. Close the
OmaRAW window before an agent opens its catalog. Ratings, flags and colour
labels remain choices you make in the Library.

Run `omaraw skill` to check the links, or `omaraw skill --link` to repair them
or set them up for another user. Updating the app updates the packaged skill;
existing custom skills are preserved.
