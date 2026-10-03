#include "autotag.h"
#include "metadata.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <opencv2/core.hpp>
#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>
#include <cmath>
#include <cstdio>
#include <sys/resource.h>

namespace {
struct Subject { const char *key; const char *label; const char *group; };
// The order is the model's COCO class index, not alphabetical order.
const Subject subjects[] = {
    {"people", QT_TRANSLATE_NOOP("AutoTags", "People"), "people"},
    {"bicycles", QT_TRANSLATE_NOOP("AutoTags", "Bicycles"), "transport"},
    {"cars", QT_TRANSLATE_NOOP("AutoTags", "Cars"), "transport"},
    {"motorcycles", QT_TRANSLATE_NOOP("AutoTags", "Motorcycles"), "transport"},
    {"aircraft", QT_TRANSLATE_NOOP("AutoTags", "Aircraft"), "transport"},
    {"buses", QT_TRANSLATE_NOOP("AutoTags", "Buses"), "transport"},
    {"trains", QT_TRANSLATE_NOOP("AutoTags", "Trains"), "transport"},
    {"trucks", QT_TRANSLATE_NOOP("AutoTags", "Trucks"), "transport"},
    {"boats", QT_TRANSLATE_NOOP("AutoTags", "Boats"), "transport"},
    {"traffic_lights", QT_TRANSLATE_NOOP("AutoTags", "Traffic lights"), "objects"},
    {"fire_hydrants", QT_TRANSLATE_NOOP("AutoTags", "Fire hydrants"), "objects"},
    {"stop_signs", QT_TRANSLATE_NOOP("AutoTags", "Stop signs"), "objects"},
    {"parking_meters", QT_TRANSLATE_NOOP("AutoTags", "Parking meters"), "objects"},
    {"benches", QT_TRANSLATE_NOOP("AutoTags", "Benches"), "objects"},
    {"birds", QT_TRANSLATE_NOOP("AutoTags", "Birds"), "animals"},
    {"cats", QT_TRANSLATE_NOOP("AutoTags", "Cats"), "animals"},
    {"dogs", QT_TRANSLATE_NOOP("AutoTags", "Dogs"), "animals"},
    {"horses", QT_TRANSLATE_NOOP("AutoTags", "Horses"), "animals"},
    {"sheep", QT_TRANSLATE_NOOP("AutoTags", "Sheep"), "animals"},
    {"cattle", QT_TRANSLATE_NOOP("AutoTags", "Cattle"), "animals"},
    {"elephants", QT_TRANSLATE_NOOP("AutoTags", "Elephants"), "animals"},
    {"bears", QT_TRANSLATE_NOOP("AutoTags", "Bears"), "animals"},
    {"zebras", QT_TRANSLATE_NOOP("AutoTags", "Zebras"), "animals"},
    {"giraffes", QT_TRANSLATE_NOOP("AutoTags", "Giraffes"), "animals"},
    {"backpacks", QT_TRANSLATE_NOOP("AutoTags", "Backpacks"), "objects"},
    {"umbrellas", QT_TRANSLATE_NOOP("AutoTags", "Umbrellas"), "objects"},
    {"handbags", QT_TRANSLATE_NOOP("AutoTags", "Handbags"), "objects"},
    {"ties", QT_TRANSLATE_NOOP("AutoTags", "Ties"), "objects"},
    {"suitcases", QT_TRANSLATE_NOOP("AutoTags", "Suitcases"), "objects"},
    {"frisbees", QT_TRANSLATE_NOOP("AutoTags", "Frisbees"), "sports"},
    {"skis", QT_TRANSLATE_NOOP("AutoTags", "Skis"), "sports"},
    {"snowboards", QT_TRANSLATE_NOOP("AutoTags", "Snowboards"), "sports"},
    {"sports_balls", QT_TRANSLATE_NOOP("AutoTags", "Sports balls"), "sports"},
    {"kites", QT_TRANSLATE_NOOP("AutoTags", "Kites"), "sports"},
    {"baseball_bats", QT_TRANSLATE_NOOP("AutoTags", "Baseball bats"), "sports"},
    {"baseball_gloves", QT_TRANSLATE_NOOP("AutoTags", "Baseball gloves"), "sports"},
    {"skateboards", QT_TRANSLATE_NOOP("AutoTags", "Skateboards"), "sports"},
    {"surfboards", QT_TRANSLATE_NOOP("AutoTags", "Surfboards"), "sports"},
    {"tennis_rackets", QT_TRANSLATE_NOOP("AutoTags", "Tennis rackets"), "sports"},
    {"bottles", QT_TRANSLATE_NOOP("AutoTags", "Bottles"), "objects"},
    {"wine_glasses", QT_TRANSLATE_NOOP("AutoTags", "Wine glasses"), "objects"},
    {"cups", QT_TRANSLATE_NOOP("AutoTags", "Cups"), "objects"},
    {"forks", QT_TRANSLATE_NOOP("AutoTags", "Forks"), "objects"},
    {"knives", QT_TRANSLATE_NOOP("AutoTags", "Knives"), "objects"},
    {"spoons", QT_TRANSLATE_NOOP("AutoTags", "Spoons"), "objects"},
    {"bowls", QT_TRANSLATE_NOOP("AutoTags", "Bowls"), "objects"},
    {"bananas", QT_TRANSLATE_NOOP("AutoTags", "Bananas"), "food"},
    {"apples", QT_TRANSLATE_NOOP("AutoTags", "Apples"), "food"},
    {"sandwiches", QT_TRANSLATE_NOOP("AutoTags", "Sandwiches"), "food"},
    {"oranges", QT_TRANSLATE_NOOP("AutoTags", "Oranges"), "food"},
    {"broccoli", QT_TRANSLATE_NOOP("AutoTags", "Broccoli"), "food"},
    {"carrots", QT_TRANSLATE_NOOP("AutoTags", "Carrots"), "food"},
    {"hot_dogs", QT_TRANSLATE_NOOP("AutoTags", "Hot dogs"), "food"},
    {"pizza", QT_TRANSLATE_NOOP("AutoTags", "Pizza"), "food"},
    {"doughnuts", QT_TRANSLATE_NOOP("AutoTags", "Doughnuts"), "food"},
    {"cakes", QT_TRANSLATE_NOOP("AutoTags", "Cakes"), "food"},
    {"chairs", QT_TRANSLATE_NOOP("AutoTags", "Chairs"), "objects"},
    {"sofas", QT_TRANSLATE_NOOP("AutoTags", "Sofas"), "objects"},
    {"potted_plants", QT_TRANSLATE_NOOP("AutoTags", "Potted plants"), "objects"},
    {"beds", QT_TRANSLATE_NOOP("AutoTags", "Beds"), "objects"},
    {"dining_tables", QT_TRANSLATE_NOOP("AutoTags", "Dining tables"), "objects"},
    {"toilets", QT_TRANSLATE_NOOP("AutoTags", "Toilets"), "objects"},
    {"televisions", QT_TRANSLATE_NOOP("AutoTags", "Televisions"), "objects"},
    {"laptops", QT_TRANSLATE_NOOP("AutoTags", "Laptops"), "objects"},
    {"computer_mice", QT_TRANSLATE_NOOP("AutoTags", "Computer mice"), "objects"},
    {"remote_controls", QT_TRANSLATE_NOOP("AutoTags", "Remote controls"), "objects"},
    {"keyboards", QT_TRANSLATE_NOOP("AutoTags", "Keyboards"), "objects"},
    {"phones", QT_TRANSLATE_NOOP("AutoTags", "Phones"), "objects"},
    {"microwaves", QT_TRANSLATE_NOOP("AutoTags", "Microwaves"), "objects"},
    {"ovens", QT_TRANSLATE_NOOP("AutoTags", "Ovens"), "objects"},
    {"toasters", QT_TRANSLATE_NOOP("AutoTags", "Toasters"), "objects"},
    {"sinks", QT_TRANSLATE_NOOP("AutoTags", "Sinks"), "objects"},
    {"refrigerators", QT_TRANSLATE_NOOP("AutoTags", "Refrigerators"), "objects"},
    {"books", QT_TRANSLATE_NOOP("AutoTags", "Books"), "objects"},
    {"clocks", QT_TRANSLATE_NOOP("AutoTags", "Clocks"), "objects"},
    {"vases", QT_TRANSLATE_NOOP("AutoTags", "Vases"), "objects"},
    {"scissors", QT_TRANSLATE_NOOP("AutoTags", "Scissors"), "objects"},
    {"teddy_bears", QT_TRANSLATE_NOOP("AutoTags", "Teddy bears"), "objects"},
    {"hair_driers", QT_TRANSLATE_NOOP("AutoTags", "Hair driers"), "objects"},
    {"toothbrushes", QT_TRANSLATE_NOOP("AutoTags", "Toothbrushes"), "objects"},
};
static_assert(sizeof(subjects) / sizeof(subjects[0]) == 80);
}
QStringList AutoTags::keys() {
    QStringList out; for (const auto &s : subjects) out << QString::fromLatin1(s.key); return out;
}
QString AutoTags::label(const QString &key) {
    for (const auto &s : subjects) if (key == QLatin1String(s.key)) return QCoreApplication::translate("AutoTags",s.label);
    for (const auto &g : groups()) if (key == "group:" + g.toMap().value("key").toString()) return g.toMap().value("label").toString();
    return {};
}
QVariantList AutoTags::groups() {
    QVariantList out;
    for (const auto &g : {QPair<const char *,const char *>{"people", QT_TRANSLATE_NOOP("AutoTags","People")},
         {"animals", QT_TRANSLATE_NOOP("AutoTags","Animals")}, {"transport", QT_TRANSLATE_NOOP("AutoTags","Transport")},
         {"sports", QT_TRANSLATE_NOOP("AutoTags","Sports equipment")}, {"food", QT_TRANSLATE_NOOP("AutoTags","Food")},
         {"objects", QT_TRANSLATE_NOOP("AutoTags","Other objects")}})
        out << QVariantMap{{"key",QString::fromLatin1(g.first)},{"label",QCoreApplication::translate("AutoTags",g.second)}};
    return out;
}
QVariantList AutoTags::options() {
    QVariantList out;
    for (const auto &s : subjects) out << QVariantMap{{"tag",QString::fromLatin1(s.key)},
        {"label",QCoreApplication::translate("AutoTags",s.label)},{"group",QString::fromLatin1(s.group)}};
    return out;
}
QStringList AutoTags::collectionKeys() {
    auto out=keys(); for (const auto &g : groups()) if(g.toMap().value("key")!="people") out << "group:"+g.toMap().value("key").toString();
    return out;
}
QStringList AutoTags::members(const QString &key) {
    if(keys().contains(key)) return {key};
    QStringList out;
    if(key.startsWith("group:")) for(const auto &s:subjects) if(key.mid(6)==QLatin1String(s.group)) out << QString::fromLatin1(s.key);
    return out;
}
QString AutoTags::modelVersion() { return QStringLiteral("yolox-nano-0.1.1rc0-v2-all80"); }

