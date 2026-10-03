#pragma once
#include <QString>
class QGuiApplication;
class QQuickWindow;
namespace oma::gpu {
// Call before loading application data or starting any service. Returns -1
// during a normal launch, otherwise the exit status of the isolated probe.
int quickProbeMain(QGuiApplication &);
// Call before constructing any Quick window. Respects explicit Qt backend
// overrides; automatic uses a probed Vulkan device, then Qt's software path.
// Also observes newly shown Quick windows, including controls' popup windows,
// and attaches graceful renderer-error recovery before their first frame.
QString selectQuickBackend(QGuiApplication &);
// Reports renderer creation failures as a graceful application exit. The app
// must unwind its services and locks before calling restartQuickSoftware.
constexpr int QuickSoftwareRestart = 73;
void guardQuickWindow(QQuickWindow *);
int restartQuickSoftware(int argc, char **argv);
}
