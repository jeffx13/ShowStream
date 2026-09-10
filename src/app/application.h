#pragma once

#include <QObject>
#include <QGuiApplication>
#include <QClipboard>
#include <QFuture>

#include "app/logger.h"
#include "app/qmlsingleton.h"
#include "app/discordpresence.h"
#include "app/settings.h"
#include "library/downloadqueue.h"
#include "library/library.h"
#include "library/libraryproxymodel.h"
#include "media/playlist.h"
#include "media/skiptimes.h"
#include "providers/providerlist.h"
#include "ui/searchresults.h"
#include "ui/showdetails.h"
#include "ui/subtitlesearch.h"

class Application : public QObject
{
    Q_OBJECT
    Q_PROPERTY(ProviderList      *providers      READ providers      CONSTANT)
    Q_PROPERTY(ShowDetails       *show           READ show           CONSTANT)
    Q_PROPERTY(SearchResults     *explorer       READ explorer       CONSTANT)
    Q_PROPERTY(SearchResults     *migrateSearch  READ migrateSearch  CONSTANT)
    Q_PROPERTY(Library           *library        READ library        CONSTANT)
    Q_PROPERTY(LibraryProxyModel *libraryModel   READ libraryModel   CONSTANT)
    Q_PROPERTY(Playlist          *playlist       READ playlist       CONSTANT)
    Q_PROPERTY(SkipTimes         *skipTimes      READ skipTimes      CONSTANT)
    Q_PROPERTY(SubtitleSearch    *subtitleSearch READ subtitleSearch CONSTANT)
    Q_PROPERTY(DownloadQueue     *downloads      READ downloads      CONSTANT)
    Q_PROPERTY(LogListModel      *logList        READ logList        CONSTANT)
    Q_PROPERTY(Settings          *settings       READ settings       CONSTANT)

public:
    explicit Application(const QString &launchPath);
    ~Application();
    Application(const Application &) = delete;
    Application &operator=(const Application &) = delete;

    Q_INVOKABLE void search(const QString &query);
    Q_INVOKABLE void browse(bool latest);
    Q_INVOKABLE void loadShow(int index, bool fromLibrary);
    Q_INVOKABLE void reloadShow();
    Q_INVOKABLE void playFromEpisodeList(int index, bool append);
    Q_INVOKABLE void continueWatching();
    Q_INVOKABLE void addToLibrary(int index, int libraryType);
    Q_INVOKABLE void appendToPlaylists(int index, bool fromLibrary, bool play = false);
    Q_INVOKABLE void resumeFromHistory(const QString &link);
    Q_INVOKABLE void downloadCurrentShow(int startIndex, int endIndex = -1);
    Q_INVOKABLE void copyToClipboard(const QString &text) { QGuiApplication::clipboard()->setText(text); }

    Q_INVOKABLE void searchOnProvider(const QString &providerName, const QString &query);
    Q_INVOKABLE void migrateShow(int libraryIndex, int resultIndex, int resumeEpisode);

    void setFont(const QString &fontPath);

private:
    void checkForUpdates();
    static bool isNewerVersion(const QString &latest, const QString &current);

    ProviderList      *providers()      { return &m_providers; }
    ShowDetails       *show()           { return &m_show; }
    SearchResults     *explorer()       { return &m_explorer; }
    SearchResults     *migrateSearch()  { return &m_migrateSearch; }
    Library           *library()        { return &m_library; }
    LibraryProxyModel *libraryModel()   { return &m_libraryProxyModel; }
    Playlist          *playlist()       { return &m_playlist; }
    SkipTimes         *skipTimes()      { return &m_skipTimes; }
    SubtitleSearch    *subtitleSearch() { return &m_subtitleSearch; }
    DownloadQueue     *downloads()      { return &m_downloads; }
    LogListModel      *logList()        { return &QLog::logListModel; }
    Settings          *settings()       { return &Settings::instance(); }

    void loadResult(SearchResults &src, int index);
    void appendResult(SearchResults &src, int index, bool play);
    void openEntry(const LibraryEntry &entry, bool autoResume);

    // Destroyed last, so it outlives the models below whose workers are still in provider calls.
    ProviderList        m_providers{this};

    SearchResults       m_explorer;
    SearchResults       m_migrateSearch;
    bool                m_pendingAutoResume = false;

    Playlist            m_playlist;

    Library             m_library;
    LibraryProxyModel   m_libraryProxyModel;

    DownloadQueue       m_downloads;

    ShowDetails         m_show{this};
    SkipTimes           m_skipTimes{this};
    SubtitleSearch      m_subtitleSearch{this};
    DiscordPresence     m_discordPresence{this};

    // migrate() runs off-thread against a provider this object owns; closing mid-migration must stop it first.
    CancelToken         m_migrateCancel;
    QFuture<void>       m_migrateFuture;
};

DECLARE_QML_NAMED_SINGLETON(Application, App)
