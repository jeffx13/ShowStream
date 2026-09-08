#pragma once
#include <QString>
#include <QUrl>
#include <QMap>
#include <QList>
#include <QRegularExpression>
#include "media/danmaku.h"

struct VideoServer {
    // Sub/Dub steer the server selector; Unknown = the provider doesn't say.
    enum Translation { Unknown, Sub, Dub };

    QString name;
    QString link;
    Translation translation = Unknown;


    VideoServer(const QString& name, const QString& link, Translation translation = Unknown)
        : name(name), link(link), translation(translation) {}

    // Resolution out of the name ("... 1080p" -> 1080); 0 when it just names a host.
    int resolution() const {
        static const QRegularExpression re(QStringLiteral("(\\d{3,4})\\s*[pP]"));
        const auto match = re.match(name);
        return match.hasMatch() ? match.captured(1).toInt() : 0;
    }
};

struct Track {
    QUrl url;
    QString title;
    QString lang;
    int bitrate = 0;  // bits/sec
    int height = 0;   // resolution, for sorting video quality
    double fps = 0;

    Track(const QUrl& url, const QString& title = "", const QString& lang = "", int bitrate = 0)
        : url(url), title(title), lang(lang), bitrate(bitrate) {}

    static QString formatBitrate(int bps) {
        if (bps <= 0) return {};
        if (bps >= 1000000)
            return QString("%1 Mbps").arg(bps / 1000000.0, 0, 'f', 1);
        return QString("%1 kbps").arg(bps / 1000);
    }

};

struct Video : public Track {
    Video(const QUrl& url, const QString& title = "", int resolution = 0,
          int bitrate = 0, const QString& lang = "")
        : Track(url, title, lang, bitrate) { height = resolution; }
};

struct PlayInfo {
    QList<Video> videos;
    QList<Track> audios;
    QList<Track> subtitles;
    QMap<QString, QString> headers;
    double progress = 0.0;   // where to resume, as a fraction of the duration

    QList<DanmakuComment> danmaku;
    QString danmakuKey;

    void addHeader(const QString& key, const QString& value) {
        headers.insert(key, value);
    }

    void clear() {
        videos.clear();
        audios.clear();
        subtitles.clear();
        headers.clear();
        danmaku.clear();
        danmakuKey.clear();
        progress = 0.0;
    }
};
