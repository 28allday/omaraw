# DNG compatibility

This product includes DNG technology under license by Adobe.

OmaRAW supports Bayer and linear RGB DNGs, including supported JPEG XL compressed
tiles and integer or floating-point pixels. Embedded calibration, baseline
exposure and the primary camera profile are used when supported.

The renderer supports ProfileGainTableMap and ProfileGainTableMap2. These
profile adjustments stay aligned when the photograph is cropped or rotated.
They are applied before the enhanced profile look and creative grading.

Support does not cover every DNG opcode or profile feature. There is no selector
for alternative embedded profiles or HDR/SDR profile variants. Unsupported
required calibration or malformed data can prevent a file from opening.
The result may differ from another application's rendering of the same DNG.

These paths work on the CPU and do not require a particular GPU. AI denoising
has narrower input requirements than Develop; see [AI denoise](AI-DENOISE.md).
