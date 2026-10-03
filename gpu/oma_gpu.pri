# The exact same source is compiled into each standalone app. Install OCIO,
# libplacebo and Vulkan development packages, or set private build prefixes.
CONFIG += c++17 link_pkgconfig
INCLUDEPATH += $$PWD/include
HEADERS += $$PWD/include/oma/gpu/pipeline.h $$PWD/src/texture.h
SOURCES += $$PWD/src/pipeline.cpp
contains(CONFIG, oma_gpu_cpu_only) {
    DEFINES += OMA_GPU_WITH_VULKAN=0
} else {
    DEFINES += OMA_GPU_WITH_VULKAN=1
    PKGCONFIG += libplacebo vulkan
    OMA_VULKAN_PREFIX = $$(OMA_VULKAN_PREFIX)
    !isEmpty(OMA_VULKAN_PREFIX): INCLUDEPATH += $$OMA_VULKAN_PREFIX/include
}
# OCIO is a public dependency and is configured by the application, which
# may use its own private prefix. No sibling application is searched here.
LIBS += -pthread
