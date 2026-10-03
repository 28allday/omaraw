# Colour grading and finishing effects

Print stock and LUTs are in **Colour & Look**, alongside Primary correction. Grain, Halation, Glow and Vignette are in **Effects**. Each tool has its own heading. Grain, Halation and Vignette show all their controls directly; Print stock keeps its larger set of fine controls under Advanced.

A film look and finishing effects: print stock, grain, halation, glow and vignette. These effects feed the print stock when it is on. Each starts from nothing and leaves the picture alone until moved. Judge grain and halation at 100% zoom.

For a starting recipe, open **Presets** and choose **Film** or **Cinematic** from the category dropdown. The **Black & white** collection also includes a Cinema Mono 2302 Silver Print recipe. Each recipe sets a print and/or tonal look with matching finishing effects. To save your own film recipe, use **Presets → + → None → Film**; to save just the print, use **Print stock → ⋯ → Save adjustment preset**.

## Print stock

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

## Grain

- **Amount**: film-like grain, applied before a print stock.
- **Size**: how coarse the grain is. Fine suits 35 mm; coarse, 8 mm.
- **Mid-tone bias**: keeps grain out of the darkest and brightest parts as it rises.

## Glow

- **Strength**, **Size**, **Threshold**: a soft bloom around the brightest areas. Threshold sets how bright something must be to glow.

Strength rests at zero while Glow is off. Setting it to zero or resetting it switches Glow off and keeps the saved recipe for the effect switch. Size has its own default of 20; it controls the spread, not whether glow is applied.

## Halation

On film, light passes the emulsion, bounces off the base and exposes it again from behind, so a bright area throws a red-to-orange glow around itself, strongest at hard bright edges.

- **Amount**: how strong the halo is; 0 leaves the picture alone.
- **Scatter**: how far the halo spreads from the bright edge.
- **Dye transmission**: the halo's colour, from red towards orange.
- **Boost**: how saturated the halo is; 0 is a white glow.
- **Threshold**: how bright a part must be before it halates.

## Vignette

The vignette follows the frame's shape and the crop: it is centred on what you keep, and the four corners always match.

- **Amount**: darkens (−) or lightens (+) the corners. Lightening scales the light, so blacks stay black; −100 takes the very corners to black.
- **Midpoint**: how far from the centre the fade begins.
- **Feather**: how gradually it fades in; the fade is always smooth, with no edge.
- **Corner saturation**: takes colour out of the corners (−) or adds it (+). It starts at 0, so Amount alone keeps the corners' colour.
- **Shape**: at 1 an oval fitted to the frame; lower is squarer, higher more pointed, towards a diamond.
