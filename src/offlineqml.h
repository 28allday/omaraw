#pragma once

#include <QQmlEngine>
#include <QQmlNetworkAccessManagerFactory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>

// QML renders local UI and photos. Downloads belong to the explicit model
// installers; clicked web links open through the desktop browser. Incidental
// rich text or image URLs must never start a network request.
namespace OfflineQml {
class DeniedReply final : public QNetworkReply {
public:
    DeniedReply(const QNetworkRequest &request, QObject *parent) : QNetworkReply(parent) {
        setRequest(request); setUrl(request.url());
        setError(ContentAccessDenied, QStringLiteral("Remote resources are disabled in the photo interface"));
        open(QIODevice::ReadOnly);
        QTimer::singleShot(0, this, [this] {
            if (isFinished()) return;
            setFinished(true); emit errorOccurred(error()); emit finished();
        });
    }
    void abort() override {}
protected:
    qint64 readData(char *, qint64) override { return -1; }
};
class Manager final : public QNetworkAccessManager {
public:
    using QNetworkAccessManager::QNetworkAccessManager;
protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request, QIODevice *body) override {
        const auto scheme = request.url().scheme().toLower();
        if (scheme != QStringLiteral("file") && scheme != QStringLiteral("qrc") && scheme != QStringLiteral("data"))
            return new DeniedReply(request, this);
        return QNetworkAccessManager::createRequest(op, request, body);
    }
};
class Factory final : public QQmlNetworkAccessManagerFactory {
public:
    QNetworkAccessManager *create(QObject *parent) override { return new Manager(parent); }
};
inline void install(QQmlEngine &engine) {
    static Factory factory;
    engine.setNetworkAccessManagerFactory(&factory);
}
}
