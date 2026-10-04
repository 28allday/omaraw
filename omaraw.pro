QT += core gui qml quick quickcontrols2 sql dbus multimedia
HEADERS += src/launchscreen.h src/offlineqml.h
SOURCES += src/launchscreen.cpp
CONFIG += c++17 release link_pkgconfig
HEADERS += src/resourcesettings.h src/resourcebudget.h
SOURCES += src/resourcesettings.cpp
HEADERS += src/catalogmaintenance.h src/colourrange.h
SOURCES += src/catalogmaintenance.cpp
PKGCONFIG += libraw exiv2 libgphoto2 libheif lcms2 sqlite3 zlib libtiff-4
TARGET = omaraw
TEMPLATE = app
OMARAW_BASE_VERSION = $$cat($$PWD/VERSION)
# Optional local build suffix; release packages use VERSION unchanged.
isEmpty(OMARAW_RELEASE): DEFINES += OMARAW_VERSION=\\\"$$OMARAW_BASE_VERSION\\\"
else: DEFINES += OMARAW_VERSION=\\\"$$OMARAW_BASE_VERSION-$$OMARAW_RELEASE\\\"

HEADERS += \
    src/snapshotpreview.h \
    src/offlinestore.h src/offlinebridge.h \
    src/displaycolour.h \
    src/desktoptheme.h \
    src/catalogbackup.h \
    src/catalogchooser.h \
    src/assetmodel.h \
    src/backend.h \
    src/catalog.h \
    src/engineservice.h \
    src/headless.h \
    src/importer.h \
    src/metadata.h \
    src/metadataexport.h \
    src/finishing.h \
    src/psdwriter.h \
    src/sheets.h \
    src/maskpreview.h \
    src/proof.h \
    src/captureservice.h \
    src/capture/gphoto.h \
    src/thumbnailer.h

SOURCES += \
    src/snapshotpreview.cpp \
    src/offlinestore.cpp \
    src/displaycolour.cpp \
    src/desktoptheme.cpp \
    src/catalogbackup.cpp \
    src/catalogchooser.cpp \
    src/main.cpp \
    src/headless.cpp \
    src/assetmodel.cpp \
    src/backend.cpp \
    src/catalog.cpp \
    src/engineservice.cpp \
    src/importer.cpp \
    src/metadata.cpp \
    src/finishing.cpp \
    src/psdwriter.cpp \
    src/sheets.cpp \
    src/maskpreview.cpp \
    src/proof.cpp \
    src/captureservice.cpp \
    src/capture/gphoto.c \
    src/thumbnailer.cpp

RESOURCES += src/resources.qrc
RESOURCES += src/camera-profiles.qrc
HEADERS += src/curatedpresets.h
SOURCES += src/curatedpresets.cpp
HEADERS += src/aiservice.h src/airuntime.h
HEADERS += src/denoiseservice.h
SOURCES += src/denoiseservice.cpp
SOURCES += src/aiservice.cpp

include(engine.pri)

include(colour.pri)
HEADERS += src/imagematchmath.h src/imagematch.h src/imagematchservice.h
SOURCES += src/imagematch.cpp src/imagematchservice.cpp
HEADERS += src/exportstate.h
HEADERS += src/labcolour.h
HEADERS += src/toneregions.h
SOURCES += src/exportstate.cpp

HEADERS += src/capture/download.h src/capture/transport.h
SOURCES += src/capture/download.cpp

include(gpu.pri)

HEADERS += src/xmppreset.h
SOURCES += src/xmppreset.cpp
HEADERS += src/cameraprofiles.h
SOURCES += src/cameraprofiles.cpp
HEADERS += src/camerastyles.h
SOURCES += src/camerastyles.cpp

HEADERS += src/developtools.h
HEADERS += src/developscopes.h
HEADERS += src/falsecolour.h

HEADERS += src/creativeprofilelut.h
SOURCES += src/creativeprofiles.cpp

HEADERS += src/creativeprofiledata.h src/enhancedprofile.h
SOURCES += src/enhancedprofile.cpp

# Offline subject detection: accept current OpenCV and the supported 4.x branch.
packagesExist(opencv5): AUTOTAG_OPENCV = opencv5
else: AUTOTAG_OPENCV = opencv4
QMAKE_CXXFLAGS += $$system(pkg-config --cflags $$AUTOTAG_OPENCV)
LIBS += $$system(pkg-config --libs-only-L $$AUTOTAG_OPENCV) -lopencv_dnn -lopencv_imgproc -lopencv_core
HEADERS += src/autotag.h
SOURCES += src/autotag.cpp src/autotagcatalog.cpp src/autotagbackend.cpp
RESOURCES += src/autotag.qrc

HEADERS += src/headlesscapture.h
SOURCES += src/headlesscapture.cpp

HEADERS += src/exposuremeter.h

HEADERS += src/presetstate.h

contains(CONFIG, local_tools):exists($$PWD/local-tools.pri): include($$PWD/local-tools.pri)
