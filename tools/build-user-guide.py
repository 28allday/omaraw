#!/usr/bin/env python3
"""Build the installed manual from the same pages used by F1 help."""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parent.parent


def guide():
    source = (ROOT / "src/backend.cpp").read_text()
    table = source.split("QVariantList Backend::helpSections() const", 1)[1].split("QVariantList out;", 1)[0]
    pages = re.findall(r'\{"([a-z-]+)", "[^"]+", "[^"]+"\}', table)
    actual = {p.stem for p in (ROOT / "src/help").glob("*.md")}
    if not pages or set(pages) != actual:
        raise ValueError("Help index and page files do not agree")
    parts = ["# OmaRAW user guide\n\nThis guide contains the same instructions as Help (F1) in OmaRAW.\n"]
    for page in pages:
        text = (ROOT / "src/help" / (page + ".md")).read_text()
        parts.append(re.sub(r"^(#+) ", r"\1# ", text, flags=re.M).strip() + "\n")
    return "\n".join(parts)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: tools/build-user-guide.py OUTPUT.md")
    output = Path(sys.argv[1])
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(guide())
