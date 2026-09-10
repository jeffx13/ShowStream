#include "media/serverselector.h"
#include "providers/showprovider.h"
#include "net/cloudflare.h"
#include "app/logger.h"
#include "app/settings.h"
#include <QtConcurrent/QtConcurrentRun>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <QUrl>
#include <QRegularExpression>
#include <QElapsedTimer>
#include <QThread>
#include "app/exception.h"

using Playability = ServerSelector::Playability;

namespace {

// "Ask again", not "gone" - including HlsProxy's 502 for an unreachable upstream. One flaky
// CDN host answers this way while its siblings serve fine, so none of these may condemn.
bool isTransient(int code) {
    return code <= 0 || code == 408 || code == 425 || code == 429
        || code == 500 || code == 502 || code == 503 || code == 504;
}

// Bypass off, or a dead CDN's 403 opens a browser.
Client::Response probe(Client *client, const QString &url,
                       QMap<QString, QString> headers, bool head,
                       const QString &range = {}) {
    if (!range.isEmpty()) headers.insert("Range", range);
    Client prober = *client;
    prober.setBypassEnabled(false);
    // HlsProxy holds the connection while the upstream builds a playlist; elsewhere that silence means dead.
    if (QUrl(url).host() == QLatin1String("127.0.0.1")) prober.setTimeout(45000);

    Client::Response response;
    QElapsedTimer timer;
    for (int attempt = 0; attempt < 3; ++attempt) {
        timer.start();
        response = head ? prober.head(url, headers) : prober.get(url, headers);
        if (!isTransient(response.code) || prober.isCancelled()) break;
        // A slow failure is a dead host, and retrying it would stall the race behind it.
        if (timer.elapsed() > 2000) break;
        if (attempt < 2) QThread::msleep(200 << attempt);
    }
    return response;
}

template <typename Resolve>
Playability keyReachability(Client *client, const QString &body, const QMap<QString, QString> &headers,
                            const Resolve &resolve) {
    static const QRegularExpression keyRe(QStringLiteral("#EXT-X-KEY:([^\r\n]+)"));
    const auto keyMatch = keyRe.match(body);
    if (!keyMatch.hasMatch()) return Playability::Playable;

    static const QRegularExpression uriRe(QStringLiteral("URI=\"([^\"]+)\"|URI=([^\\s,]+)"));
    const auto uriMatch = uriRe.match(keyMatch.captured(1));
    if (!uriMatch.hasMatch()) return Playability::Playable;

    const QString keyUri = uriMatch.captured(1).isEmpty() ? uriMatch.captured(2) : uriMatch.captured(1);
    const QString keyUrl = resolve(keyUri);
    const auto head = probe(client, keyUrl, headers, true);
    if (head.code >= 200 && head.code < 400) return Playability::Playable;
    const auto get = probe(client, keyUrl, headers, false);
    if (get.code >= 200 && get.code < 400 && !get.body.isEmpty()) return Playability::Playable;
    return (isTransient(head.code) || isTransient(get.code)) ? Playability::Unknown : Playability::Broken;
}

QString firstUriAfter(const QStringList &lines, const QString &marker) {
    for (int i = 0; i < lines.size(); ++i) {
        if (!marker.isEmpty() && !lines[i].trimmed().startsWith(marker)) continue;
        for (int j = i + (marker.isEmpty() ? 0 : 1); j < lines.size(); ++j) {
            const QString u = lines[j].trimmed();
            if (!u.isEmpty() && !u.startsWith('#')) return u;
        }
        return {};
    }
    return {};
}

// Probe down to a real segment - an intact playlist with 404 segments otherwise fakes "working".
Playability checkHls(Client *client, const QString &url, const QMap<QString, QString> &headers) {
    QString target = url;
    for (int depth = 0; depth < 2; ++depth) {   // one master -> one media playlist
        const auto pl = probe(client, target, headers, false, QStringLiteral("bytes=0-131071"));
        if (isTransient(pl.code)) return Playability::Unknown;
        if (pl.code < 200 || pl.code >= 400) return Playability::Broken;
        if (!pl.body.startsWith("#EXTM3U")) return Playability::Playable;   // got media bytes - reachable

        const QUrl base(target);
        auto resolve = [&base](const QString &u) {
            return (QUrl(u).scheme().isEmpty() ? base.resolved(QUrl(u)) : QUrl(u)).toString();
        };
        if (const auto key = keyReachability(client, pl.body, headers, resolve);
            key != Playability::Playable)
            return key;

        const QStringList lines = pl.body.split('\n');
        if (const QString variant = firstUriAfter(lines, QStringLiteral("#EXT-X-STREAM-INF")); !variant.isEmpty()) {
            target = resolve(variant);
            continue;
        }

        const QString segment = firstUriAfter(lines, {});
        if (segment.isEmpty()) return Playability::Broken;
        const QString segUrl = resolve(segment);
        const auto seg = probe(client, segUrl, headers, false, QStringLiteral("bytes=0-0"));
        if (seg.code == 200 || seg.code == 206) return Playability::Playable;
        const auto head = probe(client, segUrl, headers, true);
        if (head.code >= 200 && head.code < 400) return Playability::Playable;
        return (isTransient(seg.code) || isTransient(head.code)) ? Playability::Unknown : Playability::Broken;
    }
    return Playability::Unknown;   // nested masters beyond two levels - can't verify
}

}

