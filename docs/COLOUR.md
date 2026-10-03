# Colour management

OmaRAW develops photographs without overwriting their original pixels. Camera
colour, editing, viewing and export each have a role in the finished result.

## Camera colour and editing

RAW files start with the camera calibration available for that model. Compatible
community camera profiles are included; you can also import your own. See
[camera profiles](CAMERA-PROFILES.md) for supported formats and matching.

Use white balance for the light in the scene, then the colour tools for the
look you want. Film simulations, creative profiles and LUTs can change both
colour and contrast. Imported looks may differ from the program that created
them; check their result on your own photographs.

## Viewing and proofing

The viewer is SDR. A monitor profile helps the viewer account for your display;
it does not change your saved edit. The viewer's background, preview exposure
and soft proof are viewing choices and are not baked into normal exports.

Soft proofing previews an output profile on your screen. It helps judge a print
or delivery, but the physical result also depends on the calibrated display,
paper and printer. Judge the final output in its intended conditions.

## Export

Choose the output colour space and profile in Output. For normal web delivery,
sRGB is a practical starting point. TIFF and PNG can retain 16-bit precision
when selected; choose an output appropriate to the next application or lab.
Custom ICC output supports rendering-intent choices.

Library thumbnails and contact sheets use the edited browsing previews.
Full-resolution photo exports use the processing engine. Smart Previews support
reduced-resolution work; a full-quality export needs the original or a verified
full offline copy.

For individual controls, open the [user guide](USER-GUIDE.md) or press F1 in
Develop. Profile and film-data sources are listed in the
[third-party credits](../THIRD_PARTY.md).
