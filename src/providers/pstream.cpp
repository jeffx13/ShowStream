#include "providers/pstream.h"
#include "app/logger.h"
#include "app/settings.h"
#include "media/playlistitem.h"
#include "net/hlsproxy.h"
#include <QJsonArray>
#include <QThread>
#include <QtConcurrent/QtConcurrentRun>


namespace {

constexpr const char *kTmdb   = "https://api.themoviedb.org/3";
constexpr const char *kImage  = "https://image.tmdb.org/t/p/w500";
constexpr const char *kLink   = "https://link.aether.cx";
constexpr const char *kSubs   = "https://sub.vdrk.site/v1";
constexpr int kAppendLimit    = 19;   // TMDB caps append_to_response at 20

// "<kind>/<id>" or "<kind>/<id>/<season>/<episode>". Persisted, so the shape must not change.
bool splitLink(const QString &link, QString &kind, QString &id) {
    const auto parts = link.split('/', Qt::SkipEmptyParts);
    if (parts.size() < 2) return false;
    kind = parts[0];
    id   = parts[1];
    return kind == QLatin1String("movie") || kind == QLatin1String("tv");
}

QString trimIndex(const QString &label) {
    QString base = label;
    while (!base.isEmpty() && base.back().isDigit()) base.chop(1);
    return base.trimmed();
}

}

PStream::PStream(QObject *parent) : ShowProvider(parent) {
    m_headers = {
        {"Origin",  "https://aether.bar"},
        {"Referer", "https://aether.bar/"},
        {"Accept",  "application/json"},
    };
    // The site's own read token.
    m_token = Settings::instance().value(
        "pstream/token",
        "eyJhbGciOiJIUzI1NiJ9.eyJhdWQiOiI1YjEwYWNhZDFhNjY3ZTQwMDEyMGVjMTc1ZDBjZTFmZCIsIm5iZiI6"
        "MTcyNDk1Mjg3MC45NDA4NDcsInN1YiI6IjY2ZDBhOTgyODQ1OWYzM2FmMjBmYjdkNSIsInNjb3BlcyI6WyJh"
        "cGlfcmVhZCJdLCJ2ZXJzaW9uIjoxfQ.ScGHs1VZTLGpUKWPG7EA-2T29OPcqW_qpJjKL5Yhrjc").toString();
}

QJsonObject PStream::tmdb(Client *client, const QString &path, QMap<QString, QString> params) const {
    params.insert("language", "en-US");
    auto headers = m_headers;
    headers["Authorization"] = "Bearer " + m_token;
    return client->get(kTmdb + path, headers, params).toJsonObject();
}

QList<ShowData> PStream::collect(const QJsonArray &results, const QString &kind, int showType) const {
    QList<ShowData> shows;
    shows.reserve(results.size());
    for (const auto &v : results) {
        const auto r = v.toObject();
        const int id = r["id"].toInt();
        if (id <= 0) continue;
        const QString title = kind == QLatin1String("movie") ? r["title"].toString()
                                                             : r["name"].toString();
        if (title.isEmpty()) continue;
        const QString poster = r["poster_path"].toString();
        const QString date = (kind == QLatin1String("movie") ? r["release_date"].toString()
                                                             : r["first_air_date"].toString());
        shows.emplaceBack(title, QStringLiteral("%1/%2").arg(kind, QString::number(id)),
                          poster.isEmpty() ? QString() : kImage + poster, const_cast<PStream *>(this),
                          date.left(4), showType);
    }
    return shows;
}

QList<ShowData> PStream::search(Client *client, const QString &query, int page, int typeIndex) {
    const QString kind = typeIndex == 0 ? "movie" : "tv";
    auto json = tmdb(client, "/search/" + kind, {{"query", query},
                                                 {"include_adult", "false"},
                                                 {"page", QString::number(page)}});
    return collect(json["results"].toArray(), kind,
                   typeIndex == 0 ? ShowData::Movie : ShowData::TvSeries);
}

