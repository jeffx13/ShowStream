#include <QQuickWindow>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QIcon>
#include <QQmlNetworkAccessManagerFactory>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QDir>
#include "app/application.h"
#include "app/crashhandler.h"
#include "net/cloudflare.h"

namespace {

// A separate stack from Client's, so the clearance and UA are applied here too, or posters 403.
class ImageAccessManager : public QNetworkAccessManager {
public:
    using QNetworkAccessManager::QNetworkAccessManager;

protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &req, QIODevice *outgoingData) override {
        const QString host = req.url().host();
        QNetworkRequest r(req);
        bool modified = false;

        // AllAnime posters 403 without one.
        if (host.endsWith("youtube-anime.com") && !req.hasRawHeader("Referer")) {
            r.setRawHeader("Referer", "https://youtu-chan.com/");
            modified = true;
        }

        if (const QString hostUa = Cloudflare::hostUserAgent(host); !hostUa.isEmpty()) {
            r.setRawHeader("User-Agent", hostUa.toUtf8());
            modified = true;
        }

        return QNetworkAccessManager::createRequest(op, modified ? r : req, outgoingData);
    }
};

class ImageAccessManagerFactory : public QQmlNetworkAccessManagerFactory {
public:
    QNetworkAccessManager *create(QObject *parent) override {
        auto *manager = new ImageAccessManager(parent);
        manager->setCookieJar(new Cloudflare::ProxyCookieJar(manager));

        const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/httpcache";
        QDir().mkpath(cacheDir);
        auto *diskCache = new QNetworkDiskCache(manager);
        diskCache->setCacheDirectory(cacheDir);
        diskCache->setMaximumCacheSize(100LL * 1024 * 1024);
        manager->setCache(diskCache);
        return manager;
    }
};

}

#ifdef _WIN32
// Prefer the discrete GPU on switchable-graphics laptops.
extern "C" {
    __declspec(dllexport) unsigned long NvOptimusEnablement = 1;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

int main(int argc, char *argv[]) {
    CrashHandler::install();

    qputenv("QT_ENABLE_ACCESSIBILITY", "0");

    // Threaded render loop (Qt defaults to the basic single-thread loop on Windows+GL).
    qputenv("QSG_RENDER_LOOP", "threaded");

    QGuiApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGLRhi);
    QGuiApplication app(argc, argv);
    app.setWindowIcon(QIcon(":/AoNami/resources/app.ico"));

    QString launchPath = (argc > 1) ? QString::fromUtf8(argv[1]) : QString();
    Application application(launchPath);
    CrashHandler::reportPending();
    application.setFont(":/AoNami/resources/app-font.ttf");

    QQmlApplicationEngine engine;
    engine.addImportPath("qrc:/AoNami/src/ui/qml");
    engine.setNetworkAccessManagerFactory(new ImageAccessManagerFactory);

    const QUrl url(QStringLiteral("qrc:/AoNami/src/ui/qml/main.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                     &app, [&url](QObject *obj, const QUrl &objUrl) {
                         if (!obj && url == objUrl)
                             QCoreApplication::exit(-1);
                     }, Qt::QueuedConnection);
    engine.load(url);
    return app.exec();
}
