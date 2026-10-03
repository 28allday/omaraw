# Kodak Vision 2383 — third table version

Uses the same March 2005 H-1-2383t source and spectral model as
`kodak-2383-v2`, with corrected characteristic-curve axes only.
The sheet and supplied Kodak LAD images are not redistributed.

- Source: Kodak H-1-2383t (March 2005), page 5; source publication details
  are also recorded in `../kodak-2383/SOURCE.md`.
- The actual plot frame in PDF points is
  `(96.4010009765625, 82.489013671875, 280.8330078125, 267.05999755859375)`.
  Its ranges are log exposure −3..3 and Status A density 0..6.
- `extraction.json` records frame coordinates, not text-label centres.
  Reproduce with `tools/film/extract-curves.py extract PDF 5 SPEC OUTPUT`.
- Three CSVs replace only the characteristic curves; sensitivity, dyes,
  5219 negative model and viewing assumptions remain as documented in
  `docs/PRINT-STOCKS.md`. No claim of measured physical-film equivalence.
- Printer lights are recalibrated to Kodak's LAD Status A aims:
  red 1.09, green 1.06, blue 1.03 (H-61B and H-387).
- `tools/film/print_chain.py` bakes `kodak-2383-v3.ompt` (stock ID 23).
  IDs 0 and 14 and their table bytes are retained for saved edits.
- Kodak H-387 Cineon values represent printing density (0.002 × CV in
  intermediate negative mode), not linear scene RGB. The separate
  `tools/film/check-lad.py` check bypasses the simulated camera negative.

Trademarks belong to Eastman Kodak Company.
