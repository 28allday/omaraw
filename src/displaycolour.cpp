#include "displaycolour.h"
#include "proof.h"
#include "colourpipeline.h"
#include <QColorSpace>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QGuiApplication>
#include <QEvent>
#include <QScreen>
#include <QSettings>
#include <QUrl>
#include <lcms2.h>

namespace {
QDBusMessage call(const QString &path, const QString &interface, const QString &method, const QVariantList &arguments = {}) {
    auto message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.ColorManager"), path, interface, method);
    message.setArguments(arguments);
    return message;
}
QDBusMessage properties(const QString &path, const QString &interface) {
    return call(path, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("GetAll"), {interface});
}
}

DisplayColour &DisplayColour::instance() {
    static DisplayColour object;
    return object;
}
DisplayColour::DisplayColour(QObject *parent) : QObject(parent) {
    connect(&ColourPipeline::instance(), &ColourPipeline::changed, this, [this] { ++m_revision; emit changed(); });
}

void DisplayColour::setWindow(QWindow *window) {
    if (m_window) { disconnect(m_window, nullptr, this, nullptr); m_window->removeEventFilter(this); }
    m_window = window;
    if (window) {
        window->installEventFilter(this);
        connect(window, &QWindow::screenChanged, this, [this] { refresh(); emit windowScaleChanged(); });
    }
    // colord emits Changed when a device's active profile changes.
    QDBusConnection::systemBus().connect(QStringLiteral("org.freedesktop.ColorManager"), QString(),
        QStringLiteral("org.freedesktop.ColorManager.Device"), QStringLiteral("Changed"), this, SLOT(refresh()));
    refresh();
    emit windowScaleChanged();
}
bool DisplayColour::eventFilter(QObject *watched, QEvent *event) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    // A Wayland surface can use a fractional scale even when its screen
    // reports an integer scale. Track the window's actual backing pixels.
    if (watched == m_window && event->type() == QEvent::DevicePixelRatioChange) emit windowScaleChanged();
#endif
    return QObject::eventFilter(watched, event);
}
void DisplayColour::setMode(int mode) {
    if (mode < 0 || mode > 2) return;
    QSettings().setValue(m_key + QStringLiteral("/mode"), mode);
    refresh();
}
void DisplayColour::setProfile(const QString &pathOrUrl) {
    const QString path = pathOrUrl.startsWith(QLatin1String("file:")) ? QUrl(pathOrUrl).toLocalFile() : pathOrUrl;
    QSettings().setValue(m_key + QStringLiteral("/profile"), path);
    QSettings().setValue(m_key + QStringLiteral("/mode"), 1);
    refresh();
}
void DisplayColour::install(const QByteArray &bytes, const QString &status) {
    { QMutexLocker lock(&m_mutex); m_icc = bytes; }
    m_status = status;
    ++m_revision;
    emit changed();
}
void DisplayColour::applyProfile(const QString &path, const QString &label) {
    QString error;
    const QByteArray bytes = Proof::readRgbProfile(path, &error);
    install(bytes, error.isEmpty() ? label + QStringLiteral(": ") + Proof::profileName(path) : tr("sRGB fallback — %1").arg(error));
}
void DisplayColour::refresh() {
    const int generation = ++m_generation;
    QScreen *screen = m_window ? m_window->screen() : QGuiApplication::primaryScreen();
    m_screenName = screen ? screen->name() : tr("No display");
    const QString model = screen ? screen->model() : QString();
    const QString serial = screen ? screen->serialNumber() : QString();
    const QByteArray identity = (m_screenName + QLatin1Char('|') + model + QLatin1Char('|') + serial).toUtf8();
    m_key = QStringLiteral("display/") + QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
    m_mode = qBound(0, QSettings().value(m_key + QStringLiteral("/mode"), 0).toInt(), 2);
    m_profile = QSettings().value(m_key + QStringLiteral("/profile")).toString();
    // A manual profile is right on Wayland only if the compositor is not
    // also applying one to this screen; say so rather than refuse it.
    if (m_mode == 1) {
        applyProfile(m_profile, QGuiApplication::platformName().startsWith(QLatin1String("wayland"))
            ? tr("Manual monitor profile (on Wayland, turn off any compositor profile for this screen, or colours convert twice)")
            : tr("Manual monitor profile"));
        return;
    }
    if (m_mode == 2) { install({}, tr("sRGB; display conversion off")); return; }
    if (QGuiApplication::platformName().startsWith(QLatin1String("wayland"))) {
        // Wayland compositors receive an sRGB surface and own the display
        // conversion. Applying colord again would convert colours twice.
        install({}, tr("sRGB surface; monitor conversion managed by the desktop"));
        return;
    }
    install({}, tr("sRGB fallback; checking for a system monitor profile"));
    querySystemProfile(generation, m_screenName, model, serial);
}
bool DisplayColour::matchesScreen(const QVariantMap &device, const QString &name, const QString &model, const QString &serial) {
    QVariantMap metadata = device.value(QStringLiteral("Metadata")).toMap();
    if (device.value(QStringLiteral("Metadata")).metaType() == QMetaType::fromType<QDBusArgument>()) {
        const auto values = qdbus_cast<QMap<QString, QString>>(device.value(QStringLiteral("Metadata")));
        for (auto it = values.cbegin(); it != values.cend(); ++it) metadata[it.key()] = it.value();
    }
    if (!device.value(QStringLiteral("Enabled"), true).toBool() || !device.value(QStringLiteral("ProfilingInhibitors")).toStringList().isEmpty()) return false;
    return (!name.isEmpty() && metadata.value(QStringLiteral("XRANDR_name")).toString() == name)
        || (!serial.isEmpty() && !model.isEmpty() && device.value(QStringLiteral("Serial")).toString() == serial && device.value(QStringLiteral("Model")).toString() == model);
}
void DisplayColour::querySystemProfile(int generation, const QString &name, const QString &model, const QString &serial) {
    auto *devices = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(call(QStringLiteral("/org/freedesktop/ColorManager"),
        QStringLiteral("org.freedesktop.ColorManager"), QStringLiteral("GetDevicesByKind"), {QStringLiteral("display")}), 2000), this);
    connect(devices, &QDBusPendingCallWatcher::finished, this, [=] {
        QDBusPendingReply<QList<QDBusObjectPath>> reply = *devices;
        devices->deleteLater();
        if (generation != m_generation) return;
        install({}, tr("sRGB fallback; no matching system monitor profile"));
        if (reply.isError()) return;
        for (const auto &device : reply.value()) {
            auto *props = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(properties(device.path(), QStringLiteral("org.freedesktop.ColorManager.Device")), 2000), this);
            connect(props, &QDBusPendingCallWatcher::finished, this, [=] {
                QDBusPendingReply<QVariantMap> values = *props;
                props->deleteLater();
                if (generation != m_generation || values.isError() || !matchesScreen(values.value(), name, model, serial)) return;
                auto *profile = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(call(device.path(),
                    QStringLiteral("org.freedesktop.ColorManager.Device"), QStringLiteral("GetProfileForQualifiers"), {QStringList{QStringLiteral("*")}}), 2000), this);
                connect(profile, &QDBusPendingCallWatcher::finished, this, [=] {
                    QDBusPendingReply<QDBusObjectPath> selected = *profile;
                    profile->deleteLater();
                    if (generation != m_generation || selected.isError()) return;
                    auto *details = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(properties(selected.value().path(), QStringLiteral("org.freedesktop.ColorManager.Profile")), 2000), this);
                    connect(details, &QDBusPendingCallWatcher::finished, this, [=] {
                        QDBusPendingReply<QVariantMap> info = *details;
                        details->deleteLater();
                        if (generation == m_generation && !info.isError()) applyProfile(info.value().value(QStringLiteral("Filename")).toString(), tr("System monitor profile"));
                    });
                });
            });
        }
    });
}

