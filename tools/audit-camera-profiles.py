#!/usr/bin/env python3
"""Offline licence, provenance and integrity check for the included DCPs.

Adding a profile requires reviewing its original source and updating the pinned
manifest. This deliberately accepts only explicit public-domain/CC0 profiles;
a DCP's embedding policy alone is not a redistribution licence.
"""
import hashlib
import json
from pathlib import Path
import struct

root = Path(__file__).resolve().parents[1] / "profiles/camera"
manifest = json.loads((root / "manifest.json").read_text())
assert manifest["repository"] == "https://github.com/RawTherapee/RawTherapee"
assert len(manifest["revision"]) == 40
allowed = {"RawTherapee CC0": "CC0-1.0", "public domain": "public-domain"}
files = set()
for entry in manifest["profiles"]:
    name = entry["file"]
    assert Path(name).name == name and name.endswith(".dcp"), name
    assert name not in files, name
    files.add(name)
    data = (root / name).read_bytes()
    assert hashlib.sha256(data).hexdigest() == entry["sha256"], name
    assert len(data) == entry["bytes"], name
    assert data[:4] in (b"IIRC", b"MMCR"), name
    endian = "<" if data[:2] == b"II" else ">"
    at = struct.unpack_from(endian + "I", data, 4)[0]
    count = struct.unpack_from(endian + "H", data, at)[0]
    tags = {}
    widths = {1: 1, 2: 1, 3: 2, 4: 4, 5: 8, 7: 1, 9: 4, 10: 8, 11: 4, 12: 8}
    for i in range(count):
        pos = at + 2 + i * 12
        tag, kind, size, offset = struct.unpack_from(endian + "HHII", data, pos)
        length = widths[kind] * size
        start = pos + 8 if length <= 4 else offset
        assert start + length <= len(data), name
        tags[tag] = (kind, size, data[start:start + length])
    copyright_ = tags[50942][2].rstrip(b"\0").decode()
    assert copyright_ in allowed and copyright_ == entry["copyright"], name
    assert entry["licence"] == allowed[copyright_], name
    assert tags[50941][:2] == (4, 1), name
    assert struct.unpack(endian + "I", tags[50941][2])[0] == 3, name
    assert entry["embedPolicy"] == [3], name
    assert 50940 in tags, name  # Current renderer needs an explicit tone curve.
    assert tags[50708][2].rstrip(b"\0").decode() == entry["camera"], name
    expected = f'{manifest["repository"]}/blob/{manifest["revision"]}/rtdata/dcpprofiles/{name}'
    assert entry["source"] == expected, name
assert files == {p.name for p in root.glob("*.dcp")}
assert (root / "CC0-1.0.txt").is_file()
assert (root / "NOTICE.md").is_file()
print(f"Verified {len(files)} camera profiles: licences, original metadata, pinned sources and SHA-256.")
