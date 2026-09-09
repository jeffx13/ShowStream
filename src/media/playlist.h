#pragma once
#include <QAbstractItemModel>
#include <QFileSystemWatcher>
#include <QFuture>
#include <QHash>
#include <QSet>
#include <QSharedPointer>
#include <QTimer>
#include <QWeakPointer>
#include "providers/showdata.h"
#include "ui/serverlistmodel.h"
#include "media/playlistitem.h"
#include "net/canceltoken.h"
#include <qqmlintegration.h>

class ShowProvider;

class Playlist : public QAbstractItemModel {
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(ServerListModel *serverList READ serverList CONSTANT)
    Q_PROPERTY(bool isLoading READ isLoading NOTIFY isLoadingChanged)
public:
    explicit Playlist(QObject *parent = nullptr);
    ~Playlist();

    QSharedPointer<PlaylistItem> root() const { return m_root; }
    QSharedPointer<PlaylistItem> find(const QString &link);
    int count() const { return m_root->count(); }

    bool isPlaying(const QString &link) const;
    void rekey(const QString &oldLink, const QSharedPointer<PlaylistItem> &newPlaylist);

    int append(const QSharedPointer<PlaylistItem> &playlist, const QSharedPointer<PlaylistItem> &parent = nullptr);
    int insert(int index, const QSharedPointer<PlaylistItem> &playlist, const QSharedPointer<PlaylistItem> &parent = nullptr);
    int replace(int index, const QSharedPointer<PlaylistItem> &playlist, const QSharedPointer<PlaylistItem> &parent = nullptr);
    Q_INVOKABLE void remove(const QModelIndex &index);
    Q_INVOKABLE void clear();

    Q_INVOKABLE bool playAt(int index);
    Q_INVOKABLE void stepItem(int offset = 1);
    Q_INVOKABLE void stepPlaylist(int offset = 1);
    Q_INVOKABLE void loadIndex(const QModelIndex &index);
    Q_INVOKABLE void reload();
    Q_INVOKABLE void loadServer(int index);
    Q_INVOKABLE void tryNextServer();
    Q_INVOKABLE void showCurrentItemName() const;
    // quiet: the periodic save, which must not put a line in the log every tick.
    Q_INVOKABLE void saveProgress(bool quiet = false) const;
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void openUrl(QUrl url, bool play);

    void appendShow(const QString &title, const QString &link, ShowProvider *provider,
                    QSharedPointer<PlaylistItem> cached, const ShowData::WatchState &info, bool play);

    enum { TitleRole = Qt::UserRole, IndexRole, NumberRole, IsCurrentIndexRole, IsDeletableRole, LinkRole, IsWatchedRole };
    Q_INVOKABLE QModelIndex currentChild(const QModelIndex &idx) const;
    Q_INVOKABLE QString currentShowName() const;
    Q_INVOKABLE QString currentItemName() const;
    Q_INVOKABLE int     currentShowEpisodeCount() const;
    Q_INVOKABLE bool isFilteredOut(const QModelIndex &index, const QString &filter) const;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    QModelIndex index(int row, int column, const QModelIndex &parent) const override;
    QModelIndex parent(const QModelIndex &childIndex) const override;
    int columnCount(const QModelIndex &parent) const override { return 1; }

    // True while a play resolves (incl. the pending-restart window) - avoids spinner blink.
    bool isLoading() const { return m_watcher.isRunning() || m_pendingItem || m_pendingServerIndex >= 0; }

    // folder -> (file path, progress), most recent first. Installed by Application so the
    // playlist needs no Library pointer.
    using LocalResumeLookup = std::function<QList<QPair<QString, double>>(const QString &)>;
    void setLocalResumeLookup(LocalResumeLookup lookup) { m_localResume = std::move(lookup); }