QImage DisplayColour::transform(const QImage &image, const QByteArray &icc) {
    if (image.isNull()) return image;
    // A tagged imported photograph is first interpreted in its own space.
    const QByteArray sourceIcc = image.colorSpace().isValid() ? image.colorSpace().iccProfile() : QByteArray();
    if (icc.isEmpty() && sourceIcc.isEmpty()) return image;
    cmsHPROFILE source = sourceIcc.isEmpty() ? cmsCreate_sRGBProfile() : cmsOpenProfileFromMem(sourceIcc.constData(), sourceIcc.size());
    cmsHPROFILE target = icc.isEmpty() ? cmsCreate_sRGBProfile() : cmsOpenProfileFromMem(icc.constData(), icc.size());
    cmsHTRANSFORM conversion = source && target ? cmsCreateTransform(source, TYPE_RGBA_FLT, target, TYPE_RGBA_FLT, INTENT_RELATIVE_COLORIMETRIC,
        cmsFLAGS_BLACKPOINTCOMPENSATION | cmsFLAGS_COPY_ALPHA) : nullptr;
    QImage result = image;
    if (conversion) {
        const QImage input = image.convertToFormat(QImage::Format_RGBA32FPx4);
        result = QImage(input.size(), QImage::Format_RGBA32FPx4);
        for (int y = 0; y < input.height(); ++y) cmsDoTransform(conversion, input.constScanLine(y), result.scanLine(y), input.width());
        cmsDeleteTransform(conversion);
        result.setColorSpace(icc.isEmpty() ? QColorSpace(QColorSpace::SRgb) : QColorSpace::fromIccProfile(icc));
    }
    if (source) cmsCloseProfile(source);
    if (target) cmsCloseProfile(target);
    return result.convertToFormat(QImage::Format_ARGB32);
}
QImage DisplayColour::convert(const QImage &image) const {
    QByteArray bytes;
    { QMutexLocker lock(&m_mutex); bytes = m_icc; }
    const QImage shown = ColourPipeline::instance().present(image);
    // The view produces SDR sRGB. Monitor calibration follows it exactly
    // once; Wayland's compositor already owns that final conversion.
    return bytes.isEmpty() ? shown.convertToFormat(QImage::Format_ARGB32) : transform(shown, bytes);
}
