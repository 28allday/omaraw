#!/usr/bin/env bash
# Compare the OpenEXR and Imath series each supported system's repositories
# now carry with the latest release's packages. When a repository has moved on,
# open (or refresh) one issue so a rebuilt release can follow quickly; close it
# once a release matches every system again. --dry-run only prints the result.
set -euo pipefail
dry_run=0
[[ ${1:-} == --dry-run ]] && dry_run=1
title='Rebuild needed: system libraries have moved past the latest release'
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

# Keep these in step with the builders in tools/ci-package.sh.
targets=(
  'x86_64|Omarchy stable (x86_64)|https://stable-mirror.omarchy.org/extra/os/x86_64/extra.db'
  'x86_64|Omarchy edge / current Arch (x86_64)|https://geo.mirror.pkgbuild.com/extra/os/x86_64/extra.db'
  'aarch64|Omarchy Mac / Arch Linux ARM (aarch64)|https://fl.us.mirror.archlinuxarm.org/aarch64/extra/extra.db'
)

series() { sed -E 's/^[0-9]+://; s/^([0-9]+\.[0-9]+).*/\1/' <<< "$1"; }

tag=$(gh release list --exclude-drafts --limit 1 --json tagName --jq '.[0].tagName')
[[ -n $tag ]] || { echo 'No published release to compare.' >&2; exit 1; }

# A package's .PKGINFO comes first in the archive, so its start is enough.
declare -A built=()
while IFS=$'\t' read -r name url; do
  [[ $name =~ ^omaraw-.*-(x86_64|aarch64)(-openexr[0-9.]+)?\.pkg\.tar\.zst$ ]] || continue
  arch=${BASH_REMATCH[1]}
  curl -fsSL --retry 3 -r 0-262143 -o "$work/head" "$url"
  info=$(zstd -dc "$work/head" 2>/dev/null | tar -xOf - .PKGINFO 2>/dev/null || true)
  openexr=$(sed -n 's/^depend = openexr>=//p' <<< "$info")
  imath=$(sed -n 's/^depend = imath>=//p' <<< "$info")
  [[ -n $openexr ]] || { echo "Cannot read the OpenEXR dependency of $name" >&2; exit 1; }
  built[$name]="$arch|$(series "$openexr")|${imath:+$(series "$imath")}"
done < <(gh release view "$tag" --json assets --jq '.assets[] | [.name, .url] | @tsv')
((${#built[@]})) || { echo "Release $tag has no packages." >&2; exit 1; }

problems=()
report=()
for target in "${targets[@]}"; do
  IFS='|' read -r arch label db <<< "$target"
  curl -fsSL --retry 3 -o "$work/repo.db" "$db"
  listing=$(tar -tzf "$work/repo.db")
  openexr=$(grep -E '^openexr-[0-9]' <<< "$listing" | head -1 | sed -E 's|^openexr-(.*)-[^-]+/$|\1|' || true)
  imath=$(grep -E '^imath-[0-9]' <<< "$listing" | head -1 | sed -E 's|^imath-(.*)-[^-]+/$|\1|' || true)
  [[ -n $openexr && -n $imath ]] || { echo "Cannot read OpenEXR/Imath from $db" >&2; exit 1; }
  matched=
  for name in "${!built[@]}"; do
    IFS='|' read -r built_arch built_openexr built_imath <<< "${built[$name]}"
    [[ $built_arch == "$arch" && $built_openexr == "$(series "$openexr")" ]] || continue
    [[ -z $built_imath || $built_imath == "$(series "$imath")" ]] || continue
    matched=$name
  done
  if [[ -n $matched ]]; then
    report+=("- $label: OpenEXR $openexr, Imath $imath — matched by \`$matched\`")
  else
    problems+=("- **$label**: OpenEXR $openexr, Imath $imath — no $arch package in $tag was built for this series")
  fi
done

printf '%s\n' "Latest release: $tag" "${problems[@]}" "${report[@]}"
existing=$(gh issue list --state open --search "in:title \"$title\"" --json number,title \
  --jq '.[] | "\(.number)\t\(.title)"' | awk -F'\t' -v title="$title" '$2 == title {print $1; exit}')
if ((${#problems[@]})); then
  body=$(printf '%s\n' \
    "Checked automatically against the latest release, $tag. OmaRAW will not start on these" \
    'systems once they update, until a release built against their libraries is published.' \
    'Installed copies show a warning after the update; the system update itself still completes.' \
    '' "${problems[@]}" '' 'Still matching:' "${report[@]}")
  ((dry_run)) && { printf '\n[dry run] would %s:\n%s\n' "${existing:+update issue #$existing}${existing:-open an issue}" "$body"; exit 0; }
  if [[ -n $existing ]]; then gh issue edit "$existing" --body "$body"
  else gh issue create --title "$title" --label bug --body "$body"; fi
elif [[ -n $existing ]]; then
  ((dry_run)) && { echo "[dry run] would close issue #$existing"; exit 0; }
  gh issue close "$existing" --comment "Release $tag now matches every supported system's libraries."
fi
