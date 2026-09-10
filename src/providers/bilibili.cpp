#include "providers/bilibili.h"
#include "providers/bilibilidanmaku.h"
#include "app/logger.h"
#include "app/settings.h"
#include "media/playlistitem.h"
#include <QLocale>



Bilibili::Bilibili(QObject *parent) : ShowProvider(parent) {
    m_proxyApi = Settings::instance().value("bilibili/proxy").toString();

    m_headers = {
                 {"Referer",    "https://www.bilibili.com/"},
                 {"Origin",     "https://www.bilibili.com"},
                 {"User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:148.0) "
                                "Gecko/20100101 Firefox/148.0"},
                 {"Accept",     "application/json, text/plain, */*"},
                 };

    auto cookieMap = Settings::instance().groupValues("bilibili/cookies");
    if (!cookieMap.isEmpty()) {
        QStringList parts;
        parts.reserve(cookieMap.size());
        for (auto it = cookieMap.constBegin(); it != cookieMap.constEnd(); ++it) {
            QString value = it.value();
            value.replace(QLatin1Char(','), QLatin1String("%2C"));
            value.replace(QLatin1Char('*'), QLatin1String("%2A"));
            parts << (it.key() + QLatin1Char('=') + value);
        }
        m_headers["Cookie"] = parts.join("; ");
        logInfo() << "Bilibili" << "cookies loaded:" << cookieMap.size() << "entries";
    } else {
        logWarn() << "Bilibili: no cookies configured - member content will be unavailable";
    }
}

// Route a Bilibili GET through the in-China relay (target URL in X-Proxy-Url) when set.
Client::Response Bilibili::apiGet(Client *client, const QString &url,
                                  const QMap<QString, QString> &params) const {
    if (m_proxyApi.isEmpty())
        return client->get(url, m_headers, params);

    auto headers = m_headers;
    headers["X-Proxy-Url"] = Client::urlWithParams(url, params);
    return client->get(m_proxyApi, headers);
}


QList<ShowData> Bilibili::search(Client *client, const QString &query, int page, int typeIndex) {
    QString searchType = (typeIndex <= 1) ? "media_bangumi" : "media_ft";
    QMap<QString, QString> params = {
                                     {"search_type", searchType},
                                     {"keyword",     QUrl::toPercentEncoding(query)},
                                     {"page",        QString::number(page)},
                                     {"page_size",   "20"},
                                     {"platform",    "pc"},
                                     };

    auto results = apiGet(client, "https://api.bilibili.com/x/web-interface/wbi/search/type", params)
                       .toJsonObject()["data"].toObject()["result"].toArray();

    QList<ShowData> shows;
    for (const auto &v : std::as_const(results)) {
        auto r = v.toObject();
        QString title = r["title"].toString();
        title.remove("<em class=\"keyword\">").remove("</em>");
        QString link = QStringLiteral("%1 %2")
                           .arg(QString::number(r["media_id"].toInt()),
                                QString::number(r["season_id"].toInt()));
        shows.emplaceBack(title, link, r["cover"].toString(), this,
                          r["index_show"].toString(), kShowTypes[typeIndex]);
    }
    return shows;
}

QList<ShowData> Bilibili::popular(Client *client, int page, int typeIndex) {
    return filterSearch(client, (typeIndex <= 1) ? 3 : 2, page, typeIndex);
}

QList<ShowData> Bilibili::latest(Client *client, int page, int typeIndex) {
    return filterSearch(client, 0, page, typeIndex);
}

QList<ShowData> Bilibili::filterSearch(Client *client, int sortBy, int page, int typeIndex) {
    int st = kSeasonTypes[typeIndex];
    QMap<QString, QString> params = {
                                     {"st",             QString::number(st)},
                                     {"season_type",    QString::number(st)},
                                     {"order",          QString::number(sortBy)},
                                     {"sort",           "0"},
                                     {"page",           QString::number(page)},
                                     {"pagesize",       "20"},
                                     {"type",           "1"},
                                     {"style_id",       "-1"},
                                     {"season_version", "-1"},
                                     {"is_finish",      "-1"},
                                     {"copyright",      "-1"},
                                     {"season_status",  "-1"},
                                     {"year",           "-1"},
                                     };

    auto list = apiGet(client, "https://api.bilibili.com/pgc/season/index/result", params)
                    .toJsonObject()["data"].toObject()["list"].toArray();

    QList<ShowData> shows;
    for (const auto &v : std::as_const(list)) {
        auto s = v.toObject();
        QString link = QStringLiteral("%1 %2")
                           .arg(QString::number(s["media_id"].toInt()),
                                QString::number(s["season_id"].toInt()));
        shows.emplaceBack(s["title"].toString(), link, s["cover"].toString(),
                          this, s["index_show"].toString(), kShowTypes[typeIndex]);
    }
    return shows;
}


