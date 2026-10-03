# Detail

Judge fine detail at 100% zoom. **Detail** contains Texture, clarity & dehaze, optional Contrast & texture and Contrast equalizer, then ordinary Sharpening. **Prepare** contains AI denoise, manual Noise reduction and the RAW-only Capture sharpening switch. Chromatic aberration is in Lens & Geometry.

## Sharpening

- **Amount**: how strongly edges are emphasised. The handle rests at zero while sharpening is off; double-click returns its amount to zero.
- **Radius** (px): how wide the halo is. Under 1 px for fine detail.

## Capture sharpening

Every lens and sensor softens the picture slightly. **Capture sharpening** undoes that at the very start, before any other edit, with a strength it measures from the photo itself, so there is nothing to set. Leave it on for most pictures and judge it at 100%.

The raw conversion itself (turning the sensor's mosaic of red, green and blue into pixels) is not a choice here: OmaRAW picks the best method for the sensor. Tested side by side at 100% on Fujifilm X-Trans and ordinary (Bayer) files, with and without noise reduction, the ones it uses gave the least false colour along fine edges and the calmest skies, for the same detail.

## AI denoise

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

## Noise reduction

The existing Noise reduction tool adjusts the current develop recipe. It knows the noise your camera makes at the ISO you shot at (for the hundreds of cameras the engine has measured), so the starting point is already matched to the photo. Turn it on with its status dot, or just move a slider. A denoised DNG usually needs little or none of this additional smoothing.

- **Everything** cleans the fine grain and the coloured blotches together. It compares small patches across the picture and averages the ones that match, the cleanest method the engine has.
- **Colour only** takes out the coloured blotches and leaves the grain, for a filmic look.
- **Amount**: rests at zero while noise reduction is off. Zero or a slider reset switches it off without discarding the saved amount. When enabled, 50 is what the camera needs at this ISO. Lower leaves more noise; much past 70 and skin starts to look waxy.
- **Keep fine detail** (Everything only): brings back texture such as skin, hair and fabric that the smoothing took, at the cost of a little grain. Around 30 is a good start on portraits.
- **Remove hot pixels**: replaces single bright pixels stuck on, common in long exposures.

Judge it at 100%: the fitted view hides both the noise and the smoothing.

## Clarity

Local contrast in the midtones: + for punch, − for a softer, dreamier look.

## Texture

Fine detail: + brings it out, − smooths it (skin). Slow on big files.

## Chromatic aberration

Corrects the colour fringes of the raw data. **Avoid colour shift** keeps the overall colour balance while doing so.

## More tools

- **Contrast & texture** enhances or softens a chosen detail size. Amount 0% is neutral. Edge protection limits halos; noise protection limits amplification of shadow noise. More filter passes cost more processing time.
- **Contrast equalizer** adjusts luminance contrast, colour contrast and edges at six detail sizes. Select a channel, then shape its bands. 0.5 is neutral. (Noise reduction lives in its own block.)
