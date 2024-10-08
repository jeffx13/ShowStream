#pragma once
#include <QDebug>
#include "showprovider.h"
#include "network/csoup.h"


class Wolong : public ShowProvider
{
public:
    explicit Wolong(QObject *parent = nullptr) : ShowProvider{parent} {};
    QString baseUrl = "https://collect.wolongzy.cc/api.php/provide/vod/?";
    QString            name() const override { return "卧龙"; }
    QList<int>         getAvailableTypes() const override { return {ShowData::ANIME, ShowData::TVSERIES, ShowData::MOVIE}; }
    QList<ShowData>    search       (Client *client, const QString &query, int page, int type) override;
    QList<ShowData>    popular      (Client *client, int page, int type) override;
    QList<ShowData>    latest       (Client *client, int page, int type) override;
    int                loadDetails  (Client *client, ShowData &show, bool loadInfo, bool getPlaylist, bool getEpisodeCount) const override;
    QList<VideoServer> loadServers  (Client *client, const PlaylistItem* episode) const override;
    PlayInfo           extractSource(Client *client, const VideoServer& server) const override;
private:
    QMap<int, int> typeMap {
                           {ShowData::ANIME, 25},
                           {ShowData::TVSERIES, 2},
                           {ShowData::MOVIE, 5},
                           };
    // https://collect.wolongzy.cc/api.php/provide/vod/?ac=list
};



