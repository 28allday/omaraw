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
// Reports renderer creation and swapchain failures as a graceful app exit. The app
// must unwind its services and locks before calling restartQuickSoftware.
constexpr int QuickSoftwareRestart = 73;
void guardQuickWindow(QQuickWindow *);
// Preserves the requested exit across nested startup/chooser event loops.
// Zero means no renderer failure has requested an exit.
int quickRecoveryExitCode();
int restartQuickSoftware(int argc, char **argv);
}
