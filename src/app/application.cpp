#include "app/application.h"
#include <QNetworkProxyFactory>
#include <QFontDatabase>
#include <QQuickStyle>
#include <QtConcurrent/QtConcurrentRun>
#include <QThreadPool>
#include <libxml/parser.h>
#include "app/async.h"
#include "app/logger.h"
#include "app/settings.h"
#include "ui/appshell.h"
#include "media/danmaku.h"
#include "net/client.h"
#include "net/hlsproxy.h"
#include "net/cloudflare.h"
#include "providers/anikoto.h"
#include "providers/bilibili.h"
#include "providers/iyf.h"
#include "providers/animepahe.h"
#include "providers/olevod.h"
#include "providers/allanime.h"
#include "providers/duboku.h"
#include "providers/pstream.h"
#include "providers/miruro.h"

namespace {
ShowData::WatchState watchStateOf(const LibraryEntry &e, int libraryType) {
    ShowData::WatchState watch;
    watch.libraryType      = libraryType;
    watch.lastWatchedIndex = e.lastWatchedIndex;
    watch.progress         = e.progress;
    return watch;
}
}

Application::Application(const QString &launchPath)
    : m_explorer(this)
    , m_playlist(this)
    , m_library(this)
    , m_libraryProxyModel(&m_library)
    , m_downloads(this)
{
    REGISTER_QML_SINGLETON(Application, this);
    REGISTER_QML_SINGLETON(AppShell, &AppShell::instance());
    REGISTER_QML_SINGLETON(Settings, &Settings::instance());
    AppShell::instance().installBackForwardFilter();

    xmlInitParser();
    QNetworkProxyFactory::setUseSystemConfiguration(true);
    // Solves run on worker threads; one outliving the window holds the process open.
    QObject::connect(qApp, &QCoreApplication::aboutToQuit, qApp, [] { Cloudflare::shutdown(); });
    new HlsProxy(this);
    DanmakuAss::pruneCache(Settings::tempDir() + QStringLiteral("/danmaku"));
    DanmakuAss::pruneCache(Settings::tempDir() + QStringLiteral("/subtitles"));
    m_libraryProxyModel.setSourceModel(&m_library);

    m_providers.setProviders({
        new Anikoto(this), new Bilibili(this), new Iyf(this), new AnimePahe(this), new Olevod(this),
        new AllAnime(this), new Duboku(this), new PStream(this), new Miruro(this),
    });

    if (!launchPath.isEmpty())
        m_playlist.openUrl(QUrl::fromUserInput(launchPath), false);

    connect(&m_playlist, &Playlist::progressUpdated,
            &m_library,  &Library::updateProgress);

    // The playing index has not changed, but "Continue from" moves to the next episode.
    connect(&m_playlist, &Playlist::progressUpdated,
            &m_show, [this](const QString &link, int, double) {
                if (m_show.show().link == link) m_show.onPlaybackIndexChanged();
            });

    connect(&m_playlist, &Playlist::episodeStarted,
            &m_library, &Library::recordHistory);

    connect(&m_playlist, &Playlist::currentItemChanged, this,
            [this](const QModelIndex &index) {
                auto *item = static_cast<PlaylistItem *>(index.internalPointer());
                m_skipTimes.onCurrentItemChanged(item);
                m_discordPresence.onCurrentItemChanged(item);
                m_show.onPlaybackIndexChanged();
            });

    connect(&m_library, &Library::fetchedAllEpCounts,
            &m_libraryProxyModel, &LibraryProxyModel::refreshFilter);

    connect(&m_show, &ShowDetails::showChanged, this, [this]() {
        const ShowData &show = m_show.show();
        m_library.updateShowCover(show.link, show.coverUrl);
        auto playlist = m_show.playlist();
        m_library.cacheHistoryMeta(show.link, show.title, show.coverUrl,
                                   show.provider ? show.provider->name() : QString(),
                                   playlist ? playlist->count() : 0);
        if (m_pendingAutoResume) {
            m_pendingAutoResume = false;
            continueWatching();
        }
    });

    std::setlocale(LC_NUMERIC, "C");
    QQuickStyle::setStyle("Universal");

    browse(true);
    checkForUpdates();
}

#ifndef APP_VERSION
#define APP_VERSION "0.0"
#endif

bool Application::isNewerVersion(const QString &latest, const QString &current) {
    const auto lp = latest.split('.');
    const auto cp = current.split('.');
    for (int i = 0; i < qMax(lp.size(), cp.size()); ++i) {
        int l = i < lp.size() ? lp[i].toInt() : 0;
        int c = i < cp.size() ? cp[i].toInt() : 0;
        if (l != c) return l > c;
    }
    return false;
}

void Application::checkForUpdates() {
    QThreadPool::globalInstance()->start([]() {
        Client client({}, false);
        auto resp = client.get("https://api.github.com/repos/jeffx13/AoNami/releases/latest",
                               {{"Accept", "application/vnd.github+json"}, {"User-Agent", "AoNami"}});
        auto obj = resp.toJsonObject();
        QString latest = obj.value("tag_name").toString();
        if (latest.startsWith('v') || latest.startsWith('V')) latest = latest.mid(1);
        if (latest.isEmpty() || !isNewerVersion(latest, QStringLiteral(APP_VERSION))) return;

        const QString url = obj.value("html_url").toString();
        QMetaObject::invokeMethod(&AppShell::instance(), [latest, url]() {
            AppShell::instance().reportInfo(
                QStringLiteral("A new version (%1) is available.\n%2").arg(latest, url),
                QStringLiteral("Update Available"));
        }, Qt::QueuedConnection);
    });
}

Application::~Application() {
    m_migrateCancel.cancel();
    waitFor(m_migrateFuture, "Application migrate");
    xmlCleanupParser();
}

void Application::setFont(const QString &fontPath) {
    int fontId = QFontDatabase::addApplicationFont(fontPath);
    auto families = QFontDatabase::applicationFontFamilies(fontId);
    if (fontId != -1 && !families.isEmpty())
        QGuiApplication::setFont(QFont(families.first(), 16));
}

void Application::search(const QString &query) {
    if (auto *provider = m_providers.currentProvider())
        m_explorer.search(query, 1, m_providers.currentTypeIndex(), provider);
}

void Application::browse(bool latest) {
    auto *provider = m_providers.currentProvider();
    if (!provider) return;
    const int type = m_providers.currentTypeIndex();
    if (latest) m_explorer.latest(1, type, provider);
    else        m_explorer.popular(1, type, provider);
}

void Application::loadResult(SearchResults &src, int index) {
    ShowData show = src.resultAt(index);
    ShowData::WatchState watch = m_library.watchState(show.link);
    watch.playlist = m_playlist.find(show.link);
    m_show.setShow(show, watch);
}

void Application::appendResult(SearchResults &src, int index, bool play) {
    auto show = src.resultAt(index);
    if (!show.provider) return;
    ShowData::WatchState watch = m_library.watchState(show.link);
    QSharedPointer<PlaylistItem> cached;
    if (m_show.show().link == show.link)
        cached = m_show.playlist();
    m_playlist.appendShow(show.title, show.link, show.provider, cached, watch, play);
}

void Application::openEntry(const QString &title, const QString &link, const QString &cover,
                            const QString &providerName, ShowData::WatchState watch, bool autoResume) {
    if (m_show.show().link == link) {
        m_pendingAutoResume = false;
        if (autoResume) continueWatching();
        else            AppShell::instance().navigateTo(AppShell::Page::Info);
        return;
    }

    auto *provider = m_providers.byName(providerName);
    if (!provider) {
        AppShell::instance().reportError(providerName + " does not exist", "Show Error");
        return;
    }
    ShowData show(title, link, cover, provider);
    watch.playlist = m_playlist.find(link);
    m_pendingAutoResume = autoResume;
    m_show.setShow(show, watch);
}

void Application::reloadShow() {
    const ShowData current = m_show.show();
    if (current.link.isEmpty() || !current.provider) return;

    ShowData show(current.title, current.link, current.coverUrl, current.provider,
                  current.latestTxt, current.type);
    ShowData::WatchState watch = m_library.watchState(current.link);
    watch.playlist = m_playlist.find(current.link);
    m_show.reload(show, watch);
}

void Application::loadShow(int index, bool fromLibrary) {
    m_pendingAutoResume = false;
    if (!fromLibrary) { loadResult(m_explorer, index); return; }

    auto entry = m_library.entryAt(index);
    if (!entry.valid) return;
    openEntry(entry.title, entry.link, entry.cover, entry.provider,
              watchStateOf(entry, m_library.displayLibraryType()), false);
}

void Application::resumeFromHistory(const QString &link) {
    if (auto entry = m_library.entryForLink(link); entry.valid) {
        openEntry(entry.title, entry.link, entry.cover, entry.provider,
                  watchStateOf(entry, entry.libraryType), true);
        return;
    }
    auto h = m_library.historyEntry(link);
    if (!h.valid) return;
    ShowData::WatchState watch;
    watch.lastWatchedIndex = h.lastWatchedIndex;
    watch.progress         = h.progress;
    openEntry(h.title, link, h.cover, h.provider, watch, true);
}

void Application::addToLibrary(int index, int libraryType) {
    auto show = (index == -1) ? m_show.show() : m_explorer.resultAt(index);
    m_library.add(show, libraryType);
}

void Application::searchOnProvider(const QString &providerName, const QString &query) {
    ShowProvider *provider = m_providers.byName(providerName);
    if (!provider) {
        AppShell::instance().reportError(providerName + " does not exist", "Migrate");
        return;
    }
    m_migrateSearch.search(query, 1, 0, provider);   // type 0 - providers don't share type indices
}

