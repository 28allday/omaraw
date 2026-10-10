# Building OmaRAW

On Omarchy, install the build tools and clone the source:

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

## ARM64 and Omarchy Mac

The same recipe supports native `x86_64` and `aarch64` builds. The ARM target is
Omarchy Mac on Asahi Alarm for M1/M2. Build inside a
matching Arch userspace; an Ubuntu ARM runner alone is not that environment.
Beta 7 includes both architectures. ARM package and hardware checks were run on
an M2 Mac with Asahi's 16 KiB pages; future changes still need hardware testing.

`pkgbuild/ai-sources.json` retains the x86 dependency lock and shared models.
`pkgbuild/ai-sources-aarch64.json` replaces only architecture-specific wheels
and the private Python interpreter. Its replacements identify the exact x86
input they accompany, so changing a shared dependency requires updating the
ARM lock too. Both packages include the complete offline AI models and runtimes.
The ARM interpreter is pinned from the Arch Linux ARM build system; its package
signature was checked against key `68B3537F39A313B3E574D06777193F152BDBE6A6`.

ARM uses baseline ARMv8 instructions and 64 KiB ELF load alignment, compatible
with Asahi's 16 KiB pages. The package validator checks all bundled ELF files,
including the Python wheels. Native Mac testing covers the Asahi kernel, GPU
drivers, AI acceleration and full-resolution RAW workloads. Texture-heavy
exports remain a separate performance limitation on both architectures.
On Asahi, the AI workers enable WebGPU discovery through ORT's CPU device entry
because its Linux hardware enumeration misses Apple's platform GPU. Dawn still
selects the Vulkan adapter; denoise retains its normal CPU comparison and fallback.
This setting is limited to Linux ARM64 machines with an Apple device tree.

The **Package candidates** GitHub Actions workflow builds both architectures
on separate native hosted runners using signed Arch root filesystems. It runs
on `master` and the `arm64-support` staging branch, and can be dispatched manually. Its repository
permissions are read-only: it uploads candidate artifacts and logs, and cannot
replace release assets or publish a release. Check both jobs before promoting
a candidate. Validation includes fully decoded exports in all seven formats,
loading the five bundled AI models, a real editable AI selection and GUI startup
with networking disabled. Hosted runners do not qualify Apple GPU behavior.

The main x86 builder synchronises the entire disposable root with Omarchy's
stable mirror, including downgrades from a newer bootstrap. A second x86 job
builds against current Arch, as used by Omarchy edge, whose libraries can be a
newer series. Its package and complete-source names add that OpenEXR series,
such as `-openexr3.5`, so both x86 packages can be attached to one release; the
installer chooses the one matching the system's OpenEXR. The ARM builder uses the
Arch Linux ARM repositories used by Omarchy Mac. Package dependencies record
the OpenEXR, OpenJPH and Imath versions used by each builder, as minimums only:
an upper bound would block users' whole system update whenever a repository
moves to a newer library series. Instead the package installs
`/usr/lib/omaraw/check-libraries` and a pacman hook that runs it after any
transaction changing a system library, and after OmaRAW is installed or
upgraded. If OmaRAW's libraries are missing it prints how to install the
matching build; it never fails the transaction. The **Library watch** workflow
compares the latest release with each builder's repositories daily and opens
an issue when one has moved to a series no release package was built for, so a
rebuilt release can follow. Validation also installs
standalone darktable and checks that OmaRAW still loads its own private engine.
Repository URLs and package versions are retained with the build logs.

The x86 complete-source filename stays unchanged. The ARM complete-source bundle
adds `-aarch64` before `.src.tar.gz`, so both can be attached to one release.
Combine the two verified checksum manifests when preparing that release.
The workflow explicitly selects `.pkg.tar.zst` binaries and `.src.tar.gz` sources,
so Arch Linux ARM's different default compression cannot change asset names.
The installer selects the matching architecture; a release without that asset
fails without installing a different architecture's package. A release without
an x86 package for the system's OpenEXR series falls back to the main x86
package, which pacman then accepts or refuses on its dependencies.

Build success does not change the licences of bundled code or data. Retain the
[third-party notices](../THIRD_PARTY.md) when distributing modified versions.
