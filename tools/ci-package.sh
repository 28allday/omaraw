#!/usr/bin/env bash
# Native package candidates in a disposable, signed Arch root filesystem.
# This workflow has read-only GitHub permissions and never publishes releases.
set -euo pipefail
[[ ${GITHUB_ACTIONS:-} == true && ${RUNNER_ENVIRONMENT:-} == github-hosted ]] || {
  echo 'Run this helper only on a disposable GitHub-hosted runner.' >&2; exit 1;
}
architecture=${1:?Expected x86_64 or aarch64}
[[ $(uname -m) == "$architecture" ]] || { echo 'A native runner is required.' >&2; exit 1; }
case "$architecture" in
  x86_64)
    rootfs_url=https://geo.mirror.pkgbuild.com/iso/2026.10.01/archlinux-bootstrap-x86_64.tar.zst
    fingerprint=3E80CA1A8B89F69CBA57D98A76A5EF9054449A5C
    keyring=archlinux
    strip=1
    # Match the repositories available to Omarchy stable installations.
    mirror='https://stable-mirror.omarchy.org/$repo/os/$arch'
    ;;
  aarch64)
    rootfs_url=https://fl.us.mirror.archlinuxarm.org/os/ArchLinuxARM-aarch64-latest.tar.gz
    fingerprint=68B3537F39A313B3E574D06777193F152BDBE6A6
    keyring=archlinuxarm
    strip=0
    mirror='https://fl.us.mirror.archlinuxarm.org/$arch/$repo'
    ;;
  *) echo 'Unsupported architecture' >&2; exit 1 ;;
esac
source_root=$(git rev-parse --show-toplevel)
output="$source_root/dist"
mkdir -p "$output/logs"
exec > >(tee "$output/logs/build.log") 2>&1
free_kib=$(df -Pk "$RUNNER_TEMP" | awk 'NR == 2 {print $4}')
(( free_kib >= 20 * 1024 * 1024 )) || { echo 'At least 20 GiB free is required.'; exit 1; }
work=$(mktemp -d "$RUNNER_TEMP/omaraw-package.XXXXXXXX")
rootfs="$work/root"
collect_logs() {
  if [[ -d $rootfs/work/report ]]; then
    sudo cp -a "$rootfs/work/report/." "$output/logs/" || true
  fi
  sudo chown -R "$(id -u):$(id -g)" "$output" || true
}
trap collect_logs EXIT
mkdir -p "$rootfs" "$work/keys"
chmod 700 "$work/keys"
curl --fail --location --proto '=https' --retry 3 -o "$work/rootfs.tar" "$rootfs_url"
curl --fail --location --proto '=https' --retry 3 -o "$work/rootfs.sig" "$rootfs_url.sig"
curl --fail --location --proto '=https' --retry 3 -o "$work/key.asc" \
  "https://keyserver.ubuntu.com/pks/lookup?op=get&search=0x$fingerprint"
gpg --batch --homedir "$work/keys" --import "$work/key.asc"
gpg --batch --homedir "$work/keys" --status-fd 1 --verify "$work/rootfs.sig" "$work/rootfs.tar" > "$output/logs/rootfs-signature.txt"
awk -v key="$fingerprint" '$1 == "[GNUPG:]" && $2 == "VALIDSIG" && ($3 == key || $NF == key) {ok=1} END {exit !ok}' "$output/logs/rootfs-signature.txt"
sha256sum "$work/rootfs.tar" > "$output/logs/rootfs-sha256.txt"
sudo tar --extract --file "$work/rootfs.tar" --directory "$rootfs" --strip-components="$strip" --numeric-owner
sudo chown root:root "$rootfs"
sudo mkdir -p "$rootfs/work/source" "$rootfs/work/report"
# Export only tracked files. Recreate minimal Git metadata for the package
# snapshot; no checkout credentials, host files or private development tools.
git archive HEAD | sudo tar -xf - -C "$rootfs/work/source"
revision=$(git rev-parse HEAD)
epoch=$(git log -1 --format=%ct)
printf 'Server = %s\n' "$mirror" | sudo tee "$rootfs/etc/pacman.d/mirrorlist" >/dev/null
nspawn=(sudo systemd-nspawn --directory="$rootfs" --register=no --console=pipe
  --resolv-conf=bind-host --system-call-filter='landlock_create_ruleset landlock_add_rule landlock_restrict_self')
"${nspawn[@]}" /bin/bash -euc '
  pacman-key --init
  pacman-key --populate "$1"
  # The bootstrap can be newer than the Omarchy snapshot. Downgrade it too,
  # inside this disposable root only, before compiling against its libraries.
  pacman -Syuu --needed --noconfirm base-devel git python python-pip imagemagick
  pacman -Q > /work/report/build-packages.txt
  cp /etc/pacman.d/mirrorlist /work/report/build-mirrorlist.txt
  useradd --create-home --uid 2000 builder
  printf "builder ALL=(ALL) NOPASSWD: /usr/bin/pacman\n" > /etc/sudoers.d/omaraw-builder
  chmod 440 /etc/sudoers.d/omaraw-builder
  chown -R builder:builder /work
' bash "$keyring"
# Keep the original tested commit and source identity instead of inventing a
# commit on the runner. The bundle contains only the history reachable from HEAD.
git bundle create "$work/source.bundle" HEAD
sudo cp "$work/source.bundle" "$rootfs/work/source.bundle"
"${nspawn[@]}" runuser -u builder -- /bin/bash -euc '
  cd /work/source
  git init --initial-branch=build
  git fetch /work/source.bundle HEAD
  git reset --mixed FETCH_HEAD
  test "$(git rev-parse HEAD)" = "$1"
  test -z "$(git status --porcelain)"
  export OMARAW_BUILD_JOBS=2 OMP_NUM_THREADS=2 OMP_THREAD_LIMIT=2 SOURCE_DATE_EPOCH="$2"
  export PKGEXT=.pkg.tar.zst SRCEXT=.src.tar.gz
  ./bin/package --syncdeps --noconfirm
' bash "$revision" "$epoch"
"${nspawn[@]}" /bin/bash -euc '
  package=(/work/source/build-package/omaraw-*.pkg.tar.zst)
  test "${#package[@]}" = 1
  # Exercise coexistence with the standalone engine, not just a clean root.
  pacman -S --needed --noconfirm darktable
  pacman -U --noconfirm "${package[0]}"
  pacman -Q > /work/report/system-packages.txt
  cd /work/source/build-package
  sha256sum --check SHA256SUMS
'
# Actual installed binaries, a new profile and no network or physical GPU.
"${nspawn[@]}" --private-network runuser -u builder -- \
  python /work/source/tools/validate-install.py /work/report/validation
mkdir -p "$output/packages"
sudo cp -a "$rootfs/work/report/." "$output/logs/"
sudo cp "$rootfs/work/source/build-package/"*.pkg.tar.zst \
  "$rootfs/work/source/build-package/"*.src.tar.gz \
  "$rootfs/work/source/build-package/SHA256SUMS" "$output/packages/"
sudo chown -R "$(id -u):$(id -g)" "$output"
printf 'Verified %s candidate from %s; no release was published.\n' "$architecture" "$revision" >> "$GITHUB_STEP_SUMMARY"
