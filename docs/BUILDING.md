# Building OmaRAW

On Arch Linux or Omarchy, install the build tools and clone the source:

```sh
sudo pacman -S --needed base-devel git python
git clone https://github.com/28allday/omaraw.git
cd omaraw
./bin/install --build
```

This builds the application and its pinned processing engine, then installs the
result with pacman. Building the engine takes several minutes. The package
includes the user guide, agent skill, processing data and licence notices.

To produce the package without installing it:

```sh
./bin/package --syncdeps
```

Packages, matching complete source and `SHA256SUMS` are written to
`build-package/`. `OMARAW_BUILD_JOBS=4` limits build parallelism;
`OMARAW_PACKAGE_DIR=/path/to/output` changes that directory. Package preparation
uses Git-tracked files, so stage new source files before building changes.

The release's complete `.src.tar.gz` bundle also contains the pinned engine and
submodule archives. Extract it, enter its `omaraw` directory and run
`makepkg --syncdeps` to rebuild. Normal system build dependencies are required;
no sibling application or private development checkout is needed.

## ARM64 and Omarchy Mac candidates

The same recipe supports native `x86_64` and `aarch64` builds. The ARM target is
Arch Linux ARM, including Omarchy Mac on Asahi Alarm for M1/M2. Build inside a
matching Arch userspace; an Ubuntu ARM runner alone is not that environment.
ARM packages remain candidates until tested on an actual supported Mac.

`pkgbuild/ai-sources.json` retains the x86 dependency lock and shared models.
`pkgbuild/ai-sources-aarch64.json` replaces only architecture-specific wheels
and the private Python interpreter. Its replacements identify the exact x86
input they accompany, so changing a shared dependency requires updating the
ARM lock too. Both packages include the complete offline AI models and runtimes.
The ARM interpreter is pinned from the Arch Linux ARM build system; its package
signature was checked against key `68B3537F39A313B3E574D06777193F152BDBE6A6`.

ARM uses baseline ARMv8 instructions and 64 KiB ELF load alignment, compatible
with Asahi's 16 KiB pages. The package validator checks all bundled ELF files,
including the Python wheels. Native Mac testing still needs to cover the Asahi
kernel, GPU drivers, AI acceleration and full-resolution RAW workloads.

The **Package candidates** GitHub Actions workflow builds both architectures
on separate native hosted runners using signed Arch root filesystems. It runs
on the `arm64-support` branch and can be dispatched manually. Its repository
permissions are read-only: it uploads candidate artifacts and logs, and cannot
replace release assets or publish a release. Check both jobs before promoting
a candidate. Validation includes fully decoded exports in all seven formats,
loading the five bundled AI models, a real editable AI selection and GUI startup
with networking disabled. Hosted runners do not qualify Apple GPU behavior.

The x86 complete-source filename stays unchanged. The ARM complete-source bundle
adds `-aarch64` before `.src.tar.gz`, so both can be attached to one release.
Combine the two verified checksum manifests when preparing that release.
The installer selects the matching architecture; a release without that asset
fails without installing a different architecture's package.

Build success does not change the licences of bundled code or data. Retain the
[third-party notices](../THIRD_PARTY.md) when distributing modified versions.
