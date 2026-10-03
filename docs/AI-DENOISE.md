# Local AI denoise

Open **Prepare → AI denoise** in Develop. The runtime and both sensor models
are included in the package, ready for offline use. Denoising runs on your computer;
photographs are not uploaded and no online account is needed.

## Preview and apply

Choose the strength and preview a detail area before applying it. Drag the
preview to another part of the photo, use the wheel to zoom, and compare Before
and After at the same position. The preview includes global colour and exposure
settings, but crop, local masks, retouch and other detail effects are excluded.
Update the comparison after changing colour edits.

**Apply AI denoise** creates an unused `-denoised.dng` file beside the original,
adds it to the catalog, stacks it with the original and opens it for editing.
**Save copy as…** lets you choose a different filename. Existing files are not
overwritten.

The original stays intact. Current exposure, colour, profiles, crop, masks and
retouch transfer to the new copy as editable settings. Demosaicing and denoise
strength are baked into its pixels; make another copy from the original to
change those. Sensor-only settings remain with the original.

## Support and performance

- Supported Bayer and X-Trans RAWs use the appropriate model automatically.
  The sensor layout determines the model, rather than the camera brand.
- Linear or already-demosaiced RAW, monochrome sensors and some calibrated DNGs
  are unsupported. A file that opens in Develop may still be unsuitable for
  AI denoise.
- CPU processing works without a GPU. Automatic GPU mode uses Vulkan after
  checking accuracy and speed, with CPU fallback where possible.
- Full-resolution processing can take substantial time and memory. X-Trans CPU
  processing can take hours on large photographs. Try a detail preview first.
- The output is a full-size float DNG and may be much larger than the original.
  Keep enough free space for the result and temporary processing.
- Cancel stops remaining processing. Reinstall the OmaRAW package to restore
  a damaged or missing runtime or model; the app does not download replacements.

## Photo versions

**Photo versions** above the image keeps the original, intermediate results and
latest AI result together. Choose **Open original** or **Open latest AI version**
to move between them; each retains its own edits. Apply denoise before object
removal for the normal RAW workflow.

## Sources and licences

The included Bayer TreeNet and X-Trans Restormer models come from
[RawForge's ONNX release](https://github.com/rymuelle/RawForge/releases/tag/onnx_v1.0.0),
distributed by the MIT-licensed upstream project. The package build checks pinned
hashes and converts the models to float32 before installation. Original and
converted hashes, conversion details and the upstream project licence are
recorded in the [third-party notices](../THIRD_PARTY.md) and model manifest.

RawForge and RawHandler code is MIT-licensed. The adapted Malvar filters come
from colour-demosaicing under BSD-3-Clause. Full attribution and licence text
are in [the denoise notices](../src/denoise/LICENSES.txt), installed with OmaRAW
as `AI-DENOISE.txt`. The processing implementation and model manifest are
included in the application's source.

See [AI tools](AI-TOOLS.md) for masks and object removal, or the
[command-line guide](cli.md) to use denoising through an agent.