    Q_SIGNAL void currentItemChanged(const QModelIndex &index);
    Q_SIGNAL void isLoadingChanged();
    Q_SIGNAL void progressUpdated(QString link, int progressIndex, double progress) const;
    Q_SIGNAL void episodeStarted(QString link, int index) const;   // -> history
    Q_SIGNAL void localProgressUpdated(QString path, QString folder, double progress) const;

private:
    QSharedPointer<PlaylistItem> m_root = QSharedPointer<PlaylistItem>::create("root", nullptr, "/");
    QWeakPointer<PlaylistItem> m_currentItem;
    QHash<QString, QWeakPointer<PlaylistItem>> m_byLink;
    ServerListModel m_serverListModel;
    QFileSystemWatcher m_folderWatcher;
    CancelToken m_cancel;

    QFutureWatcher<PlayInfo> m_watcher;
    QSharedPointer<PlaylistItem> m_pendingItem;
    int m_pendingServerIndex = -1;
    LocalResumeLookup m_localResume;
    qint64 m_lastProgressSaveMs = 0;
    static constexpr qint64 kProgressSaveIntervalMs = 15'000;
    QSet<QString> m_autoTriedServers;   // servers auto-fallback already tried this episode

    CancelToken       m_appendCancel;
    QFuture<void>     m_appendFuture;

    CancelToken       m_bgCacheCancel;
    QFuture<void>     m_bgCacheFuture;
    void cacheRemainingServers();

    bool m_currentCompleted     = false;
    bool m_mpvProgressConnected = false;
    void ensureMpvProgressConnection();
    void onPlaybackProgress();

    struct Prefetch {
        bool valid = false;
        QString itemLink;
        QList<VideoServer> servers;
        ShowProvider *provider = nullptr;
        int chosenIndex = -1;
        QHash<QString, PlayInfo> cachedSources;
        PlayInfo playInfo;
    };
    Prefetch          m_prefetch;
    CancelToken       m_prefetchCancel;
    QFuture<void>     m_prefetchFuture;
    QTimer            m_prefetchTimer;
    void prefetchNextEpisode();
    void startNextEpisodePrefetch();
    QSharedPointer<PlaylistItem> nextItem() const;
    bool tryUsePrefetch(const QSharedPointer<PlaylistItem> &item);
    void applyServers(const QList<VideoServer> &servers, ShowProvider *provider,
                           int chosenIndex, QHash<QString, PlayInfo> cache);

    void onPlayFinished();
    bool tryPlay(const QSharedPointer<PlaylistItem> &item);
    PlayInfo resolvePlayback(const QSharedPointer<PlaylistItem> &item);
    QSharedPointer<PlaylistItem> resolveToPlayableItem(QSharedPointer<PlaylistItem> item);
    PlayInfo loadPlayInfo(const QSharedPointer<PlaylistItem> &item);
    PlayInfo loadPastedPlayInfo(const QSharedPointer<PlaylistItem> &item);
    PlayInfo loadOnlinePlayInfo(const QSharedPointer<PlaylistItem> &item);
    PlayInfo loadLocalPlayInfo(const QSharedPointer<PlaylistItem> &item);
    void finalizePlayback(const QSharedPointer<PlaylistItem> &item);
    void setCurrentItem(const QSharedPointer<PlaylistItem> &currentItem);

    QModelIndex indexFor(PlaylistItem *item) const {
        return (!item || item == m_root.data()) ? QModelIndex() : createIndex(item->row(), 0, item);
    }

    void openLocalPath(const QUrl &url, const QString &urlString, bool play);
    void openRemoteUrl(const QString &urlString, const QUrl &url, bool play);

    Q_SLOT void onLocalDirectoryChanged(const QString &path);

    void registerPlaylist(const QSharedPointer<PlaylistItem> &playlist);
    void deregisterPlaylist(const QSharedPointer<PlaylistItem> &playlist);
    // Resume points for a freshly built local tree, applied after loadFolder has sorted it.
    void applyLocalResume(const QSharedPointer<PlaylistItem> &playlist);

    using PlaylistVisitor = std::function<void(const QSharedPointer<PlaylistItem> &)>;
    void visitListNodes(const QSharedPointer<PlaylistItem> &root, const PlaylistVisitor &visitor);
    ServerListModel *serverList() { return &m_serverListModel; }
};