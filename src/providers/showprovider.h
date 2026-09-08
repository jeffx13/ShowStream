#pragma once
#include "net/client.h"
#include "providers/showdata.h"
#include "media/playlistitem.h"
#include "media/playinfo.h"
#include "media/danmaku.h"
#include <QMutex>

// Every method runs on a worker thread, with the caller's Client.
class ShowProvider : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QString hostUrl READ hostUrl CONSTANT)
public:
    explicit ShowProvider(QObject *parent = nullptr) : QObject(parent) {}

    virtual QString name() const = 0;
    virtual QString hostUrl() const = 0;
    virtual QStringList availableTypes() const = 0;

    [[nodiscard]] virtual QList<ShowData>    search       (Client *client, const QString &query, int page, int typeIndex) = 0;
    [[nodiscard]] virtual QList<ShowData>    popular      (Client *client, int page, int typeIndex) = 0;
    [[nodiscard]] virtual QList<ShowData>    latest       (Client *client, int page, int typeIndex) = 0;
    [[nodiscard]] virtual QList<VideoServer> loadServers  (Client *client, const PlaylistItem *episode) const = 0;
    // VideoServer by value: each concurrent caller gets its own copy to mutate freely.
    [[nodiscard]] virtual PlayInfo           extractSource(Client *client, VideoServer server) = 0;

    // CountOnly is never combined: implementations return from it as soon as the count is known.
    enum LoadPart {
        CountOnly = 0x1,
        Episodes  = 0x2,   // fill the show's playlist
        Details   = 0x4,   // description, genres, status, cover, ...
    };
    Q_DECLARE_FLAGS(LoadParts, LoadPart)

    int loadShow(Client *client, ShowData &show) const {
        LoadParts parts = Details;
        if (!show.playlist()) parts |= Episodes;
        return loadShow(client, show, parts);
    }
    int fetchEpisodeCount(Client *client, ShowData &show) const { return loadShow(client, show, CountOnly); }
    void loadPlaylist(Client *client, ShowData &show) const { loadShow(client, show, Episodes); }

    void setPreferredServer(const QString &serverName) {
        QMutexLocker lock(&m_preferredServerMutex);
        m_preferredServer = serverName;
    }
    QString preferredServer() const {
        QMutexLocker lock(&m_preferredServerMutex);
        return m_preferredServer;
    }

protected:
    virtual int loadShow(Client *client, ShowData &show, LoadParts parts) const = 0;

    bool attachDanmaku(PlayInfo &info, QList<DanmakuComment> comments, const QString &cacheKey) const;

private:
    QString m_preferredServer;
    mutable QMutex m_preferredServerMutex;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(ShowProvider::LoadParts)
