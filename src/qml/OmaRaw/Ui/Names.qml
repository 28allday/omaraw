pragma Singleton
import QtQuick

// The names a photographer meets. The engine keeps its own names for its
// modules, their controls and their choices; every panel asks here before
// showing one, so the words on screen are the ones other RAW developers
// taught people. Anything unlisted is shown tidied: first letter up,
// "color" spelt colour.
QtObject {
    id: names

    // engine module → heading
    readonly property var modules: ({
        "toneequal": qsTr("Tone equalizer"),
        "contrastntexture": qsTr("Contrast & texture"),
        "atrous": qsTr("Contrast equalizer"),
        "primaries": qsTr("RGB primaries"),
        "rgblevels": qsTr("RGB levels"),
        "negadoctor": qsTr("Negative conversion"),
        "liquify": qsTr("Liquify"),
        "rasterfile": qsTr("External mask"),
        "exposure": qsTr("Exposure"),
        "sigmoid": qsTr("Tone mapping"),
        "filmicrgb": qsTr("Film tone"),
        "channelmixerrgb": qsTr("White balance"),
        "temperature": qsTr("Camera white balance"),
        "omarawgrade": qsTr("Primary correction"),
        "omarawhalation": qsTr("Halation"),
        "omarawprofile": qsTr("Creative profile"),
        "omarawprint": qsTr("Print stock"),
        "colorbalancergb": qsTr("Colour grading"),
        "shadhi": qsTr("Shadows & highlights"),
        "hazeremoval": qsTr("Dehaze"),
        "highlights": qsTr("Highlight recovery"),
        "colorin": qsTr("Profile"),
        "colorout": qsTr("Output profile"),
        "monochrome": qsTr("Black and white"),
        "omarawmatch": qsTr("Image Match"),
        "lut3d": qsTr("LUT"),
        "sharpen": qsTr("Sharpening"),
        "demosaic": qsTr("Raw conversion"),
        "rawprepare": qsTr("Raw levels"),
        "denoiseprofile": qsTr("Noise reduction"),
        "bilat": qsTr("Clarity"),
        "diffuse": qsTr("Texture"),
        "hotpixels": qsTr("Hot pixels"),
        "cacorrect": qsTr("Chromatic aberration"),
        "grain": qsTr("Grain"),
        "bloom": qsTr("Glow"),
        "lens": qsTr("Lens corrections"),
        "flip": qsTr("Orientation"),
        "defringe": qsTr("Defringe"),
        "ashift": qsTr("Transform"),
        "crop": qsTr("Crop"),
        "vignette": qsTr("Vignette"),
        "rgbcurve": qsTr("Point curve"),
        "tonecurve": qsTr("Parametric curve"),
        "basecurve": qsTr("Base curve"),
        "colorzones": qsTr("HSL by colour"),
        "colorequal": qsTr("Selective colour"),
        "retouch": qsTr("Retouch"),
        "omarawrepair": qsTr("AI object removal"),
        "gamma": qsTr("Display encoding"),
        "mask_manager": qsTr("Masks")
    })

    // "module.field" → control label, where the engine-facing label in the
    // parameter table is not what a photographer expects.
    readonly property var controls: ({
        "channelmixerrgb.temperature": qsTr("Temperature"),
        "sigmoid.middle_grey_contrast": qsTr("Curve steepness"),
        "sigmoid.contrast_skewness": qsTr("Curve balance"),
        "colorbalancergb.chroma_global": qsTr("Colourfulness"),
        "colorbalancergb.brilliance_global": qsTr("Colour brightness"),
        "ashift.lensshift_v": qsTr("Vertical perspective"),
        "ashift.lensshift_h": qsTr("Horizontal perspective"),
        "filmicrgb.white_point_source": qsTr("Whites"),
        "filmicrgb.black_point_source": qsTr("Blacks"),
        "highlights.mode": qsTr("Method"),
        "highlights.clip": qsTr("Clip threshold"),
        "hazeremoval.strength": qsTr("Amount"),
        "hazeremoval.distance": qsTr("Distance"),
        "monochrome.size": qsTr("Filter size"),
        "monochrome.highlights": qsTr("Highlights"),
        "omarawprofile.amount": qsTr("Amount"),
        "omarawprofile.colorspace": qsTr("LUT colour space"),
        "lut3d.filepath": qsTr("File"),
        "lut3d.colorspace": qsTr("Colour space"),
        "lut3d.interpolation": qsTr("Interpolation"),
        "sharpen.amount": qsTr("Amount"),
        "sharpen.radius": qsTr("Radius"),
        "demosaic.demosaicing_method": qsTr("Quality"),
        "demosaic.color_smoothing": qsTr("Smooth colour speckle"),
        "demosaic.green_eq": qsTr("Remove maze pattern"),
        "demosaic.cs_enabled": qsTr("Capture sharpening"),
        "demosaic.cs_radius": qsTr("Capture sharpening radius"),
        "denoiseprofile.mode": qsTr("Method"),
        "denoiseprofile.strength": qsTr("Amount"),
        "bilat.detail": qsTr("Clarity"),
        "diffuse.texture": qsTr("Texture"),
        "hotpixels.strength": qsTr("Amount"),
        "hotpixels.threshold": qsTr("Threshold"),
        "cacorrect.avoidshift": qsTr("Avoid colour shift"),
        "grain.strength": qsTr("Amount"),
        "grain.scale": qsTr("Size"),
        "grain.midtones_bias": qsTr("Mid-tone bias"),
        "omarawprint.stock": qsTr("Film stock"),
        "omarawprint.strength": qsTr("Strength"),
        "omarawprint.white": qsTr("Viewing warmth"),
        "omarawprint.paper": qsTr("White rendering"),
        "omarawprint.trim_r": qsTr("Red ↔ Cyan"),
        "omarawprint.trim_g": qsTr("Green ↔ Magenta"),
        "omarawprint.trim_b": qsTr("Blue ↔ Yellow"),
        "omarawprint.gain_r": qsTr("Red contrast"),
        "omarawprint.gain_g": qsTr("Green contrast"),
        "omarawprint.gain_b": qsTr("Blue contrast"),
        "omarawprint.profile": qsTr("Camera profile tables"),
        "omarawhalation.strength": qsTr("Amount"),
        "omarawhalation.scatter": qsTr("Scatter"),
        "omarawhalation.dye": qsTr("Dye transmission"),
        "omarawhalation.boost": qsTr("Boost"),
        "omarawhalation.threshold": qsTr("Threshold"),
        "bloom.size": qsTr("Size"),
        "bloom.threshold": qsTr("Threshold"),
        "bloom.strength": qsTr("Strength"),
        "lens.method": qsTr("Correction data"),
        "lens.modify_flags": qsTr("Corrections"),
        "lens.scale": qsTr("Scale"),
        "defringe.op_mode": qsTr("Method"),
        "defringe.radius": qsTr("Radius"),
        "defringe.thresh": qsTr("Threshold"),
        "crop.cx": qsTr("Left"),
        "crop.cy": qsTr("Top"),
        "crop.cw": qsTr("Right"),
        "crop.ch": qsTr("Bottom"),
        "vignette.brightness": qsTr("Amount"),
        "vignette.scale": qsTr("Midpoint"),
        "vignette.falloff_scale": qsTr("Feather"),
        "vignette.saturation": qsTr("Corner saturation"),
        "vignette.shape": qsTr("Shape")
    })

    // engine choice → dropdown entry, for the entries that are jargon.
    readonly property var choices: ({
        // What the choice does first, the algorithm's own name after it for
        // the people who know it: nobody should need to know what RCD stands
        // for to pick the right entry.
        "inpaint opposed": qsTr("Rebuild from surroundings (recommended)"),
        "reconstruct in LCh": qsTr("Keep brightness, drop colour"),
        "clip highlights": qsTr("Clip to white"),
        "segmentation based": qsTr("Rebuild by region (slow)"),
        "guided laplacians": qsTr("Rebuild fine detail (slowest)"),
        "reconstruct color": qsTr("Spread nearby colour (older)"),
        "non-local means auto": qsTr("Automatic, keeps texture"),
        "non-local means": qsTr("Keeps texture · non-local means"),
        "wavelets auto": qsTr("Automatic, smoother"),
        "wavelets": qsTr("Smoother, by detail size · wavelets"),
        "compute variance": qsTr("Measure noise (diagnostic)"),
        "RCD": qsTr("Standard (recommended) · RCD"),
        "AMaZE": qsTr("Finest detail, low ISO · AMaZE"),
        "LMMSE": qsTr("Noisy high-ISO files · LMMSE"),
        "VNG4": qsTr("Softer, no maze patterns · VNG4"),
        "PPG": qsTr("Fast, older · PPG"),
        "RCD (dual)": qsTr("Standard, smoother flat areas · RCD + VNG4"),
        "AMaZE (dual)": qsTr("Finest detail, smoother flat areas · AMaZE + VNG4"),
        "VNG": qsTr("Softer · VNG"),
        "Markesteijn 1-pass": qsTr("Standard (recommended) · Markesteijn 1-pass"),
        "Markesteijn 3-pass": qsTr("Finest detail, slower · Markesteijn 3-pass"),
        "Markesteijn 3-pass (dual)": qsTr("Finest detail, smoother flat areas · Markesteijn + VNG"),
        "frequency domain chroma": qsTr("Fewer colour artefacts, slowest · frequency domain"),
        "photosite color (debug)": qsTr("Show the sensor's mosaic (diagnostic)"),
        "once": qsTr("1 pass"), "twice": qsTr("2 passes"), "three times": qsTr("3 passes"),
        "four times": qsTr("4 passes"), "five times": qsTr("5 passes"),
        "local average": qsTr("Locally"), "full average": qsTr("Across the picture"),
        "full and local average": qsTr("Both"),
        "global average (fast)": qsTr("Judge the whole picture (fast)"),
        "local average (slow)": qsTr("Judge each area (slow)"),
        "static threshold (fast)": qsTr("Fixed threshold (fast)"),
        "distortion & TCA": qsTr("Distortion and colour fringing"),
        "TCA & vignetting": qsTr("Colour fringing and vignetting"),
        "only TCA": qsTr("Only colour fringing"),
        "only manual vignette": qsTr("Manual vignetting only"),
        "passthrough (monochrome)": qsTr("None (monochrome sensor)"),
        "embedded metadata": qsTr("Camera data"),
        "lensfun": qsTr("Lens database"),
        "autodetect": qsTr("As shot"),
        "no rotation": qsTr("Unrotated"),
        "rotate 90°": qsTr("Rotate 90° left"),
        "rotate -90°": qsTr("Rotate 90° right"),
        "rotate 180°": qsTr("Rotate 180°"),
        "flip horizontally": qsTr("Flip horizontally"),
        "flip vertically": qsTr("Flip vertically"),
        "transpose": qsTr("Rotate right + flip horizontally"),
        "transverse": qsTr("Rotate right + flip vertically"),
        "disabled": qsTr("Off"),
        "off": qsTr("Off")
    })

    // engine module → one sentence for the heading's tooltip
    readonly property var moduleTips: ({
        "exposure": qsTr("Overall brightness of the picture, set in stops before anything else is judged."),
        "sigmoid": qsTr("Shapes the curve that maps the camera's range onto the screen. This rendering replaces Film tone or Print stock; the general Contrast control is in Light."),
        "filmicrgb": qsTr("A film-like tone mapping with its own white and black points; use it instead of Tone, not with it."),
        "channelmixerrgb": qsTr("Colour temperature and tint of the light the photo was taken in."),
        "temperature": qsTr("The camera's own white balance coefficients; leave it alone and set the balance above."),
        "colorbalancergb": qsTr("Saturation, vibrance and colour grading by tonal zone: shadows, midtones, highlights."),
        "shadhi": qsTr("Lifts shadows and recovers highlights without moving the midtones."),
        "hazeremoval": qsTr("Cuts through atmospheric haze for deeper contrast and colour in distant parts."),
        "highlights": qsTr("Rebuilds detail where the sensor clipped, so bright skies and lights do not go flat."),
        "colorin": qsTr("The camera profile the picture is interpreted through, and the working space it is edited in."),
        "colorout": qsTr("The profile the picture is converted to for display and export."),
        "monochrome": qsTr("Convert to black and white and control how light or dark each original colour becomes."),

        "omarawprofile": qsTr("An independent colour look after film rendering. Amount 0 removes its effect, 100 gives the original look and 200 doubles the colour adjustment. Exposure, white balance and other edits stay in place."),
        "lut3d": qsTr("Applies a colour look-up table from a file, for a shared look or a film emulation. It runs after Tone or Film tone, and before a print stock. Match the LUT's expected colour space and judge it with your chosen rendering."),
        "sharpen": qsTr("Sharpens edges across the picture; go by the 100% view."),
        "demosaic": qsTr("A sensor records one colour at each pixel; this fills in the other two (demosaicing). The standard choice suits almost every file."),
        "rawprepare": qsTr("The sensor's black and white levels as read from the file."),
        "denoiseprofile": qsTr("Noise reduction tuned to this camera and ISO; judge it at 100%."),
        "bilat": qsTr("Local contrast in the midtones, the way Clarity works elsewhere."),
        "diffuse": qsTr("Texture: brings out fine detail or smooths it, slow on large files."),
        "hotpixels": qsTr("Removes stuck bright pixels from long exposures and high ISO."),
        "cacorrect": qsTr("Corrects chromatic aberration in Bayer RAWs only. For X-Trans or rendered images, use Lens corrections or Defringe."),
        "grain": qsTr("Adds film-like grain."),
        "bloom": qsTr("A soft glow spreading from the brightest parts of the picture."),
        "lens": qsTr("Corrects distortion, vignetting and chromatic aberration from the lens profile."),
        "flip": qsTr("Which way up the picture is."),
        "defringe": qsTr("Removes purple and green fringes that lens correction leaves behind."),
        "ashift": qsTr("Straightens the picture and corrects converging verticals and horizontals."),
        "crop": qsTr("The edges of the picture; drag the frame in the viewer or set them here."),
        "vignette": qsTr("Darkens or lightens the corners to draw the eye inwards."),
        "rgbcurve": qsTr("A curve of points over the whole tonal range."),
        "tonecurve": qsTr("Four regions of the tonal range, each with its own slider."),
        "basecurve": qsTr("A camera-style base curve applied before the rest."),
        "colorzones": qsTr("Hue, saturation and luminance for each band of colour."),
        "colorequal": qsTr("Shifts one colour at a time without touching its neighbours."),
        "retouch": qsTr("Heals, clones and blurs spots on the picture."),
        "omarawhalation": qsTr("The glow a bright area throws around itself on film: light passes the emulsion, bounces off the base and exposes it again from behind, red to orange at the edges."),
        "omarawprint": qsTr("The photograph as film: a cinema release print, exposed onto a colour negative, printed and projected, or a still negative printed on photographic paper. Built from the makers' data sheets. While a stock is chosen the tone mapper is off; the print is the rendering.")
    })

    // "module.field" → one sentence on what the control does
    readonly property var controlTips: ({
        "channelmixerrgb.temperature": qsTr("Warmer to the right, cooler to the left, in kelvin along the daylight line."),
        "channelmixerrgb.tint": qsTr("Magenta to the right, green to the left; fixes a cast the temperature cannot."),
        "exposure.exposure": qsTr("Brightness in stops: +1 doubles the light, -1 halves it."),
        "exposure.black": qsTr("Where pure black sits; a small negative value lifts the deepest shadows."),
        "shadhi.shadows": qsTr("Lifts the dark parts without flattening the rest."),
        "shadhi.highlights": qsTr("Pulls the bright parts back to bring out detail in skies and skin."),
        "sigmoid.middle_grey_contrast": qsTr("Slope of the rendering curve around middle grey. Selecting this rendering replaces Film tone or Print stock. Use Light → Contrast to keep the chosen look."),
        "sigmoid.contrast_skewness": qsTr("Moves the rendering curve's steepest part towards the shadows or highlights, keeping middle grey."),
        "filmicrgb.white_point_source": qsTr("How many stops above middle grey become white; raise it to hold bright detail."),
        "filmicrgb.black_point_source": qsTr("How many stops below middle grey become black; lower it for deeper shadows."),
        "highlights.mode": qsTr("How clipped highlights are rebuilt; Inpaint suits most photos."),
        "highlights.clip": qsTr("The level treated as clipped; lower it if magenta remains in the brightest parts."),
        "hazeremoval.strength": qsTr("How much haze to remove; a little goes a long way."),
        "hazeremoval.distance": qsTr("How far into the scene the removal reaches."),
        "colorbalancergb.saturation_global": qsTr("Colour intensity everywhere; stronger to the right."),
        "colorbalancergb.vibrance": qsTr("Boosts the quieter colours more than the already strong ones; kinder to skin."),
        "colorbalancergb.contrast": qsTr("Overall contrast, before the chosen tone mapping or print stock. Shares the effect switch in Colour → Vibrance & saturation."),
        "colorbalancergb.chroma_global": qsTr("Colourfulness measured perceptually, everywhere."),
        "colorbalancergb.brilliance_global": qsTr("Brightness of the coloured parts without touching the greys."),
        "colorbalancergb.hue_angle": qsTr("Rotates every hue by this many degrees."),
        "colorin.type": qsTr("The profile that says what the camera's colours mean; the standard matrix suits most files."),
        "colorin.intent": qsTr("How out-of-range colours are brought in when the profile converts them."),
        "colorin.normalize": qsTr("Clips colours to a chosen space to tame extreme values from the sensor."),
        "colorin.type_work": qsTr("The colour space the edit is calculated in; wide spaces keep more colour."),
        "monochrome.size": qsTr("How wide the virtual colour filter is; wider is a gentler filter."),
        "monochrome.highlights": qsTr("Preserves more of the original brightness in lighter areas during filtering."),
        "lut3d.filepath": qsTr("A .cube, .png, .3dl or .gmz look-up table to apply."),
        "lut3d.colorspace": qsTr("The colour space the table was made for; must match or the look is wrong."),
        "lut3d.interpolation": qsTr("How values between the table's entries are filled; tetrahedral is the usual choice."),
        "sharpen.amount": qsTr("How much sharpening to apply."),
        "sharpen.radius": qsTr("How wide the sharpened edge is, in pixels; small for fine detail."),
        "demosaic.demosaicing_method": qsTr("The algorithm that turns the sensor mosaic into pixels; RCD is the usual choice."),
        "demosaic.color_smoothing": qsTr("Smooths colour speckle left by demosaicing, without touching detail."),
        "demosaic.green_eq": qsTr("Evens out the two green channels on sensors that need it, removing a maze pattern."),
        "demosaic.cs_enabled": qsTr("Sharpens at the demosaic stage, before any other processing."),
        "demosaic.cs_radius": qsTr("How wide the capture sharpening acts."),
        "denoiseprofile.mode": qsTr("The noise reduction method; wavelets are fast, non-local means keeps more detail."),
        "denoiseprofile.strength": qsTr("How much noise to remove; too much smooths real detail."),
        "bilat.detail": qsTr("Midtone contrast: right adds punch, left softens."),
        "diffuse.texture": qsTr("Right brings out fine detail, left smooths it."),
        "hotpixels.strength": qsTr("How strongly stuck pixels are replaced."),
        "hotpixels.threshold": qsTr("How far a pixel must stand out from its neighbours to count as stuck."),
        "hotpixels.markfixed": qsTr("Shows the pixels that were fixed, to check the threshold."),
        "cacorrect.avoidshift": qsTr("Bayer RAWs only. Stops the correction shifting colours in areas that had no fringe."),
        "grain.strength": qsTr("How much grain to add."),
        "grain.scale": qsTr("How coarse the grain is: fine for 35 mm, coarse for 8 mm."),
        "grain.midtones_bias": qsTr("Keeps grain out of the darkest and brightest parts as it rises."),
        "omarawprint.stock": qsTr("Choose a cinema print or still-film look. Still films include Portrait, Fine Grain, Warm and Vivid."),
        "omarawprint.strength": qsTr("How much of the print shows; 0 is the plain rendering."),
        "omarawprint.white": qsTr("Warm the whole print: Neutral uses D65; the warmer choices use D60, D55 and D50, like changing the viewing light."),
        "omarawprint.paper": qsTr("Balanced print keeps the stock's tone curve, corrects unwanted casts on neutral greys and retains highlight headroom. The legacy choices preserve earlier edits; Bright whites can clip highlights."),
        "omarawprint.trim_r": qsTr("Left adds red; right adds cyan. This adjusts red printer light, as in a film lab. Centre keeps the stock's colour balance."),
        "omarawprint.trim_g": qsTr("Left adds green; right adds magenta. This adjusts green printer light, as in a film lab. Centre keeps the stock's colour balance."),
        "omarawprint.trim_b": qsTr("Left adds blue; right adds yellow. This adjusts blue printer light, as in a film lab. Centre keeps the stock's colour balance."),
        "omarawprint.gain_r": qsTr("Adjust contrast in the red channel by scaling its printing density. This can also shift colour; 0 keeps the stock's response."),
        "omarawprint.profile": qsTr("The saved tables for this camera's DCP look. Choose or import a profile in Prepare → Camera profile; imported XMP presets can also select one."),
        "omarawprint.gain_g": qsTr("Adjust contrast in the green channel by scaling its printing density. This can also shift colour; 0 keeps the stock's response."),
        "omarawprint.gain_b": qsTr("Adjust contrast in the blue channel by scaling its printing density. This can also shift colour; 0 keeps the stock's response."),
        "omarawhalation.strength": qsTr("How strong the halo is. 0 leaves the picture alone."),
        "omarawhalation.scatter": qsTr("How far the halo spreads from the bright edge."),
        "omarawhalation.dye": qsTr("The halo's colour, from red towards orange."),
        "omarawhalation.boost": qsTr("How saturated the halo is; 0 is a white glow."),
        "omarawhalation.threshold": qsTr("How bright a part must be before it halates."),
        "bloom.size": qsTr("How far the glow spreads."),
        "bloom.threshold": qsTr("How bright a part must be before it glows."),
        "bloom.strength": qsTr("How strong the glow is."),
        "lens.method": qsTr("Where the correction comes from: the camera's own data or the lens database."),
        "lens.modify_flags": qsTr("Which faults to correct: distortion, vignetting, chromatic aberration."),
        "lens.scale": qsTr("Zooms in after correction to hide the bent edges."),
        "flip.orientation": qsTr("Sets the picture's orientation. As shot follows the camera tag; Unrotated uses the file's original pixel order. Fine straightening is in Crop & straighten."),
        "defringe.op_mode": qsTr("How fringe colours are found; global is fast, local is thorough."),
        "defringe.radius": qsTr("How far from an edge fringes are looked for."),
        "defringe.thresh": qsTr("How strong a colour must be to count as a fringe."),
        "ashift.rotation": qsTr("Straightens the horizon; positive turns clockwise."),
        "ashift.lensshift_v": qsTr("Corrects converging verticals, as when a building was shot from below."),
        "ashift.lensshift_h": qsTr("Corrects converging horizontals, as when a wall was shot from the side."),
        "ashift.cropmode": qsTr("Trims empty corners after rotation or perspective: largest area, original aspect ratio, or off."),
        "crop.cx": qsTr("The left edge, as a fraction of the width."),
        "crop.cy": qsTr("The top edge, as a fraction of the height."),
        "crop.cw": qsTr("The right edge, as a fraction of the width."),
        "crop.ch": qsTr("The bottom edge, as a fraction of the height."),
        "vignette.brightness": qsTr("Darkens the corners to the left, lightens them to the right."),
        "vignette.scale": qsTr("How far from the centre the vignette starts."),
        "vignette.falloff_scale": qsTr("How softly the vignette blends in."),
        "vignette.saturation": qsTr("Takes colour out of the corners (left) or adds it (right); at 0 the corners keep their colour."),
        "vignette.shape": qsTr("At 1 an oval fitted to the frame; lower is squarer, higher more pointed.")
    })

    // develop group key → tab label and one sentence
    readonly property var groups: ({ "Basic": qsTr("Light"), "Color": qsTr("Colour"), "Grading": qsTr("Grading"), "Film": qsTr("Film & effects"), "Lens": qsTr("Lens & geometry"), "Local": qsTr("Masks") })
    readonly property var groupTips: ({
        "Basic": qsTr("Exposure, contrast, shadows and highlights: start here."),
        "Curve": qsTr("Point and parametric curves over the tonal range."),
        "Color": qsTr("White balance, vibrance and saturation, and colour by colour."),
        "Grading": qsTr("Colour wheels: the look, once the colour is right."),
        "Film": qsTr("A film look and finishing effects, added last: print stock, grain, halation, glow and vignette."),
        "Detail": qsTr("Sharpening, noise reduction, clarity, texture, grain and glow."),
        "Lens": qsTr("Lens corrections, orientation, transform and crop."),
        "Local": qsTr("Adjustments limited to a radial, a gradient or a brush mask."),
        "Retouch": qsTr("Heal, clone, blur and fill spots.")
    })

    // Controls a first look at a section leaves out. They are one click
    // away under "More controls"; "module.*" takes a whole adjustment there.
    readonly property var advancedControls: [
        "exposure.black", "sigmoid.contrast_skewness", "filmicrgb.*", "highlights.*", "hazeremoval.distance",
        "colorin.*", "monochrome.*", "lut3d.*",
        "sharpen.radius", "demosaic.*", "denoiseprofile.mode", "hotpixels.*", "cacorrect.*", "grain.midtones_bias", "omarawhalation.threshold", "omarawprint.white", "omarawprint.paper", "omarawprint.trim_r", "omarawprint.trim_g", "omarawprint.trim_b", "omarawprint.gain_r", "omarawprint.gain_g", "omarawprint.gain_b", "omarawprint.profile",
        "lens.method", "lens.scale", "defringe.*", "crop.*", "ashift.cropmode", "vignette.saturation", "vignette.shape"
    ]
    function isAdvanced(op, field) { return advancedControls.indexOf(op + "." + field) >= 0 || advancedControls.indexOf(op + ".*") >= 0 }
    // The order a section's first controls are met in, where the engine's
    // own order is not the one people work in. Unlisted controls follow.
    readonly property var leadOrder: [
        "exposure.exposure", "sigmoid.middle_grey_contrast", "shadhi.highlights", "shadhi.shadows", "hazeremoval.strength",
        "channelmixerrgb.temperature", "channelmixerrgb.tint",
        // Film & effects: the film first (print, grain, halation), then the finishing effects.
        "omarawprint.stock", "omarawprint.strength", "grain.strength", "grain.scale",
        "omarawhalation.strength", "omarawhalation.scatter", "omarawhalation.dye", "omarawhalation.boost",
        "bloom.strength", "bloom.size", "bloom.threshold",
        "vignette.brightness", "vignette.scale", "vignette.falloff_scale"
    ]
    function ordered(rows) {
        const rank = p => { const i = leadOrder.indexOf(p.op + "." + p.field); return i < 0 ? leadOrder.length : i }
        return rows.map((p, i) => ({p: p, i: i})).sort((a, b) => rank(a.p) - rank(b.p) || a.i - b.i).map(e => e.p)
    }
    // Which section shows a control. White balance is colour to the people
    // using it, whatever the engine's table calls it.
    function section(p) { return p.op === "channelmixerrgb" && (p.field === "temperature" || p.field === "tint") ? "Color" : p.group }
    // A lone "Amount" says nothing without its heading.
    function isGenericLabel(op, field, fallback) {
        const label = control(op, field, fallback || "")
        return [qsTr("Amount"), qsTr("Strength"), qsTr("Method")].indexOf(label) >= 0
    }

    function tidy(s) {
        s = String(s || "")
        s = s.replace(/\bcolor\b/g, "colour").replace(/\bColor\b/g, "Colour")
        // "sRGB" stays; "once" becomes "Once".
        if (s.length > 0 && (s.length === 1 || s[1] === s[1].toLowerCase())) s = s[0].toUpperCase() + s.substring(1)
        return s
    }
    function module(op, fallback) { return names.modules[op] || tidy(fallback || op) }
    function control(op, field, fallback) { return names.controls[op + "." + field] || fallback || field }
    // Some engine choices arrive as bare identifiers (DT_COLORSPACE_LIN_REC2020).
    readonly property var identifiers: ({
        "DT_INTENT_PERCEPTUAL": qsTr("Perceptual"), "DT_INTENT_RELATIVE_COLORIMETRIC": qsTr("Relative colorimetric"),
        "DT_INTENT_SATURATION": qsTr("Saturation"), "DT_INTENT_ABSOLUTE_COLORIMETRIC": qsTr("Absolute colorimetric"),
        "DT_COLORSPACE_STANDARD_MATRIX": qsTr("Camera standard (recommended)"), "DT_COLORSPACE_ENHANCED_MATRIX": qsTr("Camera enhanced"),
        "DT_COLORSPACE_VENDOR_MATRIX": qsTr("Camera maker's"), "DT_COLORSPACE_ALTERNATE_MATRIX": qsTr("Camera alternative"),
        "DT_COLORSPACE_EMBEDDED_ICC": qsTr("Embedded in the file"), "DT_COLORSPACE_EMBEDDED_MATRIX": qsTr("Embedded in the file (matrix)"),
        "DT_COLORSPACE_SRGB": "sRGB", "DT_COLORSPACE_ADOBERGB": "RGB (1998)", "DT_COLORSPACE_PROPHOTO_RGB": "ProPhoto RGB",
        "DT_COLORSPACE_DISPLAY_P3": "Display P3", "DT_COLORSPACE_REC709": "Rec. 709",
        "DT_COLORSPACE_LIN_REC709": qsTr("Linear Rec. 709"), "DT_COLORSPACE_LIN_REC2020": qsTr("Linear Rec. 2020 (recommended)"),
        "DT_COLORSPACE_PQ_REC2020": "Rec. 2020 PQ", "DT_COLORSPACE_HLG_REC2020": "Rec. 2020 HLG", "DT_COLORSPACE_PQ_P3": "P3 PQ", "DT_COLORSPACE_HLG_P3": "P3 HLG",
        "DT_COLORSPACE_XYZ": "XYZ", "DT_COLORSPACE_LAB": "Lab", "DT_COLORSPACE_INFRARED": qsTr("Infrared"), "DT_COLORSPACE_FILE": qsTr("From a profile file")
    })
    function choice(label) {
        if (names.choices[label]) return names.choices[label]
        if (names.identifiers[label]) return names.identifiers[label]
        const bare = /^DT_[A-Z]+_(.+)$/.exec(label)
        return tidy(bare ? bare[1].toLowerCase().replace(/_/g, " ") : label)
    }
    function group(key) { return names.groups[key] || key }
    function groupTip(key) { return names.groupTips[key] || "" }
    function moduleTip(op) { return names.moduleTips[op] || "" }
    function describe(op, field) {
        if (op === "monochrome" && field.startsWith("mix_"))
            return qsTr("Darkens this original colour to the left and brightens it to the right in the black and white conversion. Zero keeps the neutral mix; neighbouring colours blend smoothly.")
        return names.controlTips[op + "." + field] || ""
    }
    // A history entry reads "<module name> · <instance name>"; the module
    // half is the engine's, the instance half is the user's.
    function history(op, label) {
        const i = label.indexOf(" · ")
        return names.module(op, i < 0 ? label : label.substring(0, i)) + (i < 0 ? "" : label.substring(i))
    }
}