Playability ServerSelector::playability(Client *client, PlayInfo &playItem) {
    // An empty extraction says nothing about the server; its embed page may have been throttled.
    if (playItem.videos.isEmpty()) return Playability::Unknown;
    const auto &video = playItem.videos.first();
    if (video.url.isLocalFile()) return Playability::Playable;

    const QString url = video.url.toString();

    // A stream can be challenged separately from the site that linked it, and mpv has no jar.
    Cloudflare::applyClearanceHeaders(video.url, playItem.headers);
    const auto &headers = playItem.headers;

    // Match the path, not the url: proxied ones carry the upstream in a query string.
    const bool looksHls = video.url.path().endsWith(QLatin1String(".m3u8"), Qt::CaseInsensitive);

    Client::Response headResp;
    QString contentType;
    if (!looksHls) {
        headResp = probe(client, url, headers, true);
        contentType = headResp.header("Content-Type").toLower();
    }
    Cloudflare::applyClearanceHeaders(video.url, playItem.headers);   // probe may have solved it

    // Many hosts refuse HEAD; settle it with a ranged GET before giving up.
    if (!looksHls && (headResp.code <= 0 || headResp.code == 405 || contentType.isEmpty())) {
        auto getResp = probe(client, url, headers, false, QStringLiteral("bytes=0-1023"));
        if (isTransient(getResp.code)) return Playability::Unknown;
        if (getResp.code >= 400) return Playability::Broken;
        if (contentType.isEmpty()) contentType = getResp.header("Content-Type").toLower();
        if (headResp.code <= 0) headResp = getResp;
        if (getResp.body.startsWith(QLatin1String("#EXTM3U")))
            contentType = QStringLiteral("application/vnd.apple.mpegurl");
    }

    if (looksHls || contentType.contains("mpegurl"))
        return checkHls(client, url, headers);

    auto ranged = probe(client, url, headers, false, QStringLiteral("bytes=0-0"));
    if (ranged.code == 200 || ranged.code == 206) return Playability::Playable;
    if (headResp.code >= 200 && headResp.code < 400) {
        bool isMp4 = url.endsWith(".mp4", Qt::CaseInsensitive) || contentType.startsWith("video/mp4");
        if (isMp4 || contentType.startsWith("video/") || contentType.isEmpty())
            return Playability::Playable;
    }
    return (isTransient(ranged.code) || isTransient(headResp.code)) ? Playability::Unknown
                                                                   : Playability::Broken;
}

