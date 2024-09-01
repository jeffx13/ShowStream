#include "application.h"
#include <QNetworkProxyFactory>
#include <QTextCodec>
#include <libxml2/libxml/parser.h>
#include <QQmlContext>
#include "player/mpvobject.h"
#include "utils/errorhandler.h"
#include <QFontDatabase>
#include <QQuickStyle>

Application::Application(QGuiApplication &app, const QString &launchPath,
                         QObject *parent) : QObject(parent), app(app){
    xmlInitParser();
    curl_global_init(CURL_GLOBAL_ALL);
    QNetworkProxyFactory::setUseSystemConfiguration(true);
    // qputenv("HTTP_PROXY", QByteArray("http://127.0.0.1:7897"));
    // qputenv("HTTPS_PROXY", QByteArray("http://127.0.0.1:7897"));

    if (!launchPath.isEmpty()) {
        QUrl url = QUrl::fromUserInput(launchPath);
        m_playlistManager.openUrl(url, false);
    }

    QObject::connect(&m_playlistManager, &PlaylistManager::currentIndexChanged,
                     this, &Application::updateLastWatchedIndex);


    const QUrl url(QStringLiteral("qrc:src/qml/main.qml"));
    setFont(":/resources/app-font.ttf");
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreated, &app,
        [url](QObject *obj, const QUrl &objUrl) {
            if (!obj && url == objUrl)
                QCoreApplication::exit(-1);
        },
        Qt::QueuedConnection);
    std::setlocale(LC_NUMERIC, "C");
    qputenv("LC_NUMERIC", QByteArrayLiteral("C"));
    QQuickStyle::setStyle("Universal");

    qmlRegisterSingletonInstance<Application>("Kyokou", 1, 0, "App", this);
    qmlRegisterSingletonInstance<ErrorHandler>("Kyokou", 1, 0, "ErrorHandler", &ErrorHandler::instance());
    qmlRegisterType<MpvObject>("MpvPlayer", 1, 0, "MpvObject");
    engine.load(url);
}

Application::~Application() {
    curl_global_cleanup();
    xmlCleanupParser();
}

void Application::setFont(QString fontPath) {
    qint32 fontId = QFontDatabase::addApplicationFont(fontPath);
    QStringList fontList = QFontDatabase::applicationFontFamilies(fontId);
    QString family = fontList.first();
    QGuiApplication::setFont(QFont(family, 16));
}

void Application::loadShow(int index, bool fromWatchList) {
    if (fromWatchList) {
        QJsonObject showJson = m_libraryManager.getShowJsonAt(index);
        if (showJson.isEmpty()) return;

        QString providerName = showJson["provider"].toString();
        ShowProvider *provider = m_providerManager.getProvider(providerName);
        if (!provider) {
            ErrorHandler::instance().show(providerName + " does not exist", "Show Error");
            return;
        }

        ShowData show = ShowData::fromJson(showJson, provider);
        int lastWatchedIndex = showJson["lastWatchedIndex"].toInt();
        int timeStamp = showJson["timeStamp"].toInt(0);
        ShowData::LastWatchInfo lastWatchedInfo{ m_libraryManager.getCurrentListType(), lastWatchedIndex, timeStamp };
        lastWatchedInfo.playlist = m_playlistManager.findPlaylist(show.link);
        m_showManager.setIsLoadingFromLibrary(true);
        m_showManager.setShow(show, lastWatchedInfo);
    } else {
        ShowData show = m_searchResultManager.at(index);
        ShowData::LastWatchInfo lastWatchedInfo = m_libraryManager.getLastWatchInfo(show.link);
        lastWatchedInfo.playlist = m_playlistManager.findPlaylist(show.link);
        m_showManager.setIsLoadingFromLibrary(false);
        m_showManager.setShow(show, lastWatchedInfo);
    }
}

void Application::addCurrentShowToLibrary(int listType) {
    m_libraryManager.add (m_showManager.getShow(), listType); // Either changes the list type or adds to library
    m_showManager.setListType(listType);
}

void Application::removeCurrentShowFromLibrary() {
    m_libraryManager.remove (m_showManager.getShow());
    m_showManager.setListType(-1);
}

void Application::downloadCurrentShow(int startIndex, int count) {
    m_downloadManager.downloadShow (m_showManager.getShow(), startIndex, count); //TODO
}

void Application::playFromEpisodeList(int index) {

    auto showPlaylist = m_showManager.getPlaylist();

    if (m_playlistManager.isLoading()) {
        m_playlistManager.cancel();
        return;
    }

    updateTimeStamp();
    m_playlistManager.replaceMainPlaylist(showPlaylist);
    m_playlistManager.tryPlay(0, index);

}

void Application::continueWatching() {
    int index = m_showManager.getContinueIndex();
    if (index < 0) index = 0;
    playFromEpisodeList(index);
}

void Application::updateTimeStamp() {
    // Update the last play time
    auto lastPlaylist = m_playlistManager.getCurrentPlaylist();
    if (!lastPlaylist) return;
    auto time = MpvObject::instance()->time();
    qDebug() << "Log (App)        : Attempting to updating time stamp for" << lastPlaylist->link << "to" << time;

    if (lastPlaylist->isLoadedFromFolder()){
        lastPlaylist->updateHistoryFile(time);
    } else {
        if (time > 0.85 * MpvObject::instance()->duration() && lastPlaylist->currentIndex + 1 < lastPlaylist->size()) {
            qDebug() << "Log (App)        : Setting to next episode" << lastPlaylist->link;
            m_libraryManager.updateLastWatchedIndex (lastPlaylist->link, ++lastPlaylist->currentIndex);
        }
        else {
            m_libraryManager.updateTimeStamp(lastPlaylist->link, time);
        }
    }
}

void Application::updateLastWatchedIndex() {
    PlaylistItem *currentPlaylist = m_playlistManager.getCurrentPlaylist();
    auto showPlaylist = m_showManager.getPlaylist();
    if (!showPlaylist || !currentPlaylist) return;

    if (showPlaylist->link == currentPlaylist->link)
        m_showManager.updateLastWatchedIndex();

    m_libraryManager.updateLastWatchedIndex (currentPlaylist->link, currentPlaylist->currentIndex);
}


