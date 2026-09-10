#include "providers/anikoto.h"
#include <QUrl>
#include <QDateTime>
#include <QRegularExpression>
#include <QJsonArray>
#include "net/hlsproxy.h"
#include "net/html.h"


// Aniwave-style, no client-side crypto: list -> server/list -> server?get -> getSources -> m3u8.

QList<ShowData> Anikoto::parseShowList(const QString &html) {
    QList<ShowData> shows;
    auto doc = Html::parse(html);
    if (!doc) return shows;
    auto posters = doc.select("//div[contains(@class,'poster') and @data-tip]");
    for (const auto &p : std::as_const(posters)) {
        QString id = p.attr("data-tip");
        if (id.isEmpty()) continue;
        auto img = p.selectFirst(".//img");
        QString cover = img ? img.attr("src") : QString();
        QString title = img ? img.attr("alt") : QString();
        auto name = p.selectFirst("..//a[contains(@class,'d-title')]");
        if (name) {
            QString t = name.text().simplified();
            if (!t.isEmpty()) title = t;
        }
        if (title.isEmpty()) continue;
        shows.emplaceBack(title, id, cover, this, "", ShowData::Anime);
    }
    return shows;
}

QList<ShowData> Anikoto::search(Client *client, const QString &query, int page, int /*typeIndex*/) {
    if (query.trimmed().isEmpty()) return {};
    QString url = hostUrl() + "filter?keyword=" + QUrl::toPercentEncoding(query)
                + "&page=" + QString::number(page);
    return parseShowList(client->get(url, m_headers).body);
}

QList<ShowData> Anikoto::popular(Client *client, int page, int /*typeIndex*/) {
    QString url = hostUrl() + "ajax/home/widget/trending?page=" + QString::number(page);
    return parseShowList(client->get(url, m_headers).toJsonObject().value("result").toString());
}

QList<ShowData> Anikoto::latest(Client *client, int page, int /*typeIndex*/) {
    QString url = hostUrl() + "ajax/home/widget/updated-sub?page=" + QString::number(page);
    return parseShowList(client->get(url, m_headers).toJsonObject().value("result").toString());
}

int Anikoto::loadShow(Client *client, ShowData &show, LoadParts parts) const {
    QString epUrl = hostUrl() + "ajax/episode/list/" + show.link + "?vrf=";
    QString epHtml = client->get(epUrl, m_headers).toJsonObject().value("result").toString();
    auto doc = Html::parse(epHtml);
    auto eps = doc ? doc.select("//a[@data-ids and @data-num]") : QVector<Html::Node>{};

    if (parts.testFlag(CountOnly)) return eps.size();

    if (parts.testFlag(Episodes)) {
        for (const auto &ep : std::as_const(eps)) {
            QString ids = ep.attr("data-ids");
            if (ids.isEmpty()) continue;
            float num = ep.attr("data-num").toFloat();
            // The stable `data-ids` blob is the episode's server key - stored as the episode link.
            show.addEpisode(0, num, ids, ep.attr("data-title").simplified());
        }
    }

    if (parts.testFlag(Details)) {
        // The tooltip's synopsis is truncated server-side ("...using..."), so it is fetched only to
        // map the numeric id to its slug; the watch page carries the full text and richer metadata.
        auto tip = Html::parse(client->get(hostUrl() + "ajax/anime/tooltip/" + show.link, m_headers).body);
        auto watchLink = tip ? tip.selectFirst("//div[contains(@class,'actions')]//a[contains(@class,'watch')]")
                             : Html::Node{};
        if (const QString watchUrl = watchLink ? watchLink.attr("href") : QString(); !watchUrl.isEmpty())
            loadDetails(client, show, watchUrl);
    }
    return eps.size();
}

// Rows under .bmeta read "<label>: <span>value</span>".
static QString metaField(const Html &page, const char *label) {
    auto node = page.selectFirst(QStringLiteral("//div[contains(@class,'bmeta')]//div[contains(text(),'%1')]/span")
                                     .arg(QLatin1String(label)));
    return node ? node.text().simplified() : QString();
}

