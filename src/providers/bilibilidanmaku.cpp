#include "providers/bilibilidanmaku.h"
#include "app/logger.h"

#include <QtConcurrent/QtConcurrentRun>

// Minimal protobuf reader for DmSegMobileReply; unknown fields skip by wiretype, not number.

namespace {

constexpr int kSegmentMs   = 360000;   // 6-minute chunks
constexpr int kMaxSegments = 40;       // guards a bogus duration

using u8 = quint8;

bool readVarint(const u8 *&p, const u8 *end, quint64 &out) {
    quint64 value = 0;
    int shift = 0;
    for (int i = 0; i < 10; ++i) {
        if (p >= end) return false;
        const u8 byte = *p++;
        value |= quint64(byte & 0x7F) << shift;
        if (!(byte & 0x80)) { out = value; return true; }
        shift += 7;
    }
    return false;   // overlong varint
}

// Returns false to abort the enclosing loop.
bool skipField(const u8 *&p, const u8 *end, int wire) {
    quint64 scratch = 0;
    switch (wire) {
    case 0: return readVarint(p, end, scratch);
    case 1: if (end - p < 8) return false; p += 8; return true;
    case 5: if (end - p < 4) return false; p += 4; return true;
    case 2: {
        if (!readVarint(p, end, scratch)) return false;
        if (scratch > quint64(end - p)) return false;
        p += scratch;
        return true;
    }
    // 3/4 are deprecated groups, 6/7 invalid. Neither occurs here.
    default: return false;
    }
}

bool parseElem(const u8 *p, const u8 *end, DanmakuComment &out) {
    while (p < end) {
        quint64 key = 0;
        if (!readVarint(p, end, key)) return false;
        const int field = int(key >> 3);
        const int wire  = int(key & 7);
        quint64 v = 0;

        if (field == 2 && wire == 0) {
            if (!readVarint(p, end, v)) return false;
            out.timeMs = int(qMin<quint64>(v, INT_MAX));
        } else if (field == 3 && wire == 0) {
            if (!readVarint(p, end, v)) return false;
            out.mode = int(v);
        } else if (field == 4 && wire == 0) {
            if (!readVarint(p, end, v)) return false;
            out.fontSize = int(v);
        } else if (field == 5 && wire == 0) {
            if (!readVarint(p, end, v)) return false;
            out.color = quint32(v) & 0xFFFFFFu;
        } else if (field == 7 && wire == 2) {
            if (!readVarint(p, end, v)) return false;
            if (v > quint64(end - p)) return false;
            out.text = QString::fromUtf8(reinterpret_cast<const char *>(p), qsizetype(v));
            p += v;
        } else if (field == 9 && wire == 0) {
            if (!readVarint(p, end, v)) return false;
            out.weight = int(v);
        } else if (!skipField(p, end, wire)) {
            return false;
        }
    }
    return true;
}

QList<DanmakuComment> parseSegment(const QByteArray &data) {
    QList<DanmakuComment> out;
    const u8 *p   = reinterpret_cast<const u8 *>(data.constData());
    const u8 *end = p + data.size();

    while (p < end) {
        quint64 key = 0;
        if (!readVarint(p, end, key)) break;
        const int field = int(key >> 3);
        const int wire  = int(key & 7);

        if (field == 1 && wire == 2) {          // repeated DanmakuElem
            quint64 len = 0;
            if (!readVarint(p, end, len) || len > quint64(end - p)) break;
            DanmakuComment c;
            // Absent means unrated, not worst; 0 would let minWeight erase everything.
            c.weight = 10;
            if (parseElem(p, p + len, c) && !c.text.isEmpty())
                out.append(std::move(c));
            p += len;
        } else if (!skipField(p, end, wire)) {
            break;                              // truncated: keep what we decoded
        }
    }
    return out;
}

}

QList<DanmakuComment> BilibiliDanmaku::fetchAll(Client *client, qint64 cid, int durationMs,
                                                const QMap<QString, QString> &headers,
                                                const QString &proxyApi) {
    if (!client || cid <= 0) return {};

    int segments = durationMs > 0 ? (durationMs + kSegmentMs - 1) / kSegmentMs : 8;
    segments = qBound(1, segments, kMaxSegments);

    // Own Client copy per worker, so the caller's CancelToken still aborts them.
    QList<QFuture<QList<DanmakuComment>>> futures;
    futures.reserve(segments);
    for (int i = 1; i <= segments; ++i) {
        futures << QtConcurrent::run([client, cid, i, headers, proxyApi]() -> QList<DanmakuComment> {
            Client worker = *client;
            const QString url = QStringLiteral("https://api.bilibili.com/x/v2/dm/web/seg.so");
            const QMap<QString, QString> params = {
                {"type", "1"},
                {"oid", QString::number(cid)},
                {"segment_index", QString::number(i)},
            };
            Client::Response response;
            if (proxyApi.isEmpty()) {
                response = worker.getBytes(url, headers, params);
            } else {
                auto proxied = headers;
                proxied["X-Proxy-Url"] = Client::urlWithParams(url, params);
                response = worker.getBytes(proxyApi, proxied);
            }
            if (response.code != 200 || response.bytes.isEmpty()) return {};
            return parseSegment(response.bytes);
        });
    }

    QList<DanmakuComment> all;
    for (auto &future : futures) {
        const auto part = future.result();
        all.append(part);
    }
    if (all.isEmpty())
        logWarn() << "Danmaku" << "no comments for cid" << cid << "over" << segments << "segment(s)";
    return all;
}