int AutoTags::workerMain() {
    // This process has its own OpenCV thread policy, independent of Develop.
    setpriority(PRIO_PROCESS, 0, 10);
    cv::setNumThreads(2);
    cv::dnn::Net net;
    QFile input, output;
    if (!input.open(stdin, QIODevice::ReadOnly) || !output.open(stdout, QIODevice::WriteOnly)) return 1;
    for (;;) {
        const QByteArray line = input.readLine(65537);
        if (line.isEmpty()) break;
        if (line.size() > 65536 || !line.endsWith('\n')) return 2;
        QJsonParseError parse;
        const auto document = QJsonDocument::fromJson(line, &parse);
        if (parse.error != QJsonParseError::NoError || !document.isObject()) return 2;
        const auto request = document.object();
        const QString path = request.value("path").toString();
        QJsonObject reply{{"id", request.value("id")}};
        bool fatalFailure = net.empty();
        try {
            if (net.empty()) {
                QFile model(QStringLiteral(":/autotag/yolox_nano.onnx"));
                if (!model.open(QIODevice::ReadOnly)) throw std::runtime_error("Bundled auto-tag model is unavailable.");
                const QByteArray bytes = model.readAll();
                if (bytes.size() != 3659407 || QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()
                    != "c789161ed43c8269fcd4e67c67eeeb4e80c622da2eb296a20bc6007bd18a0b7d")
                    throw std::runtime_error("Bundled auto-tag model failed verification.");
                // OpenCV 5's auto engine can ignore backend/target requests.
                // Use the 4.x-compatible engine so the CPU choice is explicit.
                net = cv::dnn::readNetFromONNX(reinterpret_cast<const char *>(bytes.constData()), bytes.size()
#if CV_VERSION_MAJOR >= 5
                                             , cv::dnn::ENGINE_CLASSIC
#endif
                                             );
                net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
                net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
            }
            fatalFailure = false;
            if (!QFileInfo(path).isFile()) throw std::runtime_error("Photo is unavailable; reconnect its drive and rescan.");
            // Read the original's oriented preview, independent of edits or RAW CFA.
            const QImage image = Metadata::preview(path, 832).convertToFormat(QImage::Format_RGB888);
            if (image.isNull()) throw std::runtime_error("Could not read a preview for auto-tagging.");
            cv::Mat rgb(image.height(), image.width(), CV_8UC3, const_cast<uchar *>(image.constBits()), image.bytesPerLine());
            cv::Mat bgr, scaled, padded(416, 416, CV_8UC3, cv::Scalar(114,114,114));
            cv::cvtColor(rgb, bgr, cv::COLOR_RGB2BGR);
            const double scale = std::min(416.0 / bgr.cols, 416.0 / bgr.rows);
            const int width = std::max(1, int(bgr.cols * scale)), height = std::max(1, int(bgr.rows * scale));
            cv::resize(bgr, scaled, cv::Size(width, height), 0, 0, cv::INTER_LINEAR);
            scaled.copyTo(padded(cv::Rect(0, 0, width, height)));
            // Official 0.1.1rc0 preprocessing: BGR, 0..255, top-left letterbox.
            fatalFailure = true; // Inference/model failures pause the queue.
            net.setInput(cv::dnn::blobFromImage(padded, 1.0, cv::Size(416,416), cv::Scalar(), false, false));
            cv::Mat prediction = net.forward();
            if (prediction.type() != CV_32F || prediction.total() != 3549 * 85 || !prediction.isContinuous())
                throw std::runtime_error("Unexpected auto-tag model output.");
            double best[80] = {};
            int row = 0;
            for (const int stride : {8, 16, 32}) {
                const int grid = 416 / stride;
                for (int y = 0; y < grid; ++y) for (int x = 0; x < grid; ++x, ++row) {
                    const float *p = prediction.ptr<float>() + row * 85;
                    const double cx = (p[0] + x) * stride, cy = (p[1] + y) * stride;
                    if (!std::isfinite(cx) || !std::isfinite(cy) || cx < 0 || cy < 0 || cx >= width || cy >= height) continue;
                    for (int c = 0; c < 80; ++c) {
                        const double score = double(p[4]) * p[5 + c];
                        if (std::isfinite(score) && score >= 0 && score <= 1) best[c] = std::max(best[c], score);
                    }
                }
            }
            // Presence only: duplicate boxes do not affect a class's maximum.
            QJsonObject scores;
            const auto labels = keys();
            for (int i = 0; i < labels.size(); ++i) scores.insert(labels[i], best[i]);
            reply.insert("scores", scores);
        } catch (const std::exception &e) {
            reply.insert("error", QString::fromUtf8(e.what()).left(500));
            reply.insert("fatal", fatalFailure);
        }
        output.write(QJsonDocument(reply).toJson(QJsonDocument::Compact) + '\n');
        output.flush();
    }
    return 0;
}

