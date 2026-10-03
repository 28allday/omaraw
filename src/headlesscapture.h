// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "captureservice.h"
#include <functional>

class Backend;

// Bounded, explicit camera sessions for agents. Construction does not probe USB.
class HeadlessCapture : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap state READ state)
    Q_PROPERTY(QVariantMap session READ session WRITE setSession)
public:
    explicit HeadlessCapture(Backend *backend, CaptureTransport *transport = nullptr);
    QVariantMap state() const;
    QVariantMap session() const;
    void setSession(const QVariantMap &options);
    Q_INVOKABLE QVariantMap refresh();
    Q_INVOKABLE QVariantMap connectCamera(int index);
    Q_INVOKABLE QVariantMap disconnectCamera();
    Q_INVOKABLE QVariantMap reloadControls();
    Q_INVOKABLE QVariantMap setControl(const QString &name, const QString &value);
    Q_INVOKABLE QVariantMap capture();
    Q_INVOKABLE QVariantMap preview(const QString &path);
    Q_INVOKABLE QVariantMap listen(int seconds);
signals:
    void operationFailed(const QString &error);
private:
    QVariantMap answer(const QString &error = {});
    QString wait(const QString &command, const std::function<void()> &start, int timeout = 120000);
    CaptureService m_service;
    bool m_started = false, m_timedOut = false;
    QStringList m_files;
    QString m_importError;
};
