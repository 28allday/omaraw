# Preferences and colour

Edit ▸ Preferences… has four pages. Settings save as you change them; Close returns to your photos. Each page scrolls independently of the page buttons and Close button.

- **Appearance**: Colour Critical mode, high contrast, reduced motion, viewer background, colour management, keyboard shortcuts and panel layout.
- **Library**: startup, daily backups, automatic advance, colour label names and metadata writing. Leave **Choose a library at startup** off to reopen the last library.
- **Performance**: automatic or custom memory allocation, a disk preview cache size, usage and free-space readouts, and cache cleanup. Changes are remembered between launches.
- **Processing**: the first edit for new RAW photos, imported camera profiles, preview speed and export quality.
- **Colour Critical**: a single switch for a neutral dark-grey interface. The canvas surround is `#262626`, the main interface `#2D2D2D`, panels `#353535`, raised controls `#404040`, input fields `#202020`, hover states `#454545`, text `#D6D6D6`, muted text `#909090`, and dividers `#484848`. Decorative accents become grey, including selections and focus indicators. High contrast also stays neutral. The switch takes effect immediately and is remembered between launches. Turning it off restores the usual interface and your chosen viewer background.
- **Photos and colour tools**: Colour Critical changes the interface only. Photo previews, edits, exports, RGB histograms, colour wheels and photo-label swatches keep their colours. The mode is separate from colour management and monitor calibration.
- **Desktop theme**: in the usual interface, the accent (buttons, selection, focus) follows your Omarchy theme and changes with it while OmaRAW is open. The remaining interface colours stay fixed. Without Omarchy, the accent is OmaRAW's own blue. Colour Critical uses neutral accents regardless of the desktop theme.
- **Keyboard**: every shortcut, editable.
- **Processing → Speed and quality**: **Faster fitted previews** (on by default) speeds up the fitted view and uses a smaller live preview while you drag a control. The normal fitted resolution and full zoomed detail return on release; exports keep full quality. **Process exports at full resolution** runs every adjustment at the source size and downsizes last, slower on large files. GPU processing can be switched off; every job has a CPU fallback.

## Memory and cache

**Automatic memory allocation** uses a quarter of detected RAM, up to 8 GiB. Turn it off to type or drag a custom **Memory budget**, from 1 GiB up to three quarters of system RAM (at most 64 GiB). This is a processing and reusable-image budget, not a hard limit on the entire process. Large images, non-tileable processing, graphics drivers and AI workers can need additional memory. Changes apply between engine jobs without restarting.

**Maximum cache size** controls 1–1024 GiB of disposable browsing previews, with a 4 GiB default. More cached previews can make revisits faster. Space is filled only as previews are generated; the chosen amount is not preallocated. Background cleanup runs on startup, after changing the limit, and every 30 seconds, so a busy import can temporarily exceed the target.

**Clear preview cache** removes disposable sizes only. Edited preview masters, photos, catalogues, editing history, Smart Previews and offline originals are preserved. New size previews are rebuilt from edited masters where available, including when an original is offline. Older installations' preview files are migrated into the managed cache as they are accessed; untouched legacy files remain outside this quota. **Open cache folder** shows the actual location for the current library.

## Colour management

View ▸ Colour Management… selects the OCIO configuration, display, view and preview exposure. The edit runs in 32-bit float, linear Rec.2020. Viewing transforms never change edits or exports.

The same dialog controls monitor calibration: on X11 the active colord profile is used; on Wayland the desktop manages the monitor; a manual RGB ICC is available for unmanaged displays. Physical monitor and printer verification is still yours to do.


Label names, the camera profiles folder, colour management and automatic metadata-writing policies are available here. The View menu keeps the Colour Critical switch and panel visibility controls. All these settings remain searchable with Ctrl+K.
