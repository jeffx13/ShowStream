#include "media/playlist.h"
#include "app/async.h"
#include "app/logger.h"
#include "app/exception.h"
#include "media/mpvplayer.h"
#include "providers/showprovider.h"
#include "ui/appshell.h"
#include "media/serverselector.h"
#include "net/cloudflare.h"
#include "media/localmedia.h"
#include "app/settings.h"
#include "app/config.h"
#include <QtConcurrent/QtConcurrentRun>
#include <QDateTime>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <algorithm>
#include <QMetaObject>

Playlist::Playlist(QObject *parent) : QAbstractItemModel(parent) {
    registerPlaylist(m_root);
    connect(&m_folderWatcher, &QFileSystemWatcher::directoryChanged, this, &Playlist::onLocalDirectoryChanged);
    connect(&m_watcher, &QFutureWatcher<PlayInfo>::finished, this, &Playlist::onPlayFinished);
    // Ordered after onPlayFinished so isLoading reads the post-handoff state.
    connect(&m_watcher, &QFutureWatcher<PlayInfo>::started,  this, &Playlist::isLoadingChanged);
    connect(&m_watcher, &QFutureWatcher<PlayInfo>::finished, this, &Playlist::isLoadingChanged);

    m_prefetchTimer.setSingleShot(true);
    m_prefetchTimer.setInterval(4000);   // let the current episode buffer before resolving the next
    connect(&m_prefetchTimer, &QTimer::timeout, this, &Playlist::startNextEpisodePrefetch);
}

void Playlist::onPlayFinished() {
    if (!m_cancel.isCancelled()) {
        try {
            auto playItem = m_watcher.result();
            if (auto *mpv = MpvPlayer::instance()) mpv->open(playItem);
        } catch (AppException &ex) {
            ex.report();
        } catch (const std::runtime_error &ex) {
            AppShell::instance().reportError(ex.what(), "Playlist Error");
        } catch (...) {
            AppShell::instance().reportError("Something went wrong", "Playlist Error");
        }
    }
    m_cancel.reset();

    if (m_pendingServerIndex >= 0) {
        int idx = m_pendingServerIndex;
        m_pendingServerIndex = -1;
        loadServer(idx);
    } else if (m_pendingItem) {
        auto item = m_pendingItem;
        m_pendingItem.clear();
        tryPlay(item);
    }
}

Playlist::~Playlist() {
    disconnect(&m_watcher, &QFutureWatcher<PlayInfo>::finished, this, nullptr);
    m_cancel.cancel();
    m_appendCancel.cancel();
    m_bgCacheCancel.cancel();
    m_prefetchCancel.cancel();
    waitFor(m_prefetchFuture, "Playlist prefetch");
    waitFor(m_watcher,        "Playlist play");
    waitFor(m_appendFuture,   "Playlist append");
    waitFor(m_bgCacheFuture,  "Playlist server cache");
}

QSharedPointer<PlaylistItem> Playlist::find(const QString &link) {
    auto it = m_byLink.find(link);
    return it != m_byLink.end() ? it.value().toStrongRef() : nullptr;
}

int Playlist::append(const QSharedPointer<PlaylistItem> &playlist, const QSharedPointer<PlaylistItem> &parent) {
    return insert(INT_MAX, playlist, parent);
}

int Playlist::insert(int index, const QSharedPointer<PlaylistItem> &playlist, const QSharedPointer<PlaylistItem> &parent) {
    if (!playlist) return -1;
    auto actualParent = parent ? parent : m_root;
    if (!actualParent->isList()) return -1;

    auto existingPlaylist = m_byLink.value(playlist->link, QWeakPointer<PlaylistItem>()).toStrongRef();
    if (existingPlaylist)
        return existingPlaylist->row();

    registerPlaylist(playlist);
    index = qBound(0, index, actualParent->count());
    beginInsertRows(indexFor(actualParent.data()), index, index);
    actualParent->insert(index, playlist);
    endInsertRows();

    auto currentItem = m_currentItem.toStrongRef();
    setCurrentItem(currentItem);
    return index;
}

bool Playlist::isPlaying(const QString &link) const {
    if (link.isEmpty()) return false;
    for (auto item = m_currentItem.toStrongRef(); item; item = item->parent())
        if (item->link == link) return true;
    return false;
}

void Playlist::rekey(const QString &oldLink, const QSharedPointer<PlaylistItem> &newPlaylist) {
    auto stale = find(oldLink);
    if (!stale) return;
    auto parent = stale->parent();
    if (newPlaylist && replace(stale->row(), newPlaylist, parent == m_root ? nullptr : parent) != -1)
        return;
    remove(indexFor(stale.data()));
}

int Playlist::replace(int index, const QSharedPointer<PlaylistItem> &playlist, const QSharedPointer<PlaylistItem> &parent) {
    if (!playlist) return -1;

    auto existingPlaylist = m_byLink.value(playlist->link, QWeakPointer<PlaylistItem>()).toStrongRef();
    if (existingPlaylist)
        return existingPlaylist->row();

    if (parent) {
        auto existingParent = m_byLink.value(parent->link, QWeakPointer<PlaylistItem>()).toStrongRef();
        if (existingParent && existingParent != parent) return -1;
    }
    auto actualParent = parent ? parent : m_root;
    if (!actualParent->isList()) return -1;
    if (!actualParent->isValidIndex(index)) {
        logError() << "Playlist" << "Invalid index:" << index << "to replace";
        return -1;
    }

    deregisterPlaylist(actualParent->at(index));
    registerPlaylist(playlist);

    auto currentItem = m_currentItem.toStrongRef();
    bool currentPlaylistReplaced = currentItem && currentItem->parent() == actualParent->at(index);
    if (currentPlaylistReplaced)
        saveProgress();

    beginRemoveRows(indexFor(actualParent.data()), index, index);
    actualParent->removeAt(index);
    endRemoveRows();
    beginInsertRows(indexFor(actualParent.data()), index, index);
    actualParent->insert(index, playlist);
    endInsertRows();

    if (currentPlaylistReplaced)
        setCurrentItem(playlist->currentItem());

    return index;
}

