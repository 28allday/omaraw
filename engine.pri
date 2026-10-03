# Development defaults to a sibling checkout. Packages supply separate
# source, build, staged install and final runtime paths (docs/BUILDING.md).
DT_SRC = $$(OMARAW_DT_SRC)
DT_BUILD = $$(OMARAW_DT_BUILD)
DT_INSTALL = $$(OMARAW_DT_INSTALL)
# bin/package builds the pinned engine with OmaRAW's OCIO adapter. Prefer
# that matching private engine over an unpatched sibling development build.
PACKAGE_DT = $$PWD/build-package/src/darktable-281f60c9957b05b9379084b272c81a5e62fa46e3
isEmpty(DT_SRC):exists($$PACKAGE_DT/src/common/omaraw_ocio.h) {
    DT_SRC = $$PACKAGE_DT
    isEmpty(DT_BUILD): DT_BUILD = $$PWD/build-package/src/engine-build
    isEmpty(DT_INSTALL): DT_INSTALL = $$PWD/build-package/src/engine-stage/usr/lib/omaraw/engine
}
isEmpty(DT_SRC): DT_SRC = $$PWD/../darktable
isEmpty(DT_BUILD): DT_BUILD = $$DT_SRC/_build
isEmpty(DT_INSTALL): DT_INSTALL = $$DT_SRC/_install
DT_RUNTIME_PREFIX = $$(OMARAW_DT_RUNTIME_PREFIX)
isEmpty(DT_RUNTIME_PREFIX): DT_RUNTIME_PREFIX = $$DT_INSTALL
exists($$DT_INSTALL/lib/darktable/libdarktable.so) {
    message(darktable engine: $$DT_INSTALL; runtime: $$DT_RUNTIME_PREFIX)
    DEFINES += OMARAW_ENGINE OMARAW_DT_PREFIX=\\\"$$DT_RUNTIME_PREFIX\\\"
    SOURCES += $$PWD/src/engine/bridge.c
    DT_CFLAGS = $$system(python3 $$shell_quote($$PWD/tools/dt-flags.py) $$shell_quote($$DT_SRC) cflags $$shell_quote($$DT_BUILD))
    isEmpty(DT_CFLAGS): error(Cannot read the matching darktable compiler flags)
    QMAKE_CFLAGS += $$DT_CFLAGS -std=gnu99 -fopenmp -Wno-unused-parameter
    PKGCONFIG += glib-2.0 gmodule-2.0 sqlite3 lensfun
    LIBS += -L$$DT_INSTALL/lib/darktable -ldarktable -Wl,-rpath,$$DT_RUNTIME_PREFIX/lib/darktable -fopenmp
} else {
    contains(CONFIG, require_engine): error(A package requires a built darktable engine at $$DT_INSTALL)
    message(darktable engine: not found at $$DT_INSTALL — building previews-only)
}

HEADERS += $$PWD/src/engine/developtools.inc

HEADERS += $$PWD/src/engine/smartpreview.inc

HEADERS += $$PWD/src/engine/repair.inc
HEADERS += $$PWD/src/imagematchworker.inc
