# OmaRAW's own GPU engine. Each Oma application is standalone and carries
# its own; nothing here searches for another application or checkout.
OMARAW_GPU_VULKAN_PREFIX = $$(OMA_VULKAN_PREFIX)
isEmpty(OMARAW_GPU_VULKAN_PREFIX):exists($$PWD/build/deps/vulkan/usr/include/vulkan/vulkan.h): INCLUDEPATH += $$PWD/build/deps/vulkan/usr/include
include(gpu/oma_gpu.pri)
include(gpu/oma_gpu_quick.pri)