void Anikoto::loadDetails(Client *client, ShowData &show, const QString &watchUrl) const {
    auto page = Html::parse(client->get(watchUrl, m_headers).body);
    if (!page) return;

    if (auto synopsis = page.selectFirst("//div[contains(@class,'synopsis')]//div[contains(@class,'content')]"))
        show.description = synopsis.text().simplified();

    show.status      = metaField(page, "Status");
    show.releaseDate = metaField(page, "Aired");
    show.score       = metaField(page, "MAL");

    const auto genreLinks = page.select("//div[contains(@class,'bmeta')]//div[contains(text(),'Genres')]/span/a");
    for (const auto &genre : genreLinks) {
        const QString name = genre.text().simplified();
        if (!name.isEmpty()) show.genres.push_back(name);
    }

    // The banner spells the date out in GMT; its countdown carries the same instant as an epoch.
    if (auto countdown = page.selectFirst("//div[contains(@class,'next-episode')]//span[@data-target]")) {
        if (const qint64 epoch = countdown.attr("data-target").toLongLong(); epoch > 0)
            show.updateTime = QDateTime::fromSecsSinceEpoch(epoch).toString("ddd d MMM 'at' HH:mm");
    }
}

QList<VideoServer> Anikoto::loadServers(Client *client, const PlaylistItem *episode) const {
    QList<VideoServer> servers;
    QString url = hostUrl() + "ajax/server/list?servers=" + QUrl::toPercentEncoding(episode->link);
    QString html = client->get(url, m_headers).toJsonObject().value("result").toString();
    auto doc = Html::parse(html);
    if (!doc) return servers;

    auto typeDivs = doc.select("//div[contains(@class,'type') and @data-type]");
    for (const auto &td : std::as_const(typeDivs)) {
        bool isDub = td.attr("data-type") == "dub";
        QString suffix = isDub ? " Dub" : " Sub";
        auto tr = isDub ? VideoServer::Dub : VideoServer::Sub;
        auto lis = td.select(".//li[@data-link-id]");
        for (const auto &li : std::as_const(lis)) {
            QString linkId = li.attr("data-link-id");
            if (linkId.isEmpty()) continue;
            servers.emplaceBack(li.text().simplified() + suffix, linkId, tr);
        }
    }
    return servers;
}

PlayInfo Anikoto::extractSource(Client *client, VideoServer server) {
    // data-link-id -> the actual embed page url (vidtube / megaplay / vidwish / ...).
    QString getUrl = hostUrl() + "ajax/server?get=" + QUrl::toPercentEncoding(server.link);
    QJsonObject result = client->get(getUrl, m_headers).toJsonObject().value("result").toObject();
    QString embedUrl = result.value("url").toString();
    if (embedUrl.isEmpty()) return {};
    return extractEmbed(client, embedUrl, server);
}

PlayInfo Anikoto::extractEmbed(Client *client, const QString &embedUrl, const VideoServer &server) const {
    PlayInfo info;
    QUrl u(embedUrl);
    QString origin = u.scheme() + "://" + u.host();
    QString type = server.translation == VideoServer::Dub ? "dub" : "sub";

    QMap<QString, QString> pageHeaders = m_headers;
    pageHeaders["Referer"] = hostUrl();
    QString page = client->get(embedUrl, pageHeaders).body;

    // These embeds are all megaplay/megacloud-family: the player id lives on #megaplay-player.
    static const QRegularExpression idRe(R"RX(id="megaplay-player"[^>]*?data-id="(\d+)")RX");
    auto m = idRe.match(page);
    QString id = m.hasMatch() ? m.captured(1) : QString();
    if (id.isEmpty()) {
        static const QRegularExpression anyId(R"RX(data-id="(\d+)")RX");
        auto m2 = anyId.match(page);
        if (m2.hasMatch()) id = m2.captured(1);
    }
    if (id.isEmpty()) return info;

    QMap<QString, QString> srcHeaders = m_headers;
    srcHeaders["Referer"] = embedUrl;
    // Most hosts expose getSourcesNew; vidwish-style hosts use getSources. Try both.
    for (const QString &endpoint : {QStringLiteral("stream/getSourcesNew"), QStringLiteral("stream/getSources")}) {
        QString srcUrl = origin + "/" + endpoint + "?id=" + id + "&type=" + type;
        QJsonObject json = client->get(srcUrl, srcHeaders).toJsonObject();
        QString file = json.value("sources").toObject().value("file").toString();
        if (file.isEmpty()) continue;

        for (const QJsonValue &t : json.value("tracks").toArray()) {
            QJsonObject to = t.toObject();
            if (to.value("kind").toString() != "captions") continue;
            QString subUrl = to.value("file").toString();
            if (!subUrl.isEmpty())
                info.subtitles.emplaceBack(QUrl(subUrl), to.value("label").toString());
        }
        // The subtitle host 403s without the embed origin as Referer; the video has the proxy's own.
        info.addHeader("Referer", origin + "/");
        info.addHeader("User-Agent", m_headers["User-Agent"]);

        HlsProxy *proxy = HlsProxy::instance();
        info.videos.emplaceBack(QUrl(proxy ? proxy->playlistUrl(file, origin + "/") : file), server.name);
        break;
    }
    return info;
}