ServerSelector::Result ServerSelector::findWorkingServer(Client *client, ShowProvider *provider, QList<VideoServer> &servers) {
    Result result;

    const auto want = Settings::instance().preferDub() ? VideoServer::Dub : VideoServer::Sub;
    const bool hasPreferredLang =
        std::any_of(servers.begin(), servers.end(),
                    [want](const VideoServer &s) { return s.translation == want; });

    QString preferred = provider->preferredServer();
    const bool userChose = !preferred.isEmpty();
    if (!userChose) {
        int best = -1;
        for (int i = 0; i < servers.size(); ++i) {
            if (hasPreferredLang && servers[i].translation != want) continue;
            if (servers[i].resolution == 0) continue;
            if (best < 0 || servers[i].resolution > servers[best].resolution) best = i;
        }
        if (best >= 0) preferred = servers[best].name;
    }

    if (!preferred.isEmpty()) {
        auto it = std::find_if(servers.begin(), servers.end(),
                               [&](const VideoServer &s) { return s.name == preferred; });
        if (it != servers.end() && (it->translation == want || !hasPreferredLang)) {
            int idx = std::distance(servers.begin(), it);
            // A preferred server that throws (Iyf key refresh) falls through to the race.
            const char *why = userChose ? "preferred server" : "best quality";
            try {
                auto playInfo = provider->extractSource(client, *it);
                const auto verdict = playability(client, playInfo);
                if (verdict == Playability::Playable) {
                    logOk() << "Server" << "Using" << why << it->name;
                    result.cachedSources.insert(it->name, playInfo);
                    result.index = idx;
                    result.playInfo = std::move(playInfo);
                    return result;
                }
                if (verdict == Playability::Broken) logWarn() << "Server" << why << it->name << "is broken";
                else                                logWarn() << "Server" << why << it->name << "did not answer";
            } catch (AppException &e) {
                e.log();
            } catch (const std::exception &e) {
                logWarn() << "Server" << why << it->name << "failed:" << e.what();
            }
        }
    }

    const auto rest = std::stable_partition(servers.begin(), servers.end(),
                                            [want](const VideoServer &s) { return s.translation == want; });
    int pivot = int(std::distance(servers.begin(), rest));
    if (pivot == 0) pivot = servers.size();  // no preferred-language servers - race all

    std::mutex resultMutex;
    QHash<QString, PlayInfo> extractedSources;
    int winner = -1;
    PlayInfo winnerPlayInfo;

    auto raceRange = [&](int lo, int hi) {
        if (lo >= hi || client->isCancelled()) return;
        std::atomic<int>  winnerIndex{-1};
        CancelToken       raceOver;

        QList<QFuture<void>> jobs;
        jobs.reserve(hi - lo);
        for (int i = lo; i < hi; ++i) {
            if (client->isCancelled()) break;
            jobs.push_back(QtConcurrent::run([i, &servers, client, provider,
                                              &winnerIndex, &winnerPlayInfo,
                                              &resultMutex, &extractedSources,
                                              &raceOver]() {
                Client subClient = client->withCancel(raceOver);
                if (subClient.isCancelled()) return;

                try {
                    auto playInfo = provider->extractSource(&subClient, servers[i]);
                    if (subClient.isCancelled()) return;

                    const auto verdict = playability(&subClient, playInfo);
                    if (verdict == Playability::Playable) {
                        if (subClient.isCancelled()) return;

                        std::lock_guard<std::mutex> lock(resultMutex);
                        extractedSources.insert(servers[i].name, playInfo);

                        int expected = -1;
                        if (winnerIndex.compare_exchange_strong(expected, i)) {
                            winnerPlayInfo = std::move(playInfo);
                            raceOver.cancel();
                            logOk() << "Server" << "Using" << servers[i].name;
                        }
                        return;
                    }
                    if (subClient.isCancelled()) return;   // lost the race, not broken
                    if (verdict == Playability::Broken) logWarn() << "Server" << servers[i].name << "is broken";
                    else                                logWarn() << "Server" << servers[i].name << "did not answer";
                } catch (AppException &e) {
                    e.log();
                } catch (const std::exception &e) {
                    logWarn() << "Server" << servers[i].name << e.what();
                } catch (...) {
                    logWarn() << "Server" << servers[i].name << "unknown error";
                }
            }));
        }

        for (auto &job : jobs)
            job.waitForFinished();
        if (winnerIndex.load() >= 0) winner = winnerIndex.load();
    };

    raceRange(0, pivot);
    if (winner < 0)
        raceRange(pivot, servers.size());

    // No pruning: broken servers stay (marked Working/Broken later) so indices stay stable.
    result.index = winner;
    if (winner >= 0) {
        std::lock_guard<std::mutex> lock(resultMutex);
        result.playInfo = std::move(winnerPlayInfo);
    }
    result.cachedSources = std::move(extractedSources);
    return result;
}