# System OCIO in packages; an optional private prefix for development.
OMARAW_OCIO_PREFIX = $$(OMARAW_OCIO_PREFIX)
isEmpty(OMARAW_OCIO_PREFIX):!packagesExist(OpenColorIO):exists($$PWD/build/deps/ocio/usr/include/OpenColorIO/OpenColorIO.h): OMARAW_OCIO_PREFIX = $$PWD/build/deps/ocio/usr
!isEmpty(OMARAW_OCIO_PREFIX) {
    INCLUDEPATH += $$OMARAW_OCIO_PREFIX/include
    LIBS += -L$$OMARAW_OCIO_PREFIX/lib -lOpenColorIO
    # The packaged engine has its own RUNPATH. Load this optional private
    # dependency directly so development also works without system potrace.
    exists($$OMARAW_OCIO_PREFIX/lib/libpotrace.so): LIBS += -Wl,--no-as-needed -lpotrace -Wl,--as-needed
    # Private OCIO's dependencies must resolve from the same prefix as well.
    QMAKE_LFLAGS += -Wl,--disable-new-dtags
    QMAKE_RPATHDIR += $$OMARAW_OCIO_PREFIX/lib
} else {
    # packagesExist treats whitespace-separated version constraints as packages.
    OMARAW_OCIO_PKG_CONFIG = $$pkgConfigExecutable()
    !system($$OMARAW_OCIO_PKG_CONFIG --atleast-version=2.5 OpenColorIO): error(OpenColorIO 2.5 or newer is required. Install the opencolorio package.)
    PKGCONFIG += OpenColorIO
}
HEADERS += $$PWD/src/colourpipeline.h $$PWD/src/colourbridge.h
SOURCES += $$PWD/src/colourpipeline.cpp $$PWD/src/texturegpu.cpp $$PWD/src/chaingpu.cpp
HEADERS += $$PWD/src/texturebridge.h
RESOURCES += $$PWD/colour/colour.qrc
