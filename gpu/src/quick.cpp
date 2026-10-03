#include <oma/gpu/quick.h>
#include <QGuiApplication>
#include <QProcess>
#include <QQuickWindow>
#include <QQuickRenderControl>
#include <QQuickGraphicsConfiguration>
#include <QDebug>
#include <QEvent>
#include <atomic>
#include <cstdio>
#if QT_CONFIG(vulkan) && __has_include(<vulkan/vulkan.h>)
#define OMA_QUICK_VULKAN 1
#include <QVulkanInstance>
#else
#define OMA_QUICK_VULKAN 0
#endif
#ifdef Q_OS_UNIX
#include <unistd.h>
#endif
namespace oma::gpu {
namespace {
std::atomic_bool recoveryRequested{false};
std::atomic_int recoveryExitCode{0};
QtMessageHandler previousMessageHandler = nullptr;

void requestRecovery(const QString &reason) {
    auto *app = QCoreApplication::instance();
    if (!app || recoveryRequested.exchange(true)) return;
    // Renderer diagnostics can arrive on a render thread while the GUI thread
    // is waiting for it. Defer teardown until Qt has completed that frame.
    QMetaObject::invokeMethod(app, [reason] {
        const bool software = QQuickWindow::graphicsApi() == QSGRendererInterface::Software
            || QQuickWindow::sceneGraphBackend() == QStringLiteral("software")
            || qgetenv("OMA_GPU_UI") == "software";
        const int code = software ? 1 : QuickSoftwareRestart;
        recoveryExitCode.store(code);
        qWarning().noquote() << "Qt interface renderer failed:" << reason
                            << (software ? "Exiting." : "Restarting with CPU processing and software rendering.");
        QCoreApplication::exit(code);
    }, Qt::QueuedConnection);
}

void rendererMessage(QtMsgType type, const QMessageLogContext &context, const QString &message) {
    // Qt 6's render loops log swapchain failures without emitting
    // sceneGraphError (that signal covers context initialisation only).
    if ((type == QtWarningMsg || type == QtCriticalMsg)
        && (message == QStringLiteral("Failed to build or resize swapchain")
            || message == QStringLiteral("Failed to create new swapchain")))
        requestRecovery(message);
    if (previousMessageHandler) previousMessageHandler(type, context, message);
    else {
        const QByteArray formatted = qFormatLogMessage(type, context, message).toLocal8Bit();
        std::fprintf(stderr, "%s\n", formatted.constData());
    }
}

class WindowRecoveryFilter final : public QObject {
public:
    explicit WindowRecoveryFilter(QObject *parent) : QObject(parent) {
        recoveryRequested.store(false); recoveryExitCode.store(0);
        previousMessageHandler = qInstallMessageHandler(rendererMessage);
    }
    ~WindowRecoveryFilter() override {
        const auto active = qInstallMessageHandler(previousMessageHandler);
        // A consumer can install its own handler after selecting the backend.
        if (active != rendererMessage) qInstallMessageHandler(active);
    }
    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() == QEvent::Show)
            if (auto *window = qobject_cast<QQuickWindow *>(object)) guardQuickWindow(window);
        return QObject::eventFilter(object, event);
    }
};
}
int quickProbeMain(QGuiApplication &app) {
    if (!app.arguments().contains(QStringLiteral("--oma-gpu-quick-probe"))) return -1;
#if OMA_QUICK_VULKAN
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);
    QVulkanInstance instance;
    instance.setExtensions(QQuickGraphicsConfiguration::preferredInstanceExtensions());
    instance.setApiVersion(QVersionNumber(1, 1));
    if (!instance.create()) return 1;
    QQuickRenderControl control;
    QQuickWindow window(&control);
    window.setVulkanInstance(&instance);
    bool error = false;
    QObject::connect(&window, &QQuickWindow::sceneGraphError, &window,
        [&](QQuickWindow::SceneGraphError, const QString &) { error = true; }, Qt::DirectConnection);
    const bool ready = control.initialize();
    control.invalidate();
    return ready && !error ? 0 : 1;
#else
    return 1;
#endif
}
QString selectQuickBackend(QGuiApplication &app) {
    if (!app.property("_omaGpuWindowRecovery").toBool()) {
        app.installEventFilter(new WindowRecoveryFilter(&app));
        app.setProperty("_omaGpuWindowRecovery", true);
    }
    const auto preference = qgetenv("OMA_GPU_UI").toLower();
    const auto qtBackend = qgetenv("QT_QUICK_BACKEND").toLower();
    const auto rhi = qgetenv("QSG_RHI_BACKEND").toLower();
    QString reason;
    if (preference == "software" || qtBackend == "software") reason = QStringLiteral("Software interface requested");
    else if (!qtBackend.isEmpty() || (!rhi.isEmpty() && rhi != "vulkan"))
        return QStringLiteral("Using the explicitly selected Qt interface backend");
    else if (preference != "vulkan" && (app.platformName() == "offscreen" || app.platformName() == "minimal"))
        reason = QStringLiteral("Software interface for the headless platform");
    else {
        QProcess probe;
        probe.setProcessChannelMode(QProcess::MergedChannels);
        probe.start(app.applicationFilePath(), {QStringLiteral("--oma-gpu-quick-probe")});
        const bool finished = probe.waitForStarted(1000) && probe.waitForFinished(4000);
        if (finished && probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0) {
            // MangoHud 0.8.4's overlay has shared ImGui state that can race
            // when separate Qt window render threads present concurrently.
            // Keep Vulkan and the overlay, but serialize Quick rendering in
            // this process. Never override an explicit Qt render-loop choice.
            const bool serialOverlay = qgetenv("MANGOHUD") == "1"
                && qgetenv("DISABLE_MANGOHUD") != "1" && qEnvironmentVariableIsEmpty("QSG_RENDER_LOOP");
            if (serialOverlay) qputenv("QSG_RENDER_LOOP", "basic");
            QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);
            return QStringLiteral("Vulkan interface; Qt device initialisation passed")
                + (serialOverlay ? QStringLiteral("; serial Qt rendering for MangoHud compatibility") : QString());
        }
        if (!finished) { probe.kill(); probe.waitForFinished(1000); }
        reason = QStringLiteral("Vulkan interface unavailable; using software rendering");
    }
    QQuickWindow::setSceneGraphBackend(QStringLiteral("software"));
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    return reason;
}
void guardQuickWindow(QQuickWindow *window) {
    if (!window || window->property("_omaGpuRecoveryGuarded").toBool()) return;
    window->setProperty("_omaGpuRecoveryGuarded", true);
    QObject::connect(window, &QQuickWindow::sceneGraphError, window,
        [](QQuickWindow::SceneGraphError, const QString &reason) { requestRecovery(reason); }, Qt::DirectConnection);
}
int quickRecoveryExitCode() { return recoveryExitCode.load(); }
int restartQuickSoftware(int argc, char **argv) {
    Q_UNUSED(argc);
    qputenv("OMA_GPU_UI", "software");
    qputenv("QT_QUICK_BACKEND", "software");
    qputenv("OMA_GPU", "cpu");
    qputenv("OMA_GPU_RECOVERED", "1");
    qunsetenv("QSG_RHI_BACKEND");
#ifdef Q_OS_UNIX
    execvp(argv[0], argv);
    qWarning("Could not restart the interface with software rendering");
    return 1;
#else
    QStringList args;
    for (int i=1;i<argc;++i) args << QString::fromLocal8Bit(argv[i]);
    return QProcess::startDetached(QString::fromLocal8Bit(argv[0]), args) ? 0 : 1;
#endif
}
}