void Playlist::remove(const QModelIndex &index) {
    auto item = static_cast<PlaylistItem*>(index.internalPointer());
    if (!item) return;
    auto parent = item->parent();
    int row = index.row();
    if (!parent || (parent->currentIndex() != -1 && parent->currentItem().data() == item)) return;

    if (item->isList())
        deregisterPlaylist(item->sharedFromThis());

    beginRemoveRows(indexFor(parent.data()), row, row);
    parent->removeAt(row);
    endRemoveRows();

    if (parent->isEmpty()) {
        auto grandparent = parent->parent();
        if (grandparent) {
            int parentRow = parent->row();
            beginRemoveRows(indexFor(grandparent.data()), parentRow, parentRow);
            grandparent->removeAt(parentRow);
            endRemoveRows();
        }
    }

    auto currentItem = m_currentItem.toStrongRef();
    setCurrentItem(currentItem);
}

void Playlist::clear() {
    auto currentPlaylist = m_root->currentItem();

    for (const auto &playlist : m_root->children())
        if (playlist != currentPlaylist)
            deregisterPlaylist(playlist);
    // The reset must bracket the mutation: clear() frees children still inside live QModelIndexes.
    beginResetModel();
    m_root->clear();
    if (currentPlaylist)
        m_root->append(currentPlaylist);
    endResetModel();
    m_root->setCurrentIndex(currentPlaylist ? 0 : -1);

    auto currentItem = m_currentItem.toStrongRef();
    setCurrentItem(currentItem);
}

bool Playlist::playAt(int index) {
    if (m_root->isEmpty()) return false;
    auto playlist = m_root->at(index);
    if (!playlist) return false;
    int itemIndex = (playlist->currentIndex() == -1) ? 0 : playlist->currentIndex();
    return tryPlay(playlist->at(itemIndex));
}

void Playlist::stepItem(int offset) {
    auto currentItem = m_currentItem.toStrongRef();
    if (!currentItem) {
        tryPlay(m_root->at(0));
        return;
    }
    auto playlist = currentItem->parent();
    if (!playlist) {
        logError() << "Playlist" << currentItem->link << "does not belong to a playlist";
        return;
    }

    int nextItemIndex = currentItem->row() + offset;

    auto parentPlaylist = playlist->parent();
    if (parentPlaylist) {
        int playlistIndex = playlist->row();
        if (nextItemIndex == playlist->count() && playlistIndex + 1 < parentPlaylist->count()) {
            stepPlaylist(1);
            return;
        } else if (nextItemIndex < 0 && playlistIndex - 1 >= 0) {
            stepPlaylist(-1);
            return;
        }
    }
    tryPlay(playlist->at(nextItemIndex));
}

void Playlist::stepPlaylist(int offset) {
    auto currentItem = m_currentItem.toStrongRef();
    if (!currentItem || !currentItem->parent()) {
        tryPlay(m_root->at(0));
        return;
    }
    auto playlist = currentItem->parent();
    auto parentPlaylist = playlist->parent();
    if (!parentPlaylist) return;

    auto nextPlaylist = parentPlaylist->at(playlist->row() + offset);
    if (!nextPlaylist) return;

    int nextItemIndex = 0;
    if (nextPlaylist->currentIndex() != -1) {
        nextItemIndex = nextPlaylist->currentIndex();
    } else {
        for (int i = 0; i < nextPlaylist->count(); ++i) {
            if (!nextPlaylist->at(i)->isList()) {
                nextItemIndex = i;
                break;
            }
        }
    }
    tryPlay(nextPlaylist->at(nextItemIndex));
}

void Playlist::loadIndex(const QModelIndex &index) {
    auto item = static_cast<PlaylistItem*>(index.internalPointer());
    if (!item || item->isList()) return;
    auto playlist = item->parent();
    if (playlist) tryPlay(playlist->at(item->row()));
}

void Playlist::reload() {
    auto currentItem = m_currentItem.toStrongRef();
    if (!currentItem) return;
    if (auto *mpv = MpvPlayer::instance(); mpv && mpv->duration() > 0)
        currentItem->setProgress(double(mpv->time()) / double(mpv->duration()));
    tryPlay(currentItem);
}

