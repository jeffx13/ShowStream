#pragma once
#include "providers/showprovider.h"

class Anikoto : public ShowProvider {
public:
    explicit Anikoto(QObject *parent = nullptr) : ShowProvider(parent) {}
    QString name() const override { return "Anikoto"; }
    QString hostUrl() const override { return "https://anikototv.to/"; }

    QStringList availableTypes() const override { return {"Anime"}; }
    QList<ShowData>    search       (Client *client, const QString &query, int page, int typeIndex) override;
    QList<ShowData>    popular      (Client *client, int page, int typeIndex) override;
    QList<ShowData>    latest       (Client *client, int page, int typeIndex) override;
    QList<VideoServer> loadServers  (Client *client, const PlaylistItem *episode) const override;
    PlayInfo           extractSource(Client *client, VideoServer server) override;

private:
    int loadShow(Client *client, ShowData &show, LoadParts parts) const override;

    QList<ShowData> parseShowList(const QString &html);
    void            loadDetails(Client *client, ShowData &show, const QString &watchUrl) const;
    PlayInfo        extractEmbed(Client *client, const QString &embedUrl, const VideoServer &server) const;

    QMap<QString, QString> m_headers = {
        {"User-Agent",       kFirefoxUserAgent},
        {"X-Requested-With", "XMLHttpRequest"},
        {"Referer",          "https://anikototv.to/"},
    };
};
