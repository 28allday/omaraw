#!/usr/bin/env bash
# Download a beta/release package, verify its checksum, then use pacman.
set -euo pipefail
version=0.1.0-beta.7
download_dir=
usage() {
  printf '%s\n' 'Usage: bash install.sh [--version VERSION] [--download-only DIRECTORY]' \
    'Installs the matching Arch/Omarchy x86_64 or aarch64 package from a GitHub release.'
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
if ! download SHA256SUMS || ! download "$package"; then
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
  printf '%s\n' 'Checksum verified. Pacman will show the package and required dependencies.'
  if ((EUID == 0)); then pacman -U "$temporary/$package"
  else sudo pacman -U "$temporary/$package"; fi
  if ((EUID != 0)) && command -v omaraw >/dev/null; then
    if omaraw skill --link >/dev/null; then
      printf '%s\n' 'Agent skill installed. Start a new agent session to use it.'
    else
      printf '%s\n' 'Existing custom skills were preserved. Check links with: omaraw skill --link' >&2
    fi
  fi
  printf '%s\n' 'Installation complete. Open OmaRAW from your application menu.'
fi
