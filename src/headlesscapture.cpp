// SPDX-License-Identifier: GPL-3.0-or-later
#include "headlesscapture.h"
#include "backend.h"
#include <QElapsedTimer>
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QMetaProperty>
#include <QTimer>
#include <QThread>
#include <cmath>

HeadlessCapture::HeadlessCapture(Backend *backend, CaptureTransport *transport) : m_service(nullptr, transport) {
    m_service.setListenToBody(false);
    connect(&m_service, &CaptureService::captured, this, [this, backend](const QString &path) {
        m_files << path;
        if (backend && !backend->importCapture(path)) m_importError = tr("Saved %1 but could not add it to the catalog: %2").arg(path, backend->statusMessage());
    });
}
QVariantMap HeadlessCapture::state() const {
    QVariantMap result;
    const auto *meta = m_service.metaObject();
    for (int i = QObject::staticMetaObject.propertyCount(); i < meta->propertyCount(); ++i) {
        const auto property = meta->property(i);
        if (QByteArray(property.name()) != "liveSource") result.insert(QString::fromLatin1(property.name()), property.read(&m_service));
    }
    result.insert("started", m_started); result.insert("timedOut", m_timedOut);
    return result;
}
QVariantMap HeadlessCapture::session() const {
    return {{"folder",m_service.sessionFolder()},{"name",m_service.sessionName()},
            {"nameTemplate",m_service.nameTemplate()},{"deleteFromCamera",m_service.deleteFromCamera()}};
}
void HeadlessCapture::setSession(const QVariantMap &options) {
    const QStringList keys{"folder","name","nameTemplate","deleteFromCamera"};
    for (auto it=options.cbegin();it!=options.cend();++it) {
        if (!keys.contains(it.key())) { emit operationFailed(tr("Unknown capture session option: %1").arg(it.key())); return; }
        if (it.key()=="deleteFromCamera" ? it.value().metaType().id()!=QMetaType::Bool
            : it.value().metaType().id()!=QMetaType::QString || (it.key()=="folder" && it.value().toString().trimmed().isEmpty())) {
            emit operationFailed(tr("Invalid capture session option: %1").arg(it.key())); return;
        }
    }
    if (options.contains("folder")) m_service.setSessionFolder(options.value("folder").toString());
    if (options.contains("name")) m_service.setSessionName(options.value("name").toString());
    if (options.contains("nameTemplate")) m_service.setNameTemplate(options.value("nameTemplate").toString());
    if (options.contains("deleteFromCamera")) m_service.setDeleteFromCamera(options.value("deleteFromCamera").toBool());
}
QVariantMap HeadlessCapture::answer(const QString &error) {
    const QString failure = error.isEmpty() ? m_importError : error;
    QVariantMap result{{"ok",failure.isEmpty()},{"state",state()},{"files",m_files}};
    if (!failure.isEmpty()) { result.insert("error",failure); emit operationFailed(failure); }
    return result;
}
QString HeadlessCapture::wait(const QString &command, const std::function<void()> &start, int timeout) {
    if (m_timedOut) return tr("A camera command timed out; reconnect in a new CLI run.");
    QEventLoop loop; bool done=false; QString error;
    const auto connection=connect(&m_service,&CaptureService::commandFinished,&loop,[&](const QString &name,const QString &failure) {
        if (name==command) { done=true; error=failure; loop.quit(); }
    });
    QTimer timer; timer.setSingleShot(true); connect(&timer,&QTimer::timeout,&loop,&QEventLoop::quit);
    start(); timer.start(timeout); if (!done) loop.exec(); disconnect(connection);
    if (!done) { m_timedOut=true; return tr("Camera command %1 did not finish within %2 seconds.").arg(command).arg(timeout/1000); }
    return error;
}
QVariantMap HeadlessCapture::refresh() {
    m_files.clear(); m_importError.clear();
    const QString error=wait("refresh",[this] { if (m_started) m_service.refresh(); else m_service.start(); });
    m_started=m_service.available(); return answer(error);
}
QVariantMap HeadlessCapture::connectCamera(int index) {
    m_files.clear(); m_importError.clear();
    if (!m_started) { const auto found=refresh(); if (!found.value("ok").toBool()) return found; }
    if (index<0 || index>=m_service.cameras().size()) return answer(tr("Camera index is unavailable; use capture.refresh first."));
    if (m_service.connected()) { const auto closed=disconnectCamera(); if (!closed.value("ok").toBool()) return closed; }
    return answer(wait("connectCamera",[&] { m_service.connectCamera(index); }));
}
QVariantMap HeadlessCapture::disconnectCamera() {
    m_files.clear(); m_importError.clear();
    return answer(m_service.connected() ? wait("disconnectCamera",[this] { m_service.disconnectCamera(); }) : QString());
}
QVariantMap HeadlessCapture::reloadControls() {
    m_files.clear(); m_importError.clear();
    if (!m_service.connected()) return answer(tr("Connect a camera in this apply run first."));
    return answer(wait("reloadControls",[this] { m_service.reloadControls(); }));
}
QVariantMap HeadlessCapture::setControl(const QString &name,const QString &value) {
    m_files.clear(); m_importError.clear();
    if (!m_service.connected() || !m_service.canConfig()) return answer(tr("Connect a camera that supports configuration first."));
    QVariantMap control;
    for (const auto &row:m_service.controls()) if (row.toMap().value("name")==name) control=row.toMap();
    if (control.isEmpty() || control.value("readOnly").toBool()) return answer(tr("Unknown or read-only camera control: %1").arg(name));
    if (control.value("type").toInt()==0 && !control.value("choices").toStringList().contains(value)) return answer(tr("Value is not one of this camera control's choices."));
    return answer(wait("setControl",[&] { m_service.setControl(name,value); }));
}
QVariantMap HeadlessCapture::capture() {
    m_files.clear(); m_importError.clear();
    if (!m_service.connected() || !m_service.canCapture()) return answer(tr("Connect a camera that supports remote capture first."));
    return answer(wait("capture",[this] { m_service.capture(); }));
}
QVariantMap HeadlessCapture::preview(const QString &path) {
    m_files.clear(); m_importError.clear();
    if (!m_service.connected() || !m_service.canPreview()) return answer(tr("Connect a camera with live preview first."));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly|QIODevice::NewOnly)) return answer(tr("Preview needs a new writable PNG path: %1").arg(file.errorString()));
    const QString error=wait("preview",[this] { m_service.setLiveView(true); });
    m_service.setLiveView(false);
    if (!error.isEmpty() || !m_service.frameCopy().save(&file,"PNG")) { file.close(); file.remove(); return answer(error.isEmpty()?tr("Could not write the preview."):error); }
    file.close(); m_files << QFileInfo(path).absoluteFilePath(); return answer();
}
QVariantMap HeadlessCapture::listen(int seconds) {
    m_files.clear(); m_importError.clear();
    if (!m_service.connected()) return answer(tr("Connect a camera in this apply run first."));
    if (seconds<1 || seconds>3600) return answer(tr("Listen duration must be 1 to 3600 seconds."));
    QEventLoop loop; QString error;
    const auto connection=connect(&m_service,&CaptureService::commandFinished,&loop,[&](const QString &command,const QString &failure) {
        if (command=="listen" && !failure.isEmpty()) { error=failure; loop.quit(); }
    });
    m_service.setListenToBody(true);
    QTimer timer; timer.setSingleShot(true); connect(&timer,&QTimer::timeout,&loop,&QEventLoop::quit); timer.start(seconds*1000);
    if (error.isEmpty()) loop.exec();
    m_service.setListenToBody(false);
    // Drain any bounded (400 ms) camera wait already in flight before returning.
    QElapsedTimer tail; tail.start();
    while(m_service.listening() && tail.elapsed()<5000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents,20); QThread::msleep(5);
    }
    disconnect(connection);
    if(m_service.listening()) { m_timedOut=true; error=tr("The pending camera listen did not stop."); }
    return answer(error);
}
