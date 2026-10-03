// OmaRAW — photography library, RAW development, tethered capture and
// output for Linux. Qt Quick UI over a C++ core; one binary.
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QEventLoop>
#include <QTimer>
#include <QUrl>
#include <QDebug>
#include <QElapsedTimer>
#include <QDir>
#include <QCommandLineParser>
#include "headless.h"
#include "autotag.h"
#include <cstring>
#include <QLockFile>
#include <QSettings>
#include <QSurfaceFormat>
#include <QColorSpace>
#include <QQuickItem>
#include <QScopeGuard>
#include <memory>
#include "offlinebridge.h"
#include <QFileInfo>

#include "backend.h"
#include "catalogbackup.h"
#include "catalogchooser.h"
#include "launchscreen.h"
#include "offlineqml.h"
#include "captureservice.h"
#include "engineservice.h"
#include "thumbnailer.h"
#include <QThreadPool>
#include "colourpipeline.h"
#include <oma/gpu/quick.h>
#include <QStandardPaths>
#include <QThread>
#include <cerrno>
#include <signal.h>
#include <QDir>

static int runApplication(int argc, char *argv[]) {
    if (argc == 2 && std::strcmp(argv[1], "--auto-tag-worker") == 0) {
        qputenv("QT_QPA_PLATFORMTHEME", "");
        qputenv("QT_QPA_PLATFORM", "offscreen");
        QGuiApplication app(argc, argv);
        return AutoTags::workerMain();
    }
    // Headless work still needs QtGui for images and colour, but no display.
    // The plugin has to be chosen before the application exists.
    const bool headless = Headless::requested(argc, argv);
    if (headless && !qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    const int probe = oma::gpu::quickProbeMain(app);
    if (probe >= 0) return probe;
    auto gpuLifetime = qScopeGuard([] { ColourPipeline::instance().releaseGpu(); });
    app.setOrganizationName(QStringLiteral("omaraw"));
    app.setApplicationName(QStringLiteral("omaraw"));
    app.setApplicationVersion(QStringLiteral(OMARAW_VERSION));
    app.setDesktopFileName(QStringLiteral("omaraw"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("omaraw")));
    qInfo().noquote() << oma::gpu::selectQuickBackend(app);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    // Tell a colour-aware Wayland compositor how to interpret the SDR
    // surface. Its monitor transform must not be applied a second time.
    QSurfaceFormat surface = QSurfaceFormat::defaultFormat();
    surface.setColorSpace(QColorSpace::SRgb);
    QSurfaceFormat::setDefaultFormat(surface);
    if (!ColourPipeline::instance().loadSettings()) {
        qCritical() << "Cannot initialise OpenColorIO:" << ColourPipeline::instance().error();
        return 1;
    }

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({QStringLiteral("catalog"), QStringLiteral("Open a catalog"), QStringLiteral("file")});
    // Set when one window relaunches itself onto another catalog: wait for
    // the locks the window that started us is dropping, instead of racing it.
    parser.addOption({QStringLiteral("await-catalog"), QStringLiteral("Wait for another window to release the catalog")});
    // The window that started us: the engine's own database locks name its
    // process and stay until that process has gone, after OmaRAW's locks.
    parser.addOption({QStringLiteral("await-exit"), QStringLiteral("Wait for the window that started this one to exit"), QStringLiteral("pid")});
    Headless::addOptions(parser);
    parser.addPositionalArgument(QStringLiteral("folder"), QStringLiteral("Folder to import"), QStringLiteral("[folder]"));
    // Headless, a mistyped option is answered the way everything else is:
    // one JSON object and exit 2, not a line of text a script cannot read.
    if (headless) {
        if (!parser.parse(app.arguments())) return Headless::fail(parser.errorText());
        if (parser.isSet(QStringLiteral("help"))) parser.showHelp(0);
        if (parser.isSet(QStringLiteral("version"))) parser.showVersion();
    } else parser.process(app);
    // Capture startup diagnostics too, and keep redirection alive until the
    // engine worker has finished during destruction of the services below.
    Headless::OutputGuard answer(headless);
    // A verb that needs no catalog answers before one is opened.
    if (headless) if (const int code = Headless::early(parser); code >= 0) return code;
    // Interactive starts wait for OK before opening a catalog.
    if (!headless && !qEnvironmentVariableIsSet("OMARAW_SKIP_LAUNCH") && qgetenv("OMA_GPU_RECOVERED") != "1") {
        const int result = LaunchScreen::run();
        if (result != LaunchScreen::Continue) return result;
    }
    Backend backend;
    backend.setRememberBrowsing(!headless);

    // Opening a catalog means taking its lock and its engine data's locks
    // before any migration or daily snapshot runs. Any of that can fail on a
    // catalog chosen long ago — an unplugged drive, another window holding
    // it, a newer schema — so a failure asks for another catalog instead of
    // exiting into a terminal nobody is watching.
    std::unique_ptr<QLockFile> catalogLock, libraryLock, configLock;
    QString catalogPath = parser.isSet(QStringLiteral("catalog"))
                          ? QFileInfo(parser.value(QStringLiteral("catalog"))).absoluteFilePath()
                          : Backend::defaultCatalogPath();
    QString config;
    // A window handing this one its catalog is still releasing the locks.
    const int lockWaitMs = parser.isSet(QStringLiteral("await-catalog")) ? 20000 : 0;
    // The engine refuses a lock whose process is still alive, and the window
    // handing over drops OmaRAW's locks before it has finished exiting, so
    // wait for the process itself before anything reaches the engine.
    if (const qint64 pid = parser.value(QStringLiteral("await-exit")).toLongLong(); pid > 0) {
        QElapsedTimer waited; waited.start();
        while (waited.elapsed() < lockWaitMs && (::kill(pid_t(pid), 0) == 0 || errno == EPERM))
            QThread::msleep(50);
    }
    // Asking at startup is a preference, so an explicit catalog on the
    // command line or in the environment is honoured without a question.
    bool ask = !headless && backend.askCatalogAtStartup() && !parser.isSet(QStringLiteral("catalog"))
               && !qEnvironmentVariableIsSet("OMARAW_CATALOG");
    // A catalog someone named — on the command line or in the chooser — must
    // already exist. Only the default location is created on demand, so a
    // catalog that has gone missing is reported instead of quietly reappearing
    // as an empty one.
    // A named catalog must already be there, so a typo is reported instead of
    // quietly becoming an empty one. Headless work has to be able to start a
    // shoot from nothing, so it may say it means to, and only then.
    bool mustExist = parser.isSet(QStringLiteral("catalog")) && !(headless && parser.isSet(QStringLiteral("new-catalog")));

    // Tries the current candidate; returns why it could not be used.
    auto tryOpen = [&]() -> QString {
        catalogLock.reset(); libraryLock.reset(); configLock.reset();
        const QFileInfo file(catalogPath);
        if (!file.isFile() && mustExist)
            return QCoreApplication::translate("main", "That catalog does not exist.");
        catalogPath = file.exists() ? file.canonicalFilePath() : file.absoluteFilePath();
        if (!QDir().mkpath(QFileInfo(catalogPath).absolutePath()))
            return QCoreApplication::translate("main", "Its folder cannot be created.");
        catalogLock = std::make_unique<QLockFile>(catalogPath + QStringLiteral(".omaraw-lock"));
        catalogLock->setStaleLockTime(0);
        if (!catalogLock->tryLock(lockWaitMs))
            return QCoreApplication::translate("main", "It is already open in another OmaRAW window.");
        // Catalogs in the same folder share an engine library, and legacy
        // catalogs also share engine-config/data.db. Lock both before any
        // migration or daily snapshot, not only when darktable starts later.
        config = CatalogBackup::engineConfig(catalogPath);
        if (!QDir().mkpath(config))
            return QCoreApplication::translate("main", "Its engine settings folder cannot be created.");
        libraryLock = std::make_unique<QLockFile>(CatalogBackup::engineLibrary(catalogPath) + QStringLiteral(".omaraw-lock"));
        configLock = std::make_unique<QLockFile>(QFileInfo(config).canonicalFilePath() + QStringLiteral("/.omaraw-lock"));
        libraryLock->setStaleLockTime(0); configLock->setStaleLockTime(0);
        if (!libraryLock->tryLock(lockWaitMs) || !configLock->tryLock(lockWaitMs))
            return QCoreApplication::translate("main", "Its Develop data is already open in another OmaRAW window.");
        // Library thumbnails can be requested before the engine has finished
        // starting. Load the durable source map before constructing the UI.
        oma_offline_configure(CatalogBackup::engineLibrary(catalogPath).toLocal8Bit().constData());
        if (!backend.openCatalog(catalogPath)) return backend.statusMessage();
        return QString();
    };

    for (;;) {
        const QString failure = ask ? QString() : tryOpen();
        if (!ask && failure.isEmpty()) break;
        catalogLock.reset(); libraryLock.reset(); configLock.reset();
        // Nobody is watching a terminal for a chooser to appear.
        if (headless) return Headless::fail(failure);
        CatalogChooser chooser(&backend, failure, failure.isEmpty() ? QString() : catalogPath);
        const QString chosen = chooser.run();
        // Quitting from the chooser is a normal exit when the user was only
        // being asked, and a failure when nothing could be opened.
        if (chosen.isEmpty()) return failure.isEmpty() ? 0 : 1;
        catalogPath = chosen;
        ask = false;
        mustExist = true;
    }

    // The darktable engine: its own config/cache/library under OmaRAW's
    // directories, never the user's darktable setup. OMARAW_DT_PREFIX at
    // build time, OMARAW_DT_PREFIX in the environment overrides.
    EngineService dtEngine;
    dtEngine.loadSettings();
    bool engineStarted = false;
    {
        QString prefix = QString::fromLocal8Bit(qgetenv("OMARAW_DT_PREFIX"));
#ifdef OMARAW_DT_PREFIX
        if (prefix.isEmpty()) prefix = QStringLiteral(OMARAW_DT_PREFIX);
#endif
        const QString data = QFileInfo(backend.catalogPath()).absolutePath();
        const bool recovered = config == data + QStringLiteral("/engine-config");
        const QString cache = recovered ? data + QStringLiteral("/cache/engine") : QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/engine");
        if (recovered) app.setProperty("omarawThumbnailCache", data + QStringLiteral("/cache/thumbs"));
        // Listing renders nothing: no engine to wait for. (An import may
        // bring edits in with the photos' sidecars, which the engine applies.)
        const QString verb = headless ? parser.positionalArguments().value(0) : QString();
        const bool needsEngine = verb != QLatin1String("list");
        if (!prefix.isEmpty() && !qEnvironmentVariableIsSet("OMARAW_NO_ENGINE") && needsEngine) {
            engineStarted = true;
            dtEngine.start(prefix, config, cache, data + QStringLiteral("/engine-library.db"));
        }
    }
    backend.setEngine(&dtEngine);

    // The work itself, with no interface and no camera behind it: the same
    // Backend and EngineService, driven by Headless and reporting in JSON.
    if (headless) {
        if (engineStarted) {
            // Rendering needs the engine ready; importing and listing do not.
            // (apply and inspect open photos in it; ops only reads names.)
            const QString verb = parser.positionalArguments().value(0);
            if (!dtEngine.ready() && (verb == QLatin1String("export") || verb == QLatin1String("apply") || verb == QLatin1String("inspect") || verb == QLatin1String("import"))) {
                QEventLoop wait;
                QObject::connect(&dtEngine, &EngineService::availableChanged, &wait, &QEventLoop::quit);
                QTimer::singleShot(120000, &wait, &QEventLoop::quit);
                if (!dtEngine.ready()) wait.exec();
            }
        }
        return Headless::run(parser, backend, dtEngine);
    }

    // Tethered capture over libgphoto2: its own worker, files land in the
    // session folder and go straight into the catalog.
    CaptureService capture;
    QObject::connect(&capture, &CaptureService::captured, &backend, &Backend::importCapture);
    if (!qEnvironmentVariableIsSet("OMARAW_NO_CAPTURE")) capture.start();

    QQmlApplicationEngine engine;
    OfflineQml::install(engine);
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, &app,
                     [](const QList<QQmlError> &warnings) {
        for (const QQmlError &w : warnings) qWarning().noquote() << w.toString();
    });
    engine.addImportPath(QStringLiteral(":/qml"));
    engine.addImageProvider(QStringLiteral("thumbs"), new Thumbnailer);
    // Maintain the chosen preview-cache limit throughout the session.
    backend.resources()->startMaintenance();
    engine.addImageProvider(QStringLiteral("engine"), new EngineImageProvider(&dtEngine));
    engine.addImageProvider(QStringLiteral("engine-detail"), new EngineDetailProvider(&dtEngine));
    engine.addImageProvider(QStringLiteral("engine-original"), new EngineOriginalProvider(&dtEngine));
    engine.addImageProvider(QStringLiteral("mask"), new MaskImageProvider(&dtEngine));
    engine.addImageProvider(QStringLiteral("clipping"), new ClippingImageProvider(&dtEngine));
    engine.addImageProvider(QStringLiteral("snapshots"), new SnapshotImageProvider(&backend));
    engine.addImageProvider(QStringLiteral("liveview"), new LiveViewProvider(&capture));
    engine.rootContext()->setContextProperty(QStringLiteral("capture"), &capture);
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    backend.watchHoldKeys(&app);
    engine.rootContext()->setContextProperty(QStringLiteral("assets"), backend.model());
    engine.rootContext()->setContextProperty(QStringLiteral("engine"), &dtEngine);
    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().isEmpty()) {
        qCritical() << "Could not load the OmaRAW interface.";
        return -1;
    }
    oma::gpu::guardQuickWindow(qobject_cast<QQuickWindow *>(engine.rootObjects().first()));
    DisplayColour::instance().setWindow(qobject_cast<QQuickWindow *>(engine.rootObjects().first()));

    // A folder on the command line imports in place ("omaraw ~/Pictures/job");
    // a photo (Open With from a file manager) opens in Develop.
    const QStringList args = parser.positionalArguments();
    if (!args.isEmpty()) backend.openFromCommandLine(args.first());

    if (qgetenv("OMA_GPU_RECOVERED") == "1")
        backend.setStatus(QCoreApplication::translate("main", "Recovered from a graphics failure. Using CPU processing and software rendering for this session."));
    const int recovery = oma::gpu::quickRecoveryExitCode();
    return recovery ? recovery : app.exec();
}

int main(int argc, char *argv[]) {
    // Unwind the catalogue locks and worker services before a renderer restart.
    int result = runApplication(argc, argv);
    if (const int recovery = oma::gpu::quickRecoveryExitCode()) result = recovery;
    return result == oma::gpu::QuickSoftwareRestart ? oma::gpu::restartQuickSoftware(argc, argv) : result;
}
