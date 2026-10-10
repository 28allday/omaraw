#!/usr/bin/env bash
# Download a beta/release package, verify its checksum, then use pacman.
set -euo pipefail
version=0.1.0-beta.10
download_dir=
usage() {
  printf '%s\n' 'Usage: bash install.sh [--version VERSION] [--download-only DIRECTORY]' \
    'Installs the matching Omarchy x86_64 or aarch64 package from a GitHub release.'
}
while (($#)); do
  case "$1" in
    --version|--download-only)
      (($# >= 2)) || { usage >&2; exit 2; }
      if [[ $1 == --version ]]; then version=$2; else download_dir=$2; fi
      shift 2 ;;
    --help|-h) usage; exit 0 ;;
    *) usage >&2; exit 2 ;;
  esac
done
[[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+(-(alpha|beta|rc)\.[0-9]+)?$ ]] || {
  printf 'Invalid release version: %s\n' "$version" >&2; exit 2;
}
architecture=$(uname -m)
[[ $(uname -s) == Linux && ( $architecture == x86_64 || $architecture == aarch64 ) ]] || {
  printf '%s\n' 'Packages support Linux x86_64 and aarch64. See the build instructions for other platforms.' >&2; exit 1;
}
for tool in curl sha256sum awk mktemp; do
  command -v "$tool" >/dev/null || { printf 'Required command missing: %s\n' "$tool" >&2; exit 1; }
done
if [[ -z $download_dir ]]; then
  command -v pacman >/dev/null || { printf '%s\n' 'This installer requires Arch Linux / Omarchy and pacman.' >&2; exit 1; }
  if ((EUID != 0)); then command -v sudo >/dev/null || { printf '%s\n' 'sudo is required to install the package.' >&2; exit 1; }; fi
fi
package_version=${version/-alpha./alpha}
package_version=${package_version/-beta./beta}
package_version=${package_version/-rc./rc}
package="omaraw-${package_version}-1-${architecture}.pkg.tar.zst"
url="https://github.com/28allday/omaraw/releases/download/v${version}"
temporary=$(mktemp -d -t omaraw-install.XXXXXXXX)
trap 'rm -rf -- "$temporary"' EXIT
download() {
  curl --fail --location --proto '=https' --tlsv1.2 --retry 2 --output "$temporary/$1" "$url/$1"
}
printf 'Downloading OmaRAW %s…\n' "$version"
listed() { awk -v name="$1" '$2 == name || $2 == "*" name {found=1} END {exit !found}' "$temporary/SHA256SUMS"; }
selected=0
if download SHA256SUMS; then
  # A release may add a build for a newer OpenEXR series, such as current Arch on
  # Omarchy edge. Use it when this system's OpenEXR matches; pacman checks the rest.
  if command -v pacman >/dev/null; then
    openexr=$(pacman -Q openexr 2>/dev/null | awk '{print $2}') || true
    [[ -n $openexr ]] || openexr=$(pacman -Si openexr 2>/dev/null | sed -n '1,/^Version/s/^Version *: *//p') || true
    if [[ $openexr =~ ^([0-9]+:)?([0-9]+\.[0-9]+) ]]; then
      alternative="omaraw-${package_version}-1-${architecture}-openexr${BASH_REMATCH[2]}.pkg.tar.zst"
      if listed "$alternative"; then package=$alternative; fi
    fi
  fi
  download "$package" && selected=1
fi
if ((selected == 0)); then
  printf 'Download failed. Check your connection and that release %s provides a %s package:\n' "$version" "$architecture" >&2
  printf '%s\n' \
    'https://github.com/28allday/omaraw/releases' >&2
  exit 1
fi
checksum=$(awk -v name="$package" '$2 == name || $2 == "*" name { print $1 }' "$temporary/SHA256SUMS")
[[ $checksum =~ ^[0-9a-fA-F]{64}$ ]] || {
  printf '%s\n' 'The checksum manifest has no unique valid entry for this package. Nothing was installed.' >&2; exit 1;
}
if ! (cd "$temporary" && printf '%s  %s\n' "$checksum" "$package" | sha256sum --check --status); then
  printf '%s\n' 'Package checksum mismatch. Nothing was installed.' >&2; exit 1
fi
if [[ -n $download_dir ]]; then
  mkdir -p -- "$download_dir"
  if [[ -e $download_dir/$package || -L $download_dir/$package ]]; then
    printf 'Destination already exists: %s\n' "$download_dir/$package" >&2; exit 1
  fi
  cp -- "$temporary/$package" "$download_dir/$package"
  printf 'Verified package saved: %s\n' "$download_dir/$package"
else
  printf '%s\n' 'Checking package dependencies against your configured repositories…'
  if ! pacman -Up --print-format '%n %v' "$temporary/$package" > "$temporary/transaction" 2> "$temporary/dependencies.log"; then
    cat "$temporary/dependencies.log" >&2
    printf '%s\n' \
      'OmaRAW cannot be installed with the dependencies currently available on this system.' \
      'Run a full Omarchy update, then retry. If dependencies are still unavailable, report the error at:' \
      'https://github.com/28allday/omaraw/issues' \
      'Include your Omarchy version and architecture. Nothing was installed.' >&2
    exit 1
  fi
  printf '%s\n' 'Checksum verified. Pacman will show the package and required dependencies.'
  if ((EUID == 0)); then pacman -U "$temporary/$package"
  else sudo pacman -U "$temporary/$package"; fi
  # The package already explained any library mismatch; skip what cannot start.
  if [[ -x /usr/lib/omaraw/check-libraries ]] && ! /usr/lib/omaraw/check-libraries --quiet; then
    exit 1
  fi
  if ((EUID != 0)) && command -v omaraw >/dev/null; then
    if omaraw skill --link >/dev/null; then
      printf '%s\n' 'Agent skill installed. Start a new agent session to use it.'
    else
      printf '%s\n' 'Existing custom skills were preserved. Check links with: omaraw skill --link' >&2
    fi
  fi
  printf '%s\n' 'Installation complete. Open OmaRAW from your application menu.'
fi