void Playlist::loadServer(int index) {
    if (m_watcher.isRunning()) {
        m_pendingServerIndex = index;
        m_cancel.cancel();
        return;
    }
    if (!m_serverListModel.isValidIndex(index)) return;
    VideoServer server = m_serverListModel.at(index);
    ShowProvider *provider = m_serverListModel.provider();
    if (!provider) return;

    logInfo() << "Server" << "Switching to" << server.name;

    if (const auto *cached = m_serverListModel.cachedSource(server.name)) {
        PlayInfo playItem = *cached;
        if (auto *mpv = MpvPlayer::instance()) {
            // The cached entry skips the probe, so refresh the clearance headers; its cookie may be stale.
            if (!playItem.videos.isEmpty())
                Cloudflare::applyClearanceHeaders(playItem.videos.first().url, playItem.headers);
            if (mpv->duration() > 0)
                playItem.progress = double(mpv->time()) / double(mpv->duration());
            mpv->open(playItem);
        }
        m_serverListModel.setCurrentIndex(index);
        m_serverListModel.setPreferredServer(index);
        logInfo() << "Server" << "Loaded" << server.name << "(cached)";
        return;
    }

    m_cancel.reset();
    m_watcher.setFuture(QtConcurrent::run([this, index, server, provider]() {
        Client client(m_cancel);
        PlayInfo playItem = provider->extractSource(&client, server);
        const auto verdict = ServerSelector::playability(&client, playItem);
        if (verdict != ServerSelector::Playability::Playable && !client.isCancelled()) {
            playItem.clear();
            if (verdict == ServerSelector::Playability::Broken) {
                logWarn() << "Server" << server.name << "is broken";
                QMetaObject::invokeMethod(this, [this, name = server.name]() {
                    m_serverListModel.markBroken(name);
                }, Qt::QueuedConnection);
            } else {
                logWarn() << "Server" << server.name << "did not answer - left unchecked";
            }
        }
        if (playItem.videos.isEmpty()) {
            logWarn() << "Server" << QString("Failed to load server %1").arg(server.name);
            return playItem;
        }
        if (auto *mpv = MpvPlayer::instance(); mpv && mpv->duration() > 0)
            playItem.progress = double(mpv->time()) / double(mpv->duration());
        QMetaObject::invokeMethod(this, [this, serverName = server.name, playItem]() {
            // cacheSource resorts when a broken server recovers, so select by name, not the captured index.
            m_serverListModel.cacheSource(serverName, playItem);
            m_serverListModel.setCurrentServer(serverName);
            logInfo() << "Server" << "Loaded" << serverName;
        }, Qt::QueuedConnection);
        return playItem;
    }));
}

void Playlist::tryNextServer() {
    if (m_watcher.isRunning()) return;
    const int current = m_serverListModel.currentIndex();
    if (m_serverListModel.isValidIndex(current))
        m_autoTriedServers.insert(m_serverListModel.at(current).name);

    for (int i = 0; i < m_serverListModel.count(); ++i) {
        if (i == current) continue;
        const QString name = m_serverListModel.at(i).name;
        if (m_autoTriedServers.contains(name)) continue;
        if (!m_serverListModel.cachedSource(name)) continue;   // only known-working servers
        logOk() << "Server" << "Playback failed - auto-switching to" << name;
        loadServer(i);
        return;
    }
    logError() << "Server" << "Playback failed and no other working server is available";
}

void Playlist::cancel() {
    if (m_watcher.isRunning()) {
        m_cancel.cancel();
    } else if (auto *mpv = MpvPlayer::instance()) {
        if (mpv->isLoading()) mpv->stop();
    }
}

void Playlist::cacheRemainingServers() {
    if (m_bgCacheFuture.isRunning()) return;
    ShowProvider *provider = m_serverListModel.provider();
    if (!provider) return;

    QList<VideoServer> toCheck;
    for (int i = 0; i < m_serverListModel.count(); ++i) {
        const auto &server = m_serverListModel.at(i);
        if (!m_serverListModel.cachedSource(server.name))
            toCheck.append(server);
    }
    if (toCheck.isEmpty()) return;

    m_bgCacheCancel.reset();
    m_bgCacheFuture = QtConcurrent::run([this, toCheck, provider]() {
        QList<QFuture<void>> jobs;
        jobs.reserve(toCheck.size());
        for (const VideoServer &server : toCheck) {
            jobs.push_back(QtConcurrent::run([this, server, provider]() {
                if (m_bgCacheCancel.isCancelled()) return;
                // A throw means the check never ran, so it condemns nothing.
                auto verdict = ServerSelector::Playability::Unknown;
                PlayInfo playInfo;
                try {
                    Client client(m_bgCacheCancel);
                    playInfo = provider->extractSource(&client, server);
                    if (!m_bgCacheCancel.isCancelled())
                        verdict = ServerSelector::playability(&client, playInfo);
                } catch (AppException &e) {
                    logWarn() << "Server" << server.name << "background cache failed:" << e.what();
                } catch (const std::exception &e) {
                    logWarn() << "Server" << server.name << "background cache failed:" << e.what();
                }
                if (m_bgCacheCancel.isCancelled()) return;
                QMetaObject::invokeMethod(this, [this, name = server.name, playInfo, verdict]() {
                    if (m_bgCacheCancel.isCancelled()) return;
                    if (verdict == ServerSelector::Playability::Playable)
                        m_serverListModel.cacheSource(name, std::move(playInfo));
                    else if (verdict == ServerSelector::Playability::Broken)
                        m_serverListModel.markBroken(name);
                }, Qt::QueuedConnection);
            }));
        }
        for (auto &j : jobs) j.waitForFinished();
    });
}

void Playlist::appendShow(const QString &title, const QString &link, ShowProvider *provider,
                                 QSharedPointer<PlaylistItem> cached, const ShowData::WatchState &watch, bool play) {
    if (m_appendFuture.isRunning()) return;
    if (!cached) cached = find(link);

    auto commit = [this](const QSharedPointer<PlaylistItem> &pl, const ShowData::WatchState &state, bool doPlay) {
        if (state.lastWatchedIndex != -1) {
            pl->setCurrentIndex(state.lastWatchedIndex);
            if (auto item = pl->currentItem()) item->setProgress(state.progress);
        }
        int idx = append(pl);
        if (doPlay) playAt(idx);
    };

    if (cached) { commit(cached, watch, play); return; }

    m_appendCancel.reset();
    m_appendFuture = QtConcurrent::run([this, title, link, provider, watch, play, commit]() {
        Client client(m_appendCancel);
        ShowData dummy(title, link, "", provider);
        provider->loadPlaylist(&client, dummy);
        auto playlist = dummy.playlist();
        if (!m_appendCancel.isCancelled() && playlist) {
            QMetaObject::invokeMethod(this, [this, playlist, watch, play, commit]() {
                if (!m_appendCancel.isCancelled()) commit(playlist, watch, play);
            }, Qt::QueuedConnection);
        }
    });
}