QList<ShowData> PStream::popular(Client *client, int page, int typeIndex) {
    const QString kind = typeIndex == 0 ? "movie" : "tv";
    auto json = tmdb(client, QStringLiteral("/%1/popular").arg(kind),
                     {{"page", QString::number(page)}});
    return collect(json["results"].toArray(), kind,
                   typeIndex == 0 ? ShowData::Movie : ShowData::TvSeries);
}

QList<ShowData> PStream::latest(Client *client, int page, int typeIndex) {
    const QString kind = typeIndex == 0 ? "movie" : "tv";
    auto json = tmdb(client, QStringLiteral("/trending/%1/week").arg(kind),
                     {{"page", QString::number(page)}});
    return collect(json["results"].toArray(), kind,
                   typeIndex == 0 ? ShowData::Movie : ShowData::TvSeries);
}

int PStream::loadShow(Client *client, ShowData &show, LoadParts parts) const {
    QString kind, id;
    if (!splitLink(show.link, kind, id)) return 0;
    const bool isMovie = (kind == QLatin1String("movie"));

    auto json = tmdb(client, QStringLiteral("/%1/%2").arg(kind, id),
                     {{"append_to_response", "external_ids"}});
    if (json.isEmpty()) return 0;

    if (isMovie) {
        if (parts.testFlag(Details)) {
            show.description = json["overview"].toString();
            show.releaseDate = json["release_date"].toString();
            show.status      = json["status"].toString();
            show.score       = QString::number(json["vote_average"].toDouble(), 'f', 1);
            show.views       = QString::number(qRound(json["popularity"].toDouble()));
            const auto runtime = json["runtime"].toInt();
            if (runtime > 0) show.updateTime = QStringLiteral("%1 min").arg(runtime);
            for (const auto &g : json["genres"].toArray())
                show.genres.append(g.toObject()["name"].toString());
        }
        if (parts.testFlag(CountOnly)) return 1;
        if (parts.testFlag(Episodes))
            show.addEpisode(0, 1, show.link, json["title"].toString());
        return 1;
    }

    if (parts.testFlag(Details)) {
        show.description = json["overview"].toString();
        show.releaseDate = json["first_air_date"].toString();
        show.status      = json["status"].toString();
        show.score       = QString::number(json["vote_average"].toDouble(), 'f', 1);
        show.views       = QString::number(qRound(json["popularity"].toDouble()));
        show.updateTime  = json["last_air_date"].toString();
        for (const auto &g : json["genres"].toArray())
            show.genres.append(g.toObject()["name"].toString());
    }

    const int total = json["number_of_episodes"].toInt();
    if (!parts.testFlag(Episodes)) return total;

    QList<int> seasons;
    for (const auto &v : json["seasons"].toArray()) {
        const int n = v.toObject()["season_number"].toInt(-1);
        if (n > 0) seasons.append(n);   // 0 is the specials bucket
    }
    std::sort(seasons.begin(), seasons.end());

    // append_to_response pulls whole seasons in, so a series costs one request, not one per season.
    int count = 0;
    for (int i = 0; i < seasons.size(); i += kAppendLimit) {
        QStringList batch;
        for (int j = i; j < qMin(i + kAppendLimit, int(seasons.size())); ++j)
            batch << QStringLiteral("season/%1").arg(seasons[j]);
        auto bundle = tmdb(client, QStringLiteral("/tv/%1").arg(id),
                           {{"append_to_response", batch.join(',')}});
        for (const QString &key : std::as_const(batch)) {
            const auto episodes = bundle[key].toObject()["episodes"].toArray();
            for (const auto &v : episodes) {
                const auto e = v.toObject();
                const int sn = e["season_number"].toInt();
                const int en = e["episode_number"].toInt();
                if (en <= 0) continue;
                show.addEpisode(sn, float(en),
                                QStringLiteral("tv/%1/%2/%3").arg(id).arg(sn).arg(en),
                                e["name"].toString());
                ++count;
            }
        }
    }
    return count > 0 ? count : total;
}

QList<VideoServer> PStream::loadServers(Client * /*client*/, const PlaylistItem *episode) const {
    return {{"P-Stream", episode->link}};
}

PlayInfo PStream::extractSource(Client *client, VideoServer server) {
    PlayInfo playInfo;

    QString kind, id;
    if (!splitLink(server.link, kind, id)) return playInfo;
    const auto parts = server.link.split('/', Qt::SkipEmptyParts);
    const bool isMovie = (kind == QLatin1String("movie"));
    const QString suffix = isMovie ? QString()
                                   : QStringLiteral("/%1/%2").arg(parts.value(2), parts.value(3));

    // Nothing in the stream lookup feeds these, so overlap the two.
    const QString subsUrl = QStringLiteral("%1/%2/%3%4").arg(kSubs, kind, id, suffix);
    auto subsJob = QtConcurrent::run([this, subClient = *client, subsUrl]() mutable {
        return subClient.get(subsUrl, m_headers).toJsonArray();
    });

    // aether rate-limits a busy session, and a single 429 would read as "no stream". It clears in a second.
    const QString linkUrl = QStringLiteral("%1/%2/%3%4").arg(kLink, kind, id, suffix);
    QJsonObject json;
    QString stream;
    for (int attempt = 0; attempt < 3 && stream.isEmpty(); ++attempt) {
        if (attempt > 0) {
            if (client->isCancelled()) return playInfo;
            QThread::msleep(600 * attempt);
        }
        const auto response = client->get(linkUrl, m_headers);
        if (response.code > 0 && response.code != 429 && response.code < 500) {
            json = response.toJsonObject();
            stream = json["stream"].toString();
            if (stream.isEmpty()) break;   // a real "nothing here", not a blip
        }
    }
    if (stream.isEmpty()) {
        subsJob.waitForFinished();
        logWarn() << name() << "No stream for" << server.link;
        return playInfo;
    }

    const QUrl streamUrl(stream);

    // Upstream segments 403 on aether's referer, so pick the referer by host.
    const QString host = streamUrl.host();
    const bool viaProxy = host.endsWith(QLatin1String("aether.bar"))
                          || host.endsWith(QLatin1String("aether.cx"));
    const QString referer = viaProxy ? QStringLiteral("https://aether.bar")
                                     : QStringLiteral("https://nextgencloudfabric.com");
    playInfo.addHeader("Origin", referer);
    playInfo.addHeader("Referer", referer + '/');

    // These build the variant playlist on demand and can take half a minute; the proxy pays it once.
    QString playUrl = stream;
    if (streamUrl.path().endsWith(QLatin1String(".m3u8"), Qt::CaseInsensitive))
        if (HlsProxy *proxy = HlsProxy::instance())
            playUrl = proxy->playlistUrl(stream, referer + '/');
    playInfo.videos.emplaceBack(QUrl(playUrl), json["title"].toString());

    const QJsonArray subs = subsJob.result();
    QSet<QString> languages;
    for (const auto &v : subs) {
        const auto s = v.toObject();
        const QString file = s["file"].toString();
        const QString label = s["label"].toString();
        if (file.isEmpty() || label.isEmpty()) continue;
        // The list runs to ~90 entries, numbered per language; one each is plenty.
        const QString base = trimIndex(label);
        if (base.isEmpty() || languages.contains(base)) continue;
        languages.insert(base);
        playInfo.subtitles.emplaceBack(QUrl(file), base, base == QLatin1String("English") ? "en" : "");
    }

    logInfo() << name() << "Extracted" << playInfo.videos.size() << "video,"
           << playInfo.subtitles.size() << "subtitle tracks";
    return playInfo;
}
