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

Build success does not change the licences of bundled code or data. Retain the
[third-party notices](../THIRD_PARTY.md) when distributing modified versions.