void Playlist::setCurrentItem(const QSharedPointer<PlaylistItem> &item) {
    if (!item || item->isList()) {
        m_currentItem = QWeakPointer<PlaylistItem>();
        emit currentItemChanged(QModelIndex());
        if (item && item->isList())
            logWarn() << "Playlist" << "Cannot set current item to a list" << item->link;
        return;
    }
    m_currentItem = item;
    m_currentCompleted = false;        // new episode - completion re-evaluated from its position
    // finalizePlayback has just stamped this item; don't let the periodic save fire immediately.
    m_lastProgressSaveMs = QDateTime::currentMSecsSinceEpoch();
    ensureMpvProgressConnection();

    if (auto *mpv = MpvPlayer::instance()) {
        auto p = item->parent();
        mpv->setShowKey(p ? p->link : item->link);
        mpv->setEpisodeKey(item->link);   // a fetched subtitle only fits the episode it was for
    }

    int row = item->row();
    auto parent = item->parent();
    while (parent) {
        parent->setCurrentIndex(row);
        row = parent->row();
        parent = parent->parent();
    }
    emit currentItemChanged(indexFor(m_currentItem.toStrongRef().data()));
}

void Playlist::showCurrentItemName() const {
    auto currentItem = m_currentItem.toStrongRef();
    if (!currentItem) return;
    auto playlist = currentItem->parent();
    if (!playlist) return;

    QString path = playlist->name;
    auto current = playlist->parent();
    while (current && current != m_root) {
        path = current->name + " | " + path;
        current = current->parent();
    }
    QString displayText = QString("%1\n[%2/%3] %4\n%5")
                              .arg(path,
                                   QString::number(playlist->currentIndex() + 1),
                                   QString::number(playlist->count()),
                                   currentItem->displayName.simplified(),
                                   QDateTime::currentDateTime().toString("dd/MM/yyyy HH:mm:ss"));
    if (auto *mpv = MpvPlayer::instance()) mpv->showText(displayText);
}

void Playlist::saveProgress(bool quiet) const {
    auto currentItem = m_currentItem.toStrongRef();
    if (!currentItem || currentItem->preview) return;   // preview/trailer episodes get no resume point
    auto playlist = currentItem->parent();
    if (!playlist || !playlist->isList()) return;

    int row = currentItem->row();
    auto *mpv = MpvPlayer::instance();
    if (!mpv) return;

    const double duration = mpv->duration();
    const double progress = duration > 0 ? qBound(0.0, mpv->time() / duration, 1.0) : 0.0;
    if (!quiet)
        logInfo() << "Playlist" << playlist->name << "Saving | Index =" << row
               << "| Progress =" << QString::number(progress * 100, 'f', 1) + "%";

    currentItem->setProgress(progress);
    if (playlist->isLocalDir())
        emit localProgressUpdated(currentItem->link, playlist->link, progress);
    emit progressUpdated(playlist->link, row, progress);
}

void Playlist::ensureMpvProgressConnection() {
    if (m_mpvProgressConnected) return;
    auto *mpv = MpvPlayer::instance();
    if (!mpv) return;
    connect(mpv, &MpvPlayer::timeChanged,     this, &Playlist::onPlaybackProgress);
    connect(mpv, &MpvPlayer::durationChanged, this, &Playlist::onPlaybackProgress);
    m_mpvProgressConnected = true;
}

void Playlist::onPlaybackProgress() {
    auto currentItem = m_currentItem.toStrongRef();
    if (!currentItem || currentItem->preview) return;
    auto playlist = currentItem->parent();
    if (!playlist || !playlist->isList()) return;
    auto *mpv = MpvPlayer::instance();
    if (!mpv) return;
    const double duration = mpv->duration();
    if (duration <= 0) return;
    const double progress = qBound(0.0, mpv->time() / duration, 1.0);
    const bool completed = progress >= Settings::instance().watchedFraction();

    // No timer needed: timeChanged already ticks about once a second and stops while paused.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastProgressSaveMs >= kProgressSaveIntervalMs) {
        m_lastProgressSaveMs = now;
        m_currentCompleted = completed;
        saveProgress(true);
        return;
    }

    if (completed == m_currentCompleted) return;   // only act on a threshold crossing
    m_currentCompleted = completed;
    currentItem->setProgress(progress);
    emit progressUpdated(playlist->link, currentItem->row(), progress);
}

bool Playlist::tryPlay(const QSharedPointer<PlaylistItem> &item) {
    if (!item) return false;

    auto resolvedItem = item;

    auto parent = resolvedItem->isList() ? nullptr : resolvedItem->parent();
    auto owner = resolvedItem->isList() ? resolvedItem : parent;
    if (!owner) {
        logError() << "Playlist" << resolvedItem->link << "has no parent playlist";
        return false;
    }
    QString link = owner->link;
    auto playlist = !link.isEmpty() ? m_byLink.value(link, QWeakPointer<PlaylistItem>()).toStrongRef() : nullptr;
    if (!playlist) {
        logError() << "Playlist" << link << "is not registered";
        return false;
    }

    if (!resolvedItem->isList() && parent != playlist) {
        logWarn() << "Playlist" << "Item does not belong to registered playlist";
        int itemIndex = playlist->indexOf(resolvedItem->link);
        if (itemIndex != -1) {
            resolvedItem = playlist->at(itemIndex);
        } else {
            logError() << "Item does not belong to registered playlist";
            return false;
        }
    }

    // Resolve to a playable leaf on the main thread; the worker must not race tree mutations.
    resolvedItem = resolveToPlayableItem(resolvedItem);
    if (!resolvedItem) return false;

    // Keep the leaf's parent alive across the worker (main thread may replace/remove).
    auto playlistRef = resolvedItem->parent();
    if (!playlistRef) return false;

    if (tryUsePrefetch(resolvedItem)) return true;

    if (m_watcher.isRunning()) {
        m_pendingItem = resolvedItem;
        m_cancel.cancel();
        return false;
    }

    // A different episode than any prefetch -> drop the stale one.
    m_prefetchCancel.cancel();
    m_prefetch = {};

    m_pendingItem.clear();
    m_pendingServerIndex = -1;
    m_cancel.reset();

    auto currentItem = m_currentItem.toStrongRef();
    if (currentItem && currentItem != resolvedItem)
        saveProgress();

    m_bgCacheCancel.cancel();
    m_serverListModel.clear();

    m_watcher.setFuture(QtConcurrent::run([this, resolvedItem, playlistRef]() {
        return this->resolvePlayback(resolvedItem);
    }));
    return true;
}