int Bilibili::loadShow(Client *client, ShowData &show, LoadParts parts) const {
    auto ids = show.link.split(' ');
    if (ids.size() < 2) return 0;
    const QString &seasonId = ids[1];

    auto json = apiGet(client, "https://api.bilibili.com/pgc/view/web/season",
                       {{"season_id", seasonId}}).toJsonObject();

    auto result = json["result"].toObject();
    if (result.isEmpty()) {
        logWarn() << name() << "Failed to load season" << seasonId;
        return 0;
    }

    auto episodeList = result["episodes"].toArray();

    int episodeCount = 0;
    for (const auto &v : std::as_const(episodeList)) {
        if (v.toObject()["badge"].toString() != QStringLiteral("预告"))
            episodeCount++;
    }

    if (parts.testFlag(CountOnly)) return episodeCount;

    if (parts.testFlag(Episodes)) {
        for (int i = 0; i < episodeList.size(); ++i) {
            auto ep = episodeList[i].toObject();
            bool isPreview = (ep["badge"].toString() == QStringLiteral("预告"));
            if (isPreview && i != episodeList.size() - 1) continue;

            QString epId = QString::number(ep["ep_id"].toInteger());
            QString link = seasonId + '&' + epId;
            // Carry the cid: playurl is geo-blocked, but the danmaku endpoint is open and this listing has it.
            if (const qint64 epCid = ep["cid"].toInteger(); epCid > 0)
                link += '&' + QString::number(epCid);
            QString title = ep["title"].toString();
            QString longTitle = ep["long_title"].toString();
            if (isPreview) longTitle = QStringLiteral("(预告) ") + longTitle;

            bool ok;
            float number = title.toFloat(&ok);
            show.addEpisode(0, ok ? number : -1, link, ok ? longTitle : title, isPreview);
        }
    }

    if (!parts.testFlag(Details)) return episodeCount;

    show.coverUrl    = result["cover"].toString();
    show.description = result["evaluate"].toString();
    show.releaseDate = result["publish"].toObject()["pub_time_show"].toString();
    show.updateTime  = result["new_ep"].toObject()["desc"].toString();

    auto stat = result["stat"].toObject();
    show.views  = QLocale::system().toString(stat["views"].toInteger());
    show.status = stat["follow_text"].toString();

    auto rating = result["rating"].toObject();
    if (!rating.isEmpty()) {
        show.score = QStringLiteral("%1 (%2)")
        .arg(QString::number(rating["score"].toDouble()),
             QLocale::system().toString(rating["count"].toInt()));
    }

    for (const auto &s : result["styles"].toArray())
        show.genres.push_back(s.toObject()["name"].toString());

    return episodeCount;
}


QList<VideoServer> Bilibili::loadServers(Client * /*client*/, const PlaylistItem *episode) const {
    return {{"Default", episode->link}};
}

static QString dashUrl(const QJsonObject &o) {
    for (const auto &key : {"baseUrl", "base_url", "url"}) {
        QString u = o[key].toString();
        if (!u.isEmpty()) return u;
    }
    return {};
}

// The nesting varies with the relay, so search for the two integers rather than bet on a path.
static qint64 findInt(const QJsonValue &value, QLatin1String key, int depth = 0) {
    if (depth > 6) return 0;
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        const auto hit = object.constFind(key);
        if (hit != object.constEnd() && hit->toInteger() > 0)
            return hit->toInteger();
        for (const QJsonValue &child : object)
            if (const qint64 found = findInt(child, key, depth + 1)) return found;
    } else if (value.isArray()) {
        for (const QJsonValue &child : value.toArray())
            if (const qint64 found = findInt(child, key, depth + 1)) return found;
    }
    return 0;
}

static QJsonObject unwrapPlayInfo(const QJsonObject &json) {
    QJsonObject root = json;
    if (root.contains("raw"))  root = root["raw"].toObject();
    if (root.contains("data")) root = root["data"].toObject();
    if (root.contains("result")) {
        auto inner = root["result"].toObject();
        if (inner.contains("video_info")) return inner;
    }
    if (root.contains("video_info")) return root;
    return {};
}

PlayInfo Bilibili::extractSource(Client *client, VideoServer server) {
    PlayInfo playInfo;

    auto parts = server.link.split('&');
    if (parts.size() < 2) return playInfo;
    const qint64 listedCid = parts.size() > 2 ? parts[2].toLongLong() : 0;   // older links lack it

    QMap<QString, QString> params = {
                                     {"ep_id", parts[1]},
                                     {"fnval", "4048"},
                                     {"fnver", "0"},
                                     {"fourk", "1"},
                                     };

    auto json = apiGet(client, "https://api.bilibili.com/pgc/player/web/v2/playurl", params)
                    .toJsonObject();

    auto container = unwrapPlayInfo(json);
    auto videoInfo = container["video_info"].toObject();

    int code = json["code"].toInt(-1);
    bool isPreviewing = container["is_preview"].toInt(0) == 1;
    int quality = videoInfo["quality"].toInt();
    logInfo() << name() << "playurl: code=" << code
           << "preview=" << isPreviewing
           << "quality=" << quality
           << "dash=" << videoInfo.contains("dash")
           << "videos=" << videoInfo["dash"].toObject()["video"].toArray().size();
    if (isPreviewing) {
        logWarn() << name() << "WARNING: preview/trial content - "
                            "check SESSDATA cookie is valid and percent-encoded.";
    }

    if (videoInfo.contains("dash")) {
        auto dash = videoInfo["dash"].toObject();

        for (const auto &v : dash["dolby"].toObject()["audio"].toArray()) {
            auto a = v.toObject();
            int bw = a["bandwidth"].toInt();
            QString url = dashUrl(a);
            if (!url.isEmpty())
                playInfo.audios.emplaceBack(url, "Dolby " + Track::formatBitrate(bw), "", bw);
        }

        for (const auto &v : dash["audio"].toArray()) {
            auto a = v.toObject();
            int bw = a["bandwidth"].toInt();
            QString url = dashUrl(a);
            if (!url.isEmpty())
                playInfo.audios.emplaceBack(url, Track::formatBitrate(bw), "", bw);
        }

        // Bilibili sorts these best-first.
        for (const auto &v : dash["video"].toArray()) {
            auto vid = v.toObject();
            int h = vid["height"].toInt();
            int bw = vid["bandwidth"].toInt();
            QString label = QString("%1p %2").arg(h).arg(Track::formatBitrate(bw));
            QString url = dashUrl(vid);
            if (!url.isEmpty())
                playInfo.videos.emplaceBack(url, label, h, bw);
        }
    }
    else if (videoInfo.contains("durl")) {
        for (const auto &v : videoInfo["durl"].toArray()) {
            auto d = v.toObject();
            playInfo.videos.emplaceBack(
                d["url"].toString(),
                QStringLiteral("Q%1 (%2 bytes)").arg(videoInfo["quality"].toInt()).arg(d["size"].toInt()));
        }
    } else if (videoInfo.contains("durls")) {
        for (const auto &v : videoInfo["durls"].toArray()) {
            auto item = v.toObject();
            // at(), not [0]: the non-const operator[] asserts, so a missing durl aborts a Debug build.
            auto d = item["durl"].toArray().at(0).toObject();
            if (d.isEmpty()) continue;
            playInfo.videos.emplaceBack(
                d["url"].toString(),
                QStringLiteral("Q%1 (%2 bytes)").arg(item["quality"].toInt()).arg(d["size"].toInt()));
        }
    } else {
        logWarn() << name() << "No video streams found in response";
        return playInfo;
    }

    logInfo() << name() << "Extracted:" << playInfo.videos.size() << "video,"
           << playInfo.audios.size() << "audio streams";

    playInfo.addHeader("Referer", "https://www.bilibili.com/");
    playInfo.addHeader("User-Agent", m_headers["User-Agent"]);

    // cid doubles as the danmaku oid, and it is already in this response.
    if (DanmakuOptions::current().enabled) {
        const qint64 cid = listedCid > 0 ? listedCid : findInt(json, QLatin1String("cid"));
        if (cid > 0) {
            const int durationMs = int(findInt(json, QLatin1String("timelength")));
            attachDanmaku(playInfo,
                          BilibiliDanmaku::fetchAll(client, cid, durationMs, m_headers, m_proxyApi),
                          QStringLiteral("bili-%1").arg(cid));
        }
    }
    return playInfo;
}