void Application::migrateShow(int libraryIndex, int resultIndex, int resumeEpisode) {
    const auto oldEntry = m_library.entryAt(libraryIndex);
    if (!oldEntry.valid) { AppShell::instance().reportError("Library item not found", "Migrate"); return; }
    if (resultIndex < 0 || resultIndex >= m_migrateSearch.count()) {
        AppShell::instance().reportError("No show selected", "Migrate"); return;
    }
    ShowData newShow = m_migrateSearch.resultAt(resultIndex);
    if (!newShow.provider) { AppShell::instance().reportError("Selected show has no provider", "Migrate"); return; }

    const QString oldLink = oldEntry.link;
    if (m_playlist.isPlaying(oldLink)) {
        AppShell::instance().reportError("This show is playing right now - stop it first.", "Migrate");
        return;
    }
    if (newShow.link != oldLink && m_library.linkExists(newShow.link)) {
        AppShell::instance().reportError("That show is already in your library.", "Migrate");
        return;
    }

    if (m_migrateFuture.isRunning()) {
        AppShell::instance().reportError("A migration is already running.", "Migrate");
        return;
    }

    ShowProvider *provider = newShow.provider;

    // last_watched_index is positional, so the episode has to be found by number in the new playlist.
    m_migrateCancel.reset();
    m_migrateFuture = QtConcurrent::run([this, newShow, oldLink, resumeEpisode, provider,
                                         cancel = m_migrateCancel]() mutable {
        Client client(cancel);
        try {
            provider->loadPlaylist(&client, newShow);
        } catch (const std::exception &e) {
            // Without this the future is discarded and Migrate just never finishes.
            const QString msg = QString::fromUtf8(e.what());
            QMetaObject::invokeMethod(&AppShell::instance(), [msg]() {
                AppShell::instance().reportError("Could not load the show on that provider:\n" + msg, "Migrate");
            }, Qt::QueuedConnection);
            return;
        } catch (...) {
            logWarn() << "Migrate" << "provider threw a non-standard exception";
            return;
        }
        if (cancel.isCancelled()) return;

        auto playlist = newShow.playlist();
        const int total = playlist ? playlist->count() : 0;
        int targetIndex = qBound(0, resumeEpisode - 1, total > 0 ? total - 1 : 0);
        if (playlist) {
            for (int i = 0; i < playlist->count(); ++i) {
                auto ep = playlist->at(i);
                if (ep && int(ep->number) == resumeEpisode) { targetIndex = i; break; }
            }
        }
        const QString title = newShow.title, cover = newShow.coverUrl, newLink = newShow.link;
        const QString provName = provider->name();
        const int showType = newShow.type;
        QMetaObject::invokeMethod(this, [=, this]() {
            bool ok = m_library.migrate(oldLink, newLink, title, cover, provName, showType,
                                        targetIndex, total);
            if (!ok) {
                AppShell::instance().reportError("Migration failed (target may already be in the library).", "Migrate");
                return;
            }
            Settings &s = Settings::instance();
            const QString skipVal = s.value(Config::skipProfile(oldLink)).toString();
            if (!skipVal.isEmpty()) s.setValue(Config::skipProfile(newLink), skipVal);
            const QString malVal = s.value(Config::skipMal(oldLink)).toString();
            if (!malVal.isEmpty()) s.setValue(Config::skipMal(newLink), malVal);

            auto migrated = newShow.playlist();
            if (migrated) migrated->setCurrentIndex(targetIndex);
            m_playlist.rekey(oldLink, migrated);
            if (newLink != oldLink && m_show.show().link == oldLink) {
                ShowData::WatchState watch = m_library.watchState(newLink);
                watch.playlist = migrated;
                m_show.setShow(newShow, watch, false);
            }
            AppShell::instance().reportInfo("Migrated to " + provName + ".", "Migrate");
        }, Qt::QueuedConnection);
    });
}

void Application::playFromEpisodeList(int index, bool append) {
    auto playlist = m_show.playlist();
    if (!playlist) return;
    playlist->setCurrentIndex(index);

    if (append) {
        m_playlist.append(playlist);
        return;
    }

    playlist->season = -1;
    auto first = m_playlist.root()->at(0);
    // Both return the *existing* row for an already-queued show, so playing row 0 blindly starts the wrong one.
    const int row = (first && first->season == -1) ? m_playlist.replace(0, playlist)
                                                   : m_playlist.insert(0, playlist);
    if (row < 0) return;
    m_playlist.playAt(row);
}

void Application::continueWatching() {
    playFromEpisodeList(qMax(m_show.continueIndex(), 0), false);
}

void Application::downloadCurrentShow(int startIndex, int endIndex) {
    if (endIndex < 0) endIndex = startIndex;
    m_downloads.downloadShow(m_show.show(), startIndex, endIndex);
}

void Application::appendToPlaylists(int index, bool fromLibrary, bool play) {
    if (!fromLibrary) { appendResult(m_explorer, index, play); return; }

    auto entry = m_library.entryAt(index);
    if (!entry.valid) return;
    auto *provider = m_providers.byName(entry.provider);
    if (!provider) {
        AppShell::instance().reportError(entry.provider + " does not exist", "Show Error");
        return;
    }
    ShowData::WatchState watch;
    watch.lastWatchedIndex = entry.lastWatchedIndex;
    watch.progress = entry.progress;

    QSharedPointer<PlaylistItem> cached;
    if (m_show.show().link == entry.link)
        cached = m_show.playlist();

    m_playlist.appendShow(entry.title, entry.link, provider, cached, watch, play);
}