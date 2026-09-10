#include "providers/animepahe.h"
#include <QUrl>
#include <QRegularExpression>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include "providers/jsunpack.h"
#include "net/hlsproxy.h"
#include "net/html.h"

// Behind a CF interactive challenge: without a local browser to drive, every request is empty.

namespace {

// The API answers under either key depending on the endpoint.
QJsonArray itemsOf(const QJsonObject &root) {
    if (root.contains("data")) return root.value("data").toArray();
    return root.value("items").toArray();
}

QVector<AnimePahe::Episode> episodesOf(const QJsonObject &root) {
    const QJsonArray items = itemsOf(root);
    QVector<AnimePahe::Episode> episodes;
    episodes.reserve(items.size());
    for (const QJsonValue &value : items) {
        const QJsonObject item = value.toObject();
        const QString session = item.value("session").toString();
        if (session.isEmpty()) continue;
        double number = item.value("episode").toDouble(-1.0);
        if (number < 0) number = item.value("episode2").toDouble(-1.0);
        episodes.append({number, session});
    }
    return episodes;
}

QString releaseUrl(const QString &host, const QString &showSession, int page) {
    return host + QStringLiteral("api?m=release&id=%1&sort=episode_desc&page=%2")
                      .arg(showSession).arg(page);
}

const char *const kInfoLabels[] = {"Synonyms", "Type", "Episodes", "Duration", "Season", "Studio"};

QString externalLinksHtml(const Html::Node &panel) {
    const auto anchors = panel.selectFirst(".//p[contains(@class,'external-links')]").select(".//a");
    if (anchors.isEmpty()) return {};
    QStringList links;
    for (const Html::Node &a : anchors) {
        QString href = a.attr("href");
        if (href.startsWith("//")) href = "https:" + href;
        const QString label = a.text().simplified();
        links << QStringLiteral("<a href=\"%1\">%2</a>").arg(href, label.isEmpty() ? href : label);
    }
    return QStringLiteral("External Links:<br>") + links.join("<br>");
}

}

QList<ShowData> AnimePahe::search(Client *client, const QString &query, int page, int /*typeIndex*/) {
    if (page > 1 || query.trimmed().isEmpty()) return {};

    const QString url = hostUrl() + "api?m=search&q=" + QUrl::toPercentEncoding(query);
    QList<ShowData> shows;
    for (const QJsonValue &value : itemsOf(client->get(url, m_headers).toJsonObject())) {
        const QJsonObject item = value.toObject();
        const QString title = item.value("title").toString();
        const QString session = item.value("session").toString();
        if (title.isEmpty() || session.isEmpty()) continue;
        shows.emplaceBack(title, session, item.value("poster").toString(), this, "", ShowData::Anime);
    }
    return shows;
}

QList<ShowData> AnimePahe::popular(Client *client, int page, int typeIndex) {
    return latest(client, page, typeIndex);
}

QList<ShowData> AnimePahe::latest(Client *client, int page, int /*typeIndex*/) {
    const QString url = hostUrl() + "api?m=airing&page=" + QString::number(page);

    QList<ShowData> shows;
    QSet<QString> seen;
    for (const QJsonValue &value : itemsOf(client->get(url, m_headers).toJsonObject())) {
        const QJsonObject item = value.toObject();
        const QString title = item.value("anime_title").toString();
        const QString session = item.value("anime_session").toString();
        if (title.isEmpty() || session.isEmpty() || seen.contains(session)) continue;
        seen.insert(session);
        shows.emplaceBack(title, session, item.value("snapshot").toString(), this,
                          item.value("fansub").toString(), ShowData::Anime);
    }
    return shows;
}

// The API only serves newest-first and one page at a time, so pull the pages in parallel batches.
QVector<AnimePahe::Episode> AnimePahe::fetchEpisodes(Client *client, const QString &showSession,
                                                     bool countOnly, int &reportedTotal) const {
    reportedTotal = -1;
    const QJsonObject first = client->get(releaseUrl(hostUrl(), showSession, 1), m_headers).toJsonObject();
    if (first.isEmpty()) return {};

    QVector<Episode> episodes = episodesOf(first);
    if (countOnly) {
        reportedTotal = first.value("total").toInt(-1);
        if (reportedTotal >= 0) return episodes;
    }

    const int currentPage = first.value("current_page").toInt(first.value("currentPage").toInt(1));
    const int lastPage    = first.value("last_page").toInt(first.value("lastPage").toInt(currentPage));

    constexpr int kPagesPerBatch = 8;
    for (int start = 2; start <= lastPage && !client->isCancelled(); start += kPagesPerBatch) {
        const int end = std::min(lastPage, start + kPagesPerBatch - 1);
        QList<QFuture<QVector<Episode>>> jobs;
        jobs.reserve(end - start + 1);
        for (int page = start; page <= end; ++page) {
            jobs.push_back(QtConcurrent::run([client, headers = m_headers, url = releaseUrl(hostUrl(), showSession, page)]() {
                Client worker = *client;
                if (worker.isCancelled()) return QVector<Episode>{};
                return episodesOf(worker.get(url, headers).toJsonObject());
            }));
        }
        for (QFuture<QVector<Episode>> &job : jobs) {
            job.waitForFinished();
            if (client->isCancelled()) break;
            episodes += job.result();
        }
    }
    return episodes;
}