PlayInfo Playlist::resolvePlayback(const QSharedPointer<PlaylistItem> &item) {
    auto playlist = item->parent();
    if (!playlist || !playlist->isList()) {
        logError() << "Playlist" << item->name << "does not belong to any playlist!";
        return {};
    }

    PlayInfo playInfo = loadPlayInfo(item);
    if (playInfo.videos.isEmpty() && item->type != PlaylistItem::Local)
        return {};

    finalizePlayback(item);
    playInfo.progress = item->progress();
    return playInfo;
}

QSharedPointer<PlaylistItem> Playlist::resolveToPlayableItem(QSharedPointer<PlaylistItem> item) {
    while (item && item->isList()) {
        if (item->isEmpty()) return {};

        auto currentItem = item->currentItem();
        if (currentItem) {
            item = currentItem;
            continue;
        }

        QSharedPointer<PlaylistItem> firstPlayable;
        for (const auto &child : item->children())
            if (!child->isList()) { firstPlayable = child; break; }
        item = firstPlayable ? firstPlayable : item->first();
    }
    return item;
}

PlayInfo Playlist::loadPlayInfo(const QSharedPointer<PlaylistItem> &item) {
    switch (item->type) {
    case PlaylistItem::Pasted: return loadPastedPlayInfo(item);
    case PlaylistItem::Online: return loadOnlinePlayInfo(item);
    case PlaylistItem::Local:  return loadLocalPlayInfo(item);
    default: return {};
    }
}

PlayInfo Playlist::loadPastedPlayInfo(const QSharedPointer<PlaylistItem> &item) {
    PlayInfo playInfo;
    if (item->link.contains('|')) {
        QStringList parts = item->link.split('|');
        playInfo.videos.emplaceBack(parts.takeFirst());
        for (const QString &headerLine : std::as_const(parts)) {
            QStringList keyValue = headerLine.split(": ", Qt::KeepEmptyParts);
            if (keyValue.size() == 2)
                playInfo.headers.insert(keyValue[0].trimmed(), keyValue[1].trimmed());
        }
    } else {
        playInfo.videos.emplaceBack(item->link);
    }
    return playInfo;
}

PlayInfo Playlist::loadOnlinePlayInfo(const QSharedPointer<PlaylistItem> &item) {
    auto playlist = item->parent();
    if (!playlist)
        throw AppException("Playlist was removed during playback", "Playlist");
    auto provider = playlist->provider();
    if (!provider)
        throw AppException("Cannot get provider from playlist!", "Provider");

    QString label = item->displayName;
    label.replace('\n', " - ");
    if (!playlist->name.isEmpty()) label = playlist->name + " " + label;
    label += " (" + provider->name() + ")";

    Client client(m_cancel);
    auto servers = provider->loadServers(&client, item.data());
    if (servers.isEmpty())
        throw AppException("No servers found for " + label, "Server");

    std::sort(servers.begin(), servers.end(),
              [](const VideoServer &a, const VideoServer &b) {
                  return a.name < b.name;
              });

    auto result = ServerSelector::findWorkingServer(&client, provider, servers);
    if (!result.found())
        throw AppException(QString("No working server found for %1 (tried %2)")
                               .arg(label).arg(servers.size()), "Server");

    if (m_cancel.isCancelled()) return {};

    int chosenIndex = result.index;
    QMetaObject::invokeMethod(this, [this, servers, provider, chosenIndex,
                                     cache = std::move(result.cachedSources)]() mutable {
        applyServers(servers, provider, chosenIndex, std::move(cache));
    }, Qt::QueuedConnection);

    return result.playInfo;
}

void Playlist::applyServers(const QList<VideoServer> &servers, ShowProvider *provider,
                                        int chosenIndex, QHash<QString, PlayInfo> cache) {
    m_autoTriedServers.clear();   // fresh episode - every server gets a chance again
    const QString winnerName = (chosenIndex >= 0 && chosenIndex < servers.size())
                                   ? servers[chosenIndex].name : QString();
    m_serverListModel.setServers(servers, provider);   // sorts internally
    m_serverListModel.setCachedSources(std::move(cache));
    m_serverListModel.setCurrentServer(winnerName);    // locate the winner by name post-sort
    cacheRemainingServers();
}

// Next playable leaf in the same playlist; cross-playlist boundaries aren't prefetched.
QSharedPointer<PlaylistItem> Playlist::nextItem() const {
    auto cur = m_currentItem.toStrongRef();
    if (!cur) return {};
    auto pl = cur->parent();
    if (!pl) return {};
    int ni = cur->row() + 1;
    if (ni < 0 || ni >= pl->count()) return {};
    return pl->at(ni);
}

