# Capture

Shoot tethered over USB.

- **Cameras**: the page lists what is connected; click one to connect. If another program has the camera (a file manager that mounted it), the page says so.
- **Session**: a folder and a name. Files are named from a template with {session} {seq} {seq:N} {date} {time} {model}, keeping the camera's own extension. Existing names are never overwritten.
- **Shutter** (the button, or Space): shoots and downloads every file the camera makes; RAW+JPEG gives two. With the switch on, shots taken on the body are pulled in as well. Each file goes into the catalog at once, becomes current and shows in the filmstrip.
- **Live view**: the camera's preview stream with its size and rate.
- **Camera controls**: every setting the camera exposes, exposure first, as menus, switches, text or sliders. A rejected value reloads the camera's actual one.

Downloads are written to temporary files, flushed and checksummed from disk before they appear. Optionally delete each camera copy only after its verified file is safe.