int AnimePahe::loadShow(Client *client, ShowData &show, LoadParts parts) const {
    int reportedTotal = -1;
    QVector<Episode> episodes = fetchEpisodes(client, show.link, parts.testFlag(CountOnly), reportedTotal);
    if (parts.testFlag(CountOnly))
        return reportedTotal >= 0 ? reportedTotal : int(episodes.size());

    if (parts.testFlag(Episodes)) {
        std::sort(episodes.begin(), episodes.end(),
                  [](const Episode &a, const Episode &b) { return a.number < b.number; });
        for (const Episode &episode : std::as_const(episodes))
            show.addEpisode(0, float(episode.number),
                            QStringLiteral("/play/%1/%2").arg(show.link, episode.session), QString());
    }

    if (parts.testFlag(Details)) {
        const Html doc = client->get(hostUrl() + "anime/" + show.link, m_headers).toHtml();
        if (doc) {
            show.description = doc.selectFirst("//div[contains(@class,'anime-summary')]").text().simplified();

            const auto cover = doc.selectFirst("//div[contains(@class,'anime-poster')]//img");
            show.coverUrl = cover.attr("data-src");
            if (show.coverUrl.isEmpty()) show.coverUrl = cover.attr("src");

            show.status = doc.selectFirst("//div[contains(@class,'anime-info')]"
                                          "//p[strong[contains(text(),'Status:')]]//a").text().simplified();
            for (const Html::Node &genre : doc.select("//div[contains(@class,'anime-genre')]//li"))
                show.genres.push_back(genre.text().simplified());

            const auto panel = doc.selectFirst("//div[contains(@class,'col-sm-4') and contains(@class,'anime-info')]");
            if (panel) {
                QStringList extra;
                for (const char *label : kInfoLabels) {
                    const QString line = panel.selectFirst(
                        QStringLiteral(".//p[strong[contains(text(),'%1')]]").arg(QLatin1String(label))).text().simplified();
                    if (!line.isEmpty()) extra << line;
                }
                QString aired = panel.selectFirst(".//p[strong[contains(text(),'Aired')]]").text().simplified();
                if (!aired.isEmpty()) show.releaseDate = aired.remove("Aired:").simplified();
                if (const QString links = externalLinksHtml(panel); !links.isEmpty()) extra << links;

                if (!extra.isEmpty()) {
                    if (!show.description.isEmpty()) show.description += "<br><br>";
                    show.description += extra.join("<br>");
                }
            }
        }
    }
    return int(episodes.size());
}

QList<VideoServer> AnimePahe::loadServers(Client *client, const PlaylistItem *episode) const {
    const QString path = episode->link.startsWith('/') ? episode->link.mid(1) : episode->link;
    const Html doc = client->get(hostUrl() + path, m_headers).toHtml();
    if (!doc) return {};

    QList<VideoServer> servers;
    for (const Html::Node &button : doc.select("//div[@id='resolutionMenu']//button")) {
        QString kwik = button.attr("data-src");
        if (kwik.isEmpty()) continue;
        if (kwik.startsWith("//")) kwik = "https:" + kwik;
        else if (kwik.startsWith('/')) kwik = "https://kwik.cx" + kwik;
        servers.emplaceBack("Kwik " + button.text().simplified(), kwik);
    }
    return servers;
}

PlayInfo AnimePahe::extractSource(Client *client, VideoServer server) {
    PlayInfo info;
    // animepahe.ru is a parked domain now, so the referer has to be the live host.
    const QMap<QString, QString> headers{{"User-Agent", kFirefoxUserAgent}, {"Referer", hostUrl()}};

    static const QRegularExpression sourceRe(QStringLiteral(R"(const source='([^']+)')"));
    const auto match = sourceRe.match(Js::unpack(client->get(server.link, headers).body));
    if (!match.hasMatch()) return info;

    // Kwik's CDNs are behind CF, which refuses mpv's HTTP stack; over loopback mpv only sees 127.0.0.1.
    static const QString kKwikReferer = QStringLiteral("https://kwik.cx/");
    const QString url = match.captured(1);
    HlsProxy *proxy = HlsProxy::instance();
    const bool isHls = url.endsWith(".m3u8", Qt::CaseInsensitive);
    info.videos.emplaceBack(QUrl(proxy && isHls ? proxy->playlistUrl(url, kKwikReferer) : url), server.name);
    info.addHeader("Referer", kKwikReferer);
    info.addHeader("User-Agent", kFirefoxUserAgent);
    return info;
}