AutoTagWorker::AutoTagWorker(QObject *parent) : QObject(parent) {
    m_timeout.setSingleShot(true); m_timeout.setInterval(60000);
    m_idle.setSingleShot(true); m_idle.setInterval(30000);
    connect(&m_timeout, &QTimer::timeout, this, [this] { fail(tr("Auto-tagging timed out. Retry the scan.")); });
    connect(&m_idle, &QTimer::timeout, this, &AutoTagWorker::stop);
    connect(&m_process, &QProcess::started, this, [this] { if (m_active) m_process.write(m_request); });
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &AutoTagWorker::receive);
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] { m_process.readAllStandardError(); });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (m_active) fail(tr("Auto-tag worker failed: %1").arg(m_process.errorString()));
    });
    connect(&m_process, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
        if (m_active) fail(tr("Auto-tag worker stopped unexpectedly. Retry the scan."));
    });
}
AutoTagWorker::~AutoTagWorker() { stop(); }
void AutoTagWorker::stop() {
    m_active = false; m_timeout.stop(); m_idle.stop(); m_buffer.clear(); m_request.clear();
    if (m_process.state() != QProcess::NotRunning) { m_process.kill(); m_process.waitForFinished(3000); }
}
void AutoTagWorker::fail(const QString &error) { stop(); emit completed({}, error, true); }
void AutoTagWorker::scan(const QString &path) {
    Q_ASSERT(!m_active);
    m_idle.stop(); m_buffer.clear(); m_active = true;
    m_request = QJsonDocument(QJsonObject{{"id", ++m_serial}, {"path", path}}).toJson(QJsonDocument::Compact) + '\n';
    m_timeout.start();
    if (m_process.state() == QProcess::Running) { m_process.write(m_request); return; }
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert("QT_QPA_PLATFORMTHEME", ""); env.insert("QT_QPA_PLATFORM", "offscreen"); env.insert("QT_QUICK_BACKEND", "software");
    env.insert("OMP_NUM_THREADS", "2");
    m_process.setProcessEnvironment(env);
    m_process.start(qEnvironmentVariable("OMARAW_TEST_APP", QCoreApplication::applicationFilePath()), {"--auto-tag-worker"});
}
void AutoTagWorker::receive() {
    m_buffer += m_process.readAllStandardOutput();
    if (m_buffer.size() > 65536) { fail(tr("Auto-tag worker returned too much data.")); return; }
    while (m_buffer.contains('\n')) {
        const int end = m_buffer.indexOf('\n');
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(m_buffer.left(end), &error);
        m_buffer.remove(0, end + 1);
        if (!m_active) continue;
        const auto reply = doc.object();
        if (error.error != QJsonParseError::NoError || !doc.isObject() || reply.value("id").toInt(-1) != m_serial) {
            fail(tr("Invalid auto-tag worker response.")); return;
        }
        const auto scores = reply.value("scores").toObject();
        const QString message = reply.value("error").toString();
        if (message.isEmpty()) {
            if (scores.size() != AutoTags::keys().size()) { fail(tr("Incomplete auto-tag worker response.")); return; }
            for (const QString &key : AutoTags::keys()) {
                const double score = scores.value(key).toDouble(-1);
                if (!std::isfinite(score) || score < 0 || score > 1) { fail(tr("Invalid auto-tag score.")); return; }
            }
        }
        m_active = false; m_timeout.stop(); m_idle.start();
        emit completed(scores.toVariantMap(), message, reply.value("fatal").toBool());
    }
}