void Playlist::prefetchNextEpisode() {
    auto next = nextItem();
    if (!next || next->isList() || next->type != PlaylistItem::Online) {
        m_prefetchTimer.stop();
        return;
    }
    if (m_prefetch.valid && m_prefetch.itemLink == next->link) return;
    m_prefetchCancel.cancel();
    m_prefetch = {};
    m_prefetchTimer.start();
}

void Playlist::startNextEpisodePrefetch() {
    if (m_watcher.isRunning()) { m_prefetchTimer.start(); return; }   // busy resolving - retry later
    // One at a time: assigning over a running future drops the handle the destructor waits on,
    // and the reset() below would un-cancel the worker still holding that token.
    if (m_prefetchFuture.isRunning()) { m_prefetchTimer.start(); return; }
    auto next = nextItem();
    if (!next || next->isList() || next->type != PlaylistItem::Online) return;
    if (m_prefetch.valid && m_prefetch.itemLink == next->link) return;
    auto pl = next->parent();
    ShowProvider *provider = pl ? pl->provider() : nullptr;
    if (!provider) return;

    m_prefetchCancel.reset();
    const QString link = next->link;
    m_prefetchFuture = QtConcurrent::run([this, next, provider, link]() {
        if (m_prefetchCancel.isCancelled()) return;
        try {
            Client client(m_prefetchCancel);
            auto servers = provider->loadServers(&client, next.data());
            if (servers.isEmpty() || m_prefetchCancel.isCancelled()) return;
            std::sort(servers.begin(), servers.end(),
                      [](const VideoServer &a, const VideoServer &b) { return a.name < b.name; });
            auto result = ServerSelector::findWorkingServer(&client, provider, servers);
            if (!result.found() || m_prefetchCancel.isCancelled()) return;
            QMetaObject::invokeMethod(this, [this, link, servers, provider,
                                             idx = result.index,
                                             cache = std::move(result.cachedSources),
                                             info = result.playInfo]() mutable {
                if (m_prefetchCancel.isCancelled()) return;
                m_prefetch = Prefetch{ true, link, servers, provider, idx, std::move(cache), info };
                logOk() << "Playlist" << "Prefetched next episode source:" << link;
            }, Qt::QueuedConnection);
        } catch (AppException &e) {
            logWarn() << "Playlist" << "Next-episode prefetch failed:" << e.what();
        } catch (const std::exception &e) {
            logWarn() << "Playlist" << "Next-episode prefetch failed:" << e.what();
        }
    });
}

bool Playlist::tryUsePrefetch(const QSharedPointer<PlaylistItem> &item) {
    if (!m_prefetch.valid || m_prefetch.itemLink != item->link) return false;
    if (item->type != PlaylistItem::Online) return false;
    if (m_watcher.isRunning()) return false;   // a resolve is mid-flight -> take the normal path

    Prefetch pf = std::move(m_prefetch);
    m_prefetch = {};
    m_prefetchCancel.cancel();
    m_prefetchTimer.stop();

    auto currentItem = m_currentItem.toStrongRef();
    if (currentItem && currentItem != item) saveProgress();

    m_pendingItem.clear();
    m_pendingServerIndex = -1;
    m_bgCacheCancel.cancel();
    m_serverListModel.clear();

    applyServers(pf.servers, pf.provider, pf.chosenIndex, std::move(pf.cachedSources));
    finalizePlayback(item);

    PlayInfo playInfo = pf.playInfo;
    playInfo.progress = item->progress();
    logOk() << "Playlist" << "Using prefetched source for" << item->displayName;
    if (auto *mpv = MpvPlayer::instance()) mpv->open(playInfo);
    return true;
}

PlayInfo Playlist::loadLocalPlayInfo(const QSharedPointer<PlaylistItem> &item) {
    if (!QFile::exists(item->link)) {
        logWarn() << "Playlist" << item->link << "does not exist";
        QMetaObject::invokeMethod(this, [this, item]() {
            auto playlist = item->parent();
            if (!playlist) return;
            int itemRow = item->row();
            if (itemRow < 0) return;
            bool wasCurrent = playlist->currentIndex() == itemRow;
            beginRemoveRows(indexFor(playlist.data()), itemRow, itemRow);
            playlist->removeAt(itemRow);
            endRemoveRows();
            if (wasCurrent) playlist->setCurrentIndex(-1);
        }, Qt::QueuedConnection);
        return {};
    }
    PlayInfo playInfo;
    playInfo.videos.emplaceBack(item->link);
    return playInfo;
}

void Playlist::finalizePlayback(const QSharedPointer<PlaylistItem> &item) {
    QMetaObject::invokeMethod(this, [this, item]() {
        auto playlist = item->parent();
        if (!playlist) return;  // item may have been removed on main thread

        int itemRow = item->row();
        if (playlist->currentIndex() != itemRow)
            playlist->setCurrentIndex(itemRow);
        int row = playlist->row();
        auto parent = playlist->parent();
        while (parent) {
            parent->setCurrentIndex(row);
            row = parent->row();
            parent = parent->parent();
        }
        if (!item->preview) {
            // Duration is not known yet, so carry the stored fraction rather than resetting it to 0.
            if (playlist->isLocalDir())
                emit localProgressUpdated(item->link, playlist->link, item->progress());
            emit progressUpdated(playlist->link, itemRow, item->progress());
            emit episodeStarted(playlist->link, itemRow);
        }
        setCurrentItem(item);
        prefetchNextEpisode();
    }, Qt::QueuedConnection);
}

