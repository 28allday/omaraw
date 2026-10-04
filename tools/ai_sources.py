"""Resolve pinned AI inputs for a native Linux package build."""
import json
import platform
from pathlib import Path


ARCHITECTURES = ("x86_64", "aarch64")


def load_sources(root: Path, architecture: str | None = None) -> dict:
    architecture = architecture or platform.machine()
    if architecture not in ARCHITECTURES:
        raise ValueError(f"Unsupported package architecture: {architecture}")
    lock = json.loads((root / "pkgbuild/ai-sources.json").read_text())
    if architecture == "x86_64":
        return lock
    overlay = json.loads((root / f"pkgbuild/ai-sources-{architecture}.json").read_text())
    if overlay["python"] != lock["python"]:
        raise ValueError("ARM and x86 Python ABIs differ; update both dependency locks")
    replacements = {entry["replaces"]: entry for entry in overlay["replacements"]}
    if len(replacements) != len(overlay["replacements"]):
        raise ValueError("Duplicate ARM dependency replacement")
    for index, entry in enumerate(lock["sources"]):
        if entry["name"] in replacements:
            replacement = dict(replacements.pop(entry["name"]))
            replacement.pop("replaces")
            lock["sources"][index] = dict(entry, **replacement)
    if replacements:
        raise ValueError("ARM dependency lock is stale: " + ", ".join(replacements))
    if any("x86_64" in entry["name"] for entry in lock["sources"]):
        raise ValueError("ARM dependency lock still contains x86 binaries")
    lock["architecture"] = architecture
    return lock
