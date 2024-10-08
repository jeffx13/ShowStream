#include "iyf.h"

QList<ShowData> IyfProvider::search(Client *client, const QString &query, int page, int type) {
    QList<ShowData> shows;
    QString tag = QUrl::toPercentEncoding (query.toLower());
    QString url = QString("https://rankv21.iyf.tv/v3/list/briefsearch?tags=%1&orderby=4&page=%2&size=36&desc=1&isserial=-1&uid=%3&expire=%4&gid=0&sign=%5&token=%6")
                      .arg(tag, QString::number (page), uid, expire, sign, token);
    auto &keys = getKeys(client);
    auto resultsJson = client->post(url, { {"tag", tag}, {"vv", hash("tags=" + tag, keys)}, {"pub", keys.first} }, headers)
                            .toJsonObject()["data"].toObject()["info"].toArray().at (0).toObject()["result"].toArray();

    for (const QJsonValue &value : resultsJson) {
        QJsonObject showJson = value.toObject();
        QString title = showJson["title"].toString();
        QString link = showJson["contxt"].toString();
        QString coverUrl = showJson["imgPath"].toString();
        shows.emplaceBack(title, link, coverUrl, this);
    }
    return shows;
}

QList<ShowData> IyfProvider::filterSearch(Client *client, int page, bool latest, int type) {
    QList<ShowData> shows;
    QString orderBy = latest ? "1" : "2";
    QString params = QString("cinema=1&page=%1&size=36&orderby=%2&desc=1&cid=%3%4")
                         .arg(QString::number (page), orderBy, cid[type], latest ? "" : "&year=今年");
    auto resultsJson = invokeAPI(client, "https://m10.iyf.tv/api/list/Search?", params + "&isserial=-1&isIndex=-1&isfree=-1")["result"].toArray();

    for (const QJsonValue &value : resultsJson) {
        QJsonObject showJson = value.toObject();
        QString coverUrl = showJson["image"].toString();

        QString title = showJson["title"].toString();
        QString link = showJson["key"].toString();
        shows.emplaceBack(title, link, coverUrl, this);
    }
    return shows;
}

int IyfProvider::loadDetails(Client *client, ShowData &show, bool loadInfo, bool getPlaylist, bool getEpisodeCount) const {
    QString params = QString("cinema=1&device=1&player=CkPlayer&tech=HLS&country=HU&lang=cns&v=1&id=%1&region=UK").arg (show.link);
    auto infoJson = invokeAPI(client, "https://m10.iyf.tv/v3/video/detail?", params);
    if (infoJson.isEmpty()) return false;

    if (loadInfo) {
        show.description =  infoJson["contxt"].toString();
        show.status = infoJson["lastName"].toString();
        show.views =  QString::number(infoJson["view"].toInt (-1));
        show.updateTime = infoJson["updateweekly"].toString();
        show.score = infoJson["score"].toString();
        show.releaseDate = infoJson["add_date"].toString();
        show.genres.push_back (infoJson["videoType"].toString());
    }

    if (!getPlaylist && !getEpisodeCount) return true;

    QString cid = infoJson["cid"].toString();
    params = QString("cinema=1&vid=%1&lsk=1&taxis=0&cid=%2&uid=%3&expire=%4&gid=4&sign=%5&token=%6")
                 .arg(show.link, cid, uid, expire, sign, token);
    auto keys = getKeys(client);
    auto vv = hash(params, keys);
    params.replace (",", "%2C");

    QString url = "https://m10.iyf.tv/v3/video/languagesplaylist?" + params + "&vv=" + vv + "&pub=" + keys.first;
    auto playlistJson = client->get (url).toJsonObject()["data"].toObject()["info"].toArray().at (0).toObject()["playList"].toArray();
    if (playlistJson.isEmpty ()) return false;
    int episodeCount = playlistJson.size();

    if (getPlaylist) {
        for (const QJsonValue &value : playlistJson) {
            QJsonObject episodeJson = value.toObject();
            QString title = episodeJson["name"].toString();
            float number = resolveTitleNumber(title);
            QString link = episodeJson["key"].toString();
            show.addEpisode(0, number, link, title);
        }
    }

    return episodeCount;
}

PlayInfo IyfProvider::extractSource(Client *client, const VideoServer &server) const {
    PlayInfo playInfo;

    QString params = QString("cinema=1&id=%1&a=0&lang=none&usersign=1&region=UK&device=1&isMasterSupport=1&uid=%2&expire=%3&gid=0&sign=%4&token=%5")
                         .arg (server.link,uid, expire, sign, token);

    auto clarities = invokeAPI(client, "https://m10.iyf.tv/v3/video/play?", params)["clarity"].toArray();
    for (const QJsonValue &value : clarities) {
        auto clarity = value.toObject();
        auto path = clarity["path"];
        if (!path.isNull()) {
            QString source = path.toObject()["result"].toString();
            params = QString("uid=%1&expire=%2&gid=0&sign=%3&token=%4")
                         .arg (uid, expire, sign, token);
            auto &keys = getKeys(client);
            source += "?" + params + "&vv=" + hash(params, keys) + "&pub=" + keys.first;
            playInfo.sources.emplaceBack (source);
            // qDebug() << source;
        }
    }
    return playInfo;
}

QJsonObject IyfProvider::invokeAPI(Client *client, const QString &prefixUrl, const QString &params) const {
    auto &keys = getKeys(client);
    auto url = prefixUrl + params + "&vv=" + hash(params, keys) + "&pub=" + keys.first;
    return client->get(url).toJsonObject()["data"].toObject()["info"].toArray().at (0).toObject();
}

QPair<QString, QString> &IyfProvider::getKeys(Client *client, bool update) const {
    static QPair<QString, QString> keys;
    if (keys.first.isEmpty() || update) {
        QString url("https://www.iyf.tv/list/anime?orderBy=0&desc=true");
        static QRegularExpression pattern(R"("publicKey":"([^"]+)\","privateKey\":\[\"([^"]+)\")");
        QRegularExpressionMatch match = pattern.match(client->get (url).body);
        // Perform the search
        if (!match.hasMatch() || match.lastCapturedIndex() != 2)
            throw MyException("Failed to update keys");
        keys = {match.captured(1), match.captured(2)};
    }
    return keys;
}

QString IyfProvider::hash(const QString &input, const QPair<QString, QString> &keys) const {
    auto &[publicKey, privateKey] = keys;
    auto toHash = publicKey + "&"  + input.toLower()+ "&"  + privateKey;
    QByteArray hash = QCryptographicHash::hash(toHash.toUtf8(), QCryptographicHash::Md5);
    return hash.toHex();
}


