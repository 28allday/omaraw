#include "download.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryFile>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <fcntl.h>
#include <unistd.h>

extern "C" int oma_capture_save(const void *bytes, size_t size, const char *directory,
                                 const char *stem, const char *extension, char *out_path, int out_size,
                                 char *error, int error_size) {
    if (out_path && out_size > 0) out_path[0] = 0;
    if (error && error_size > 0) error[0] = 0;
    try {
        if (!bytes || !size || size > size_t(std::numeric_limits<qsizetype>::max()) || !directory || !stem || !out_path || out_size < 2)
            throw std::runtime_error("Invalid or empty camera download");
        const QString name = QString::fromUtf8(stem), ext = QString::fromUtf8(extension ? extension : "");
        if (name.isEmpty() || name == "." || name == ".." || name.contains('/') || name.contains('\\')
            || ext.contains('/') || ext.contains('\\') || name.contains('\n') || ext.contains('\n'))
            throw std::runtime_error("Invalid capture filename");
        const QDir folder(QString::fromLocal8Bit(directory));
        if (!folder.isAbsolute()) throw std::runtime_error("Capture folder must be absolute");
        QTemporaryFile temporary(folder.filePath(".omaraw-capture-XXXXXX"));
        if (!temporary.open()) throw std::runtime_error(temporary.errorString().toStdString());
        const char *data = static_cast<const char *>(bytes);
        for (qsizetype done = 0; done < qsizetype(size);) {
            const qint64 written = temporary.write(data + done, qMin<qsizetype>(qsizetype(size) - done, 1024 * 1024));
            if (written <= 0) throw std::runtime_error(temporary.errorString().toStdString());
            done += written;
        }
        if (!temporary.flush() || ::fsync(temporary.handle())) throw std::runtime_error("Cannot flush camera download to disk");
        QFile verify(temporary.fileName());
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!verify.open(QIODevice::ReadOnly) || verify.size() != qint64(size) || !hash.addData(&verify)
            || hash.result() != QCryptographicHash::hash(QByteArrayView(data, qsizetype(size)), QCryptographicHash::Sha256))
            throw std::runtime_error("Camera download did not verify; camera copy retained");
        verify.close();
        for (int count = 1; count <= 100000; ++count) {
            const QString suffix = count == 1 ? QString() : QStringLiteral("-%1").arg(count);
            const auto path = QFile::encodeName(folder.filePath(name + suffix + ext));
            if (path.size() >= out_size) throw std::runtime_error("Capture path is too long");
            // link is atomic and refuses existing names, including symlinks.
            if (::link(QFile::encodeName(temporary.fileName()).constData(), path.constData())) {
                if (errno == EEXIST) continue;
                throw std::runtime_error(std::string("Cannot publish capture: ") + std::strerror(errno));
            }
            std::memcpy(out_path, path.constData(), size_t(path.size()) + 1);
            const int descriptor = ::open(QFile::encodeName(folder.absolutePath()).constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            const bool durable = descriptor >= 0 && ::fsync(descriptor) == 0;
            if (descriptor >= 0) ::close(descriptor);
            if (!durable) {
                if (error && error_size > 0) std::snprintf(error, size_t(error_size), "Capture saved; directory sync failed, camera copy retained");
                return 1;
            }
            return 0;
        }
        throw std::runtime_error("Too many capture filename collisions");
    } catch (const std::exception &e) {
        if (error && error_size > 0) std::snprintf(error, size_t(error_size), "%s", e.what());
        return -1;
    }
}