void Playlist::openUrl(QUrl url, bool play) {
    LocalMedia::ParsedUrl parsed = LocalMedia::parse(url);

    if (!parsed.valid) {
        logError() << "Playlist" << "Invalid url:" << parsed.raw;
        return;
    }
    static QStringList subtitleExtensions = { "srt", "sub", "ssa", "ass", "idx", "vtt" };
    if (subtitleExtensions.contains(QFileInfo(parsed.url.path()).suffix()) ||
        parsed.url.path().toLower().contains("subtitle")) {
        if (auto *mpv = MpvPlayer::instance()) mpv->addSubtitle(Track(parsed.url));
        return;
    }

    if (parsed.url.isLocalFile()) {
        openLocalPath(parsed.url, parsed.raw, play);
    } else {
        openRemoteUrl(parsed.raw, parsed.url, play);
    }
}

void Playlist::openLocalPath(const QUrl &url, const QString &urlString, bool play) {
    QFileInfo pathInfo(url.toLocalFile());
    QString dirPath = pathInfo.isDir() ? pathInfo.absoluteFilePath() : pathInfo.dir().absolutePath();
    logInfo() << "Playlist" << "Opening local file" << dirPath;

    auto playlist = m_byLink.value(dirPath, QWeakPointer<PlaylistItem>()).toStrongRef();
    if (playlist) {
        if (!pathInfo.isDir())
            playlist->setCurrentIndex(playlist->indexOf(pathInfo.absoluteFilePath()));
    } else {
        playlist = QSharedPointer<PlaylistItem>::create();
        if (LocalMedia::loadFolder(url, playlist, [this](const QString &p) { return m_byLink.contains(p); }, 0, 5)) {
        applyLocalResume(playlist);
            append(playlist);
            logInfo() << "Playlist" << "Loaded folder" << dirPath;
        } else {
            logInfo() << "Playlist" << "Failed to load folder" << dirPath;
            playlist = nullptr;
        }
    }

    if (playlist && play) {
        if (auto *mpv = MpvPlayer::instance()) mpv->showText(QString("Playing: %1").arg(urlString));
        tryPlay(playlist);
    }
}

void Playlist::openRemoteUrl(const QString &urlString, const QUrl &url, bool play) {
    logInfo() << "Playlist" << "Opening online video" << urlString;

    auto playlist = m_byLink.value("videos", QWeakPointer<PlaylistItem>()).toStrongRef();
    if (!playlist) {
        playlist = QSharedPointer<PlaylistItem>::create("Videos", nullptr, "videos");
        append(playlist);
    }

    int itemIndex = playlist->indexOf(urlString);
    if (itemIndex == -1) {
        beginInsertRows(indexFor(playlist.data()), playlist->count(), playlist->count());
        playlist->emplaceBack(0, playlist->count() + 1, urlString, url.toString(), false);
        endInsertRows();
        playlist->last()->type = PlaylistItem::Pasted;
        itemIndex = playlist->count() - 1;
    }
    playlist->setCurrentIndex(itemIndex);

    if (play) {
        if (auto *mpv = MpvPlayer::instance()) mpv->showText(QString("Playing: %1").arg(urlString));
        tryPlay(playlist);
    }
}

void Playlist::onLocalDirectoryChanged(const QString &path) {
    auto playlist = m_byLink.value(path, QWeakPointer<PlaylistItem>()).toStrongRef();
    if (!playlist) {
        logError() << "Playlist" << "Untracked path" << path;
        return;
    }
    auto currentItem = m_currentItem.toStrongRef();
    auto currentParent = currentItem ? currentItem->parent() : nullptr;
    bool isCurrentPlaylist = currentParent == playlist;
    QString prevLink = isCurrentPlaylist ? currentItem->link : "";

    // The rebuild drops every in-memory position, so persist the live one first.
    if (isCurrentPlaylist) saveProgress(true);

    logInfo() << "Playlist" << "Directory" << path << "has changed";
    deregisterPlaylist(playlist);
    // load() rebuilds the children, so it has to happen inside the reset bracket.
    beginResetModel();
    const bool loaded = LocalMedia::loadFolder(QUrl::fromLocalFile(path), playlist,
                                                [this](const QString &p) { return m_byLink.contains(p); });
    if (loaded) applyLocalResume(playlist);
    endResetModel();
    if (loaded) {
        registerPlaylist(playlist);
        if (isCurrentPlaylist) {
            // Re-pin by link: a rescan must never move the current item while its file still
            // exists, whatever the stored resume point says.
            const int idx = prevLink.isEmpty() ? -1 : playlist->indexOf(prevLink);
            if (idx >= 0) {
                playlist->setCurrentIndex(idx);
                setCurrentItem(playlist->at(idx));
            } else {
                setCurrentItem(playlist->currentItem());
                tryPlay(playlist);
            }
        }
        return;
    }

    logInfo() << "Playlist" << "Failed to reload folder" << playlist->link;
    if (auto *mpv = MpvPlayer::instance()) mpv->pause();
    auto parent = playlist->parent();
    if (!parent) return;
    int plRow = playlist->row();
    beginRemoveRows(indexFor(parent.data()), plRow, plRow);
    parent->removeOne(playlist);
    endRemoveRows();
    setCurrentItem(m_currentItem.toStrongRef());
}

void Playlist::applyLocalResume(const QSharedPointer<PlaylistItem> &playlist) {
    if (!m_localResume || !playlist) return;
    visitListNodes(playlist, [this](const QSharedPointer<PlaylistItem> &node) {
        if (!node->isLocalDir()) return;
        const auto rows = m_localResume(node->link);
        if (rows.isEmpty()) return;

        QHash<QString, double> byPath;
        byPath.reserve(rows.size());
        for (const auto &[path, progress] : rows) byPath.insert(path, progress);

        for (const auto &child : node->children()) {
            if (child->isList()) continue;
            if (const auto it = byPath.constFind(child->link); it != byPath.constEnd())
                child->setProgress(*it);
        }
        // Row 0 is the most recent play; an explicitly opened file has already pinned the index.
        if (node->currentIndex() == -1) {
            const int idx = node->indexOf(rows.first().first);
            if (idx >= 0) node->setCurrentIndex(idx);
        }
    });
}

