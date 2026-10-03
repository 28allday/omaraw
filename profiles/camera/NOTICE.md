# Included community camera profiles

These 54 original DCP files come from the RawTherapee project at revision
`94c3096e706d89a2325415d56af188ca0228ce34`:
https://github.com/RawTherapee/RawTherapee/tree/94c3096e706d89a2325415d56af188ca0228ce34/rtdata/dcpprofiles

46 files explicitly declare **public domain** in their ProfileCopyright tag.
Eight explicitly declare **RawTherapee CC0**. CC0's full text is retained in
`CC0-1.0.txt`; https://creativecommons.org/publicdomain/zero/1.0/.
All 54 declare unrestricted embedding (ProfileEmbedPolicy 3), in addition to
their explicit copyright declaration. Embedding policy is not used as a
substitute for a copyright licence.

`manifest.json` records each original filename, profile/camera name, copyright
declaration, licence, embedding policy, byte length, SHA-256 and pinned source.
The DCP bytes are unmodified. Attribution: RawTherapee and its profile
contributors. RawTherapee documents its original chart-based profile creation
process at https://rawpedia.pixls.us/how_to_create_dcp_color_profiles/.

OmaRAW selected only profiles with an explicit public-domain/CC0 declaration
and an explicit tone curve supported by its renderer. Only files meeting those criteria were selected. Camera names identify compatibility, not manufacturer
authorship or endorsement.

The UI calls a matching included profile **Community colour**. Rendering is an
OmaRAW approximation; it is not an exact RawTherapee or camera-JPEG match.
There is currently no white-balance interpolation between DCP illuminants.

The separate **Camera look** action uses the contrast and saturation settings
from darktable's camera styles already shipped in OmaRAW's private engine,
at revision `281f60c9957b05b9379084b272c81a5e62fa46e3`, GPL-3.0-or-later.
It deliberately omits exposure, local contrast, lens and other adjustments.
See `THIRD_PARTY.md`, the engine's `LICENSE.darktable` and corresponding source.

Run `python3 tools/audit-camera-profiles.py` to verify this collection offline.
