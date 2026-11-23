#!/usr/bin/env bash
# Check that each supported system's repositories still provide every library
# some package in the latest release loads (the release's LIBRARIES asset).
# When a repository has moved on, open (or refresh) one issue so a rebuilt
# release can follow quickly; close it once a release matches every system
# again. --dry-run only prints the result.
set -euo pipefail
dry_run=0
[[ ${1:-} == --dry-run ]] && dry_run=1
title='Rebuild needed: system libraries have moved past the latest release'
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

# Keep these in step with the builders in tools/ci-package.sh.
targets=(
  'x86_64|Omarchy stable (x86_64)|https://stable-mirror.omarchy.org/$repo/os/$arch'
  'x86_64|Omarchy edge / current Arch (x86_64)|https://geo.mirror.pkgbuild.com/$repo/os/$arch'
  'aarch64|Omarchy Mac (aarch64)|https://arm-stable-mirror.omarchy.org/$arch/$repo'
)

tag=$(gh release list --exclude-drafts --limit 1 --json tagName --jq '.[0].tagName')
[[ -n $tag ]] || { echo 'No published release to compare.' >&2; exit 1; }
gh release download "$tag" --pattern LIBRARIES --dir "$work" || {
  echo "Release $tag has no LIBRARIES asset, so its packages cannot be checked." >&2; exit 1;
}

problems=()
report=()
while IFS=$'\t' read -r state label detail; do
  if [[ $state == OK ]]; then report+=("- $label: matched by \`$detail\`")
  else problems+=("- **$label**: $detail"); fi
done < <(python3 "$here/release-libraries.py" watch "$work/LIBRARIES" "${targets[@]}")
((${#problems[@]} + ${#report[@]} == ${#targets[@]})) || { echo 'The library check did not report every system.' >&2; exit 1; }

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