void Playlist::visitListNodes(const QSharedPointer<PlaylistItem> &root, const PlaylistVisitor &visitor) {
    QList<QSharedPointer<PlaylistItem>> queue{root};
    for (int front = 0; front < queue.size(); ++front) {
        const auto item = queue.at(front);   // by value: the appends below can reallocate the queue
        visitor(item);
        for (const auto &child : item->children())
            if (child->isList())
                queue.append(child);
    }
}

void Playlist::registerPlaylist(const QSharedPointer<PlaylistItem> &playlist) {
    if (!playlist || !playlist->isList() || m_byLink.contains(playlist->link)) return;
    visitListNodes(playlist, [this](const QSharedPointer<PlaylistItem> &item) {
        m_byLink.insert(item->link, QWeakPointer<PlaylistItem>(item));
        if (item->isLocalDir())
            m_folderWatcher.addPath(item->link);
    });
}

void Playlist::deregisterPlaylist(const QSharedPointer<PlaylistItem> &playlist) {
    if (!playlist || !m_byLink.contains(playlist->link)) return;
    visitListNodes(playlist, [this](const QSharedPointer<PlaylistItem> &item) {
        m_byLink.remove(item->link);
        if (item->isLocalDir())
            m_folderWatcher.removePath(item->link);
    });
}

QModelIndex Playlist::currentChild(const QModelIndex &idx) const {
    auto currentPlaylist = static_cast<PlaylistItem*>(idx.internalPointer());
    if (!currentPlaylist) return QModelIndex();
    int index = currentPlaylist->currentIndex();
    if (!currentPlaylist->isValidIndex(index)) index = 0;
    if (!currentPlaylist->isValidIndex(index)) return QModelIndex();
    return createIndex(index, 0, currentPlaylist->at(index).data());
}

QString Playlist::currentShowName() const {
    auto cur = m_currentItem.toStrongRef();
    auto parent = cur ? cur->parent() : nullptr;
    return parent ? parent->name : QString();
}

QString Playlist::currentItemName() const {
    auto cur = m_currentItem.toStrongRef();
    return cur ? cur->displayName : QString();
}

int Playlist::currentShowEpisodeCount() const {
    auto cur = m_currentItem.toStrongRef();
    auto parent = cur ? cur->parent() : nullptr;
    return parent ? parent->count() : 0;
}

int Playlist::rowCount(const QModelIndex &parent) const {
    if (parent.column() > 0) return 0;
    PlaylistItem *parentItem = parent.isValid()
                                   ? static_cast<PlaylistItem*>(parent.internalPointer())
                                   : m_root.data();
    return parentItem->count();
}

QModelIndex Playlist::index(int row, int column, const QModelIndex &parent) const {
    if (!hasIndex(row, column, parent)) return QModelIndex();
    PlaylistItem *parentItem = parent.isValid()
                                   ? static_cast<PlaylistItem*>(parent.internalPointer())
                                   : m_root.data();
    auto childItem = parentItem->at(row).data();
    return childItem ? createIndex(row, column, childItem) : QModelIndex();
}

QModelIndex Playlist::parent(const QModelIndex &childIndex) const {
    if (!childIndex.isValid()) return QModelIndex();
    auto *childItem = static_cast<PlaylistItem*>(childIndex.internalPointer());
    auto parentItem = childItem ? childItem->parent() : nullptr;
    if (!parentItem || parentItem.get() == m_root.data())
        return QModelIndex();
    return createIndex(parentItem->row(), 0, parentItem.data());
}

QVariant Playlist::data(const QModelIndex &index, int role) const {
    if (!index.isValid()) return QVariant();
    auto *item = static_cast<PlaylistItem*>(index.internalPointer());
    if (!item) return QVariant();

    switch (role) {
    case Qt::DisplayRole:
        return item->isList() ? item->name : item->displayName;
    case TitleRole:
        return item->name;
    case IndexRole:
        return index;
    case NumberRole:
        return item->number;
    case IsCurrentIndexRole: {
        auto parent = item->parent();
        if (!parent || parent->currentIndex() == -1) return false;
        return parent->currentIndex() == item->row();
    }
    case IsDeletableRole:
        return (item->count() > 0) || ((item->type & PlaylistItem::Pasted) != 0);
    case LinkRole:
        return item->link;
    case IsWatchedRole: {
        if (item->isList()) return false;
        auto parent = item->parent();
        return parent && parent->currentIndex() > item->row();
    }
    default:
        return QVariant();
    }
}

bool Playlist::isFilteredOut(const QModelIndex &index, const QString &filter) const {
    if (filter.isEmpty() || !index.isValid()) return false;
    auto *item = static_cast<PlaylistItem*>(index.internalPointer());
    if (!item || item->isList()) return false;
    const QString f = filter.toLower();
    if (item->displayName.toLower().contains(f)) return false;
    if (QString::number(item->number).contains(filter)) return false;
    return true;
}

QHash<int, QByteArray> Playlist::roleNames() const {
    return {
        {TitleRole, "title"},
        {NumberRole, "number"},
        {IndexRole, "index"},
        {Qt::DisplayRole, "display"},
        {IsCurrentIndexRole, "isCurrentIndex"},
        {IsDeletableRole, "isDeletable"},
        {LinkRole, "link"},
        {IsWatchedRole, "isWatched"},
    };
}