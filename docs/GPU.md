# Graphics and performance

OmaRAW works with CPU processing and can accelerate supported work through
Vulkan. A supported graphics driver can improve performance, but a dedicated
GPU is not required for basic editing.

On Arch Linux, use the driver appropriate to your graphics hardware. The
application tests available acceleration and uses it when its results match the
CPU path and it improves processing time. It falls back to CPU processing or
software interface rendering when acceleration is unavailable.

## Working with large photographs

Open **Preferences → Performance** to adjust memory use and the disk preview
cache. Automatic settings are a useful starting point. Faster fitted previews
keep slider adjustments responsive on large RAW files; zoomed detail and
full-quality exports use the original resolution when the original is available.

Disk previews are reused after restarting. Cache cleanup removes disposable
previews, not photographs or edits. Smart Previews and full offline copies have
separate purposes; see [offline originals](OFFLINE.md).

## Display and drivers

The viewer is SDR. Use your display's calibration profile where available and
review the result on the display and output you intend to use.

If the interface cannot initialise the graphics device, OmaRAW attempts a
software restart. For troubleshooting, you can start it with CPU processing
and software rendering explicitly:

```sh
OMA_GPU=cpu OMA_GPU_UI=software omaraw
```

Include the graphics card, driver and OmaRAW version when reporting a rendering
problem. Optional AI denoise has its own processing requirements; see
[AI denoise](AI-DENOISE.md).
