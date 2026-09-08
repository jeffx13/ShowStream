#include "media/danmaku.h"
#include "app/logger.h"

#include <QDir>
#include <QMutex>
#include <QSaveFile>
#include <QSet>
#include <QtMath>

#include <algorithm>

namespace {

constexpr int    kResX        = 1920;
constexpr int    kResY        = 1080;
constexpr int    kTopMargin   = 8;
constexpr double kBaseFont    = 48.0;   // px at 1080p
constexpr double kStaticSecs  = 5.0;
constexpr double kScrollSecs  = 10.0;   // at 100% speed
constexpr int    kMaxComments = 10000;
constexpr int    kMaxTextLen  = 100;

QMutex         g_optionsMutex;
DanmakuOptions g_options;

struct Layout {
    QString     key;
    QByteArray  fingerprint;
    QStringList events;
};
QMutex g_layoutMutex;
Layout g_layout;

QByteArray layoutFingerprint(const DanmakuOptions &o, int rawCount) {
    return QByteArray::number(o.fontScalePct) + o.font.toUtf8() + "|"
         + QByteArray::number(o.speedPct)     + "|" + QByteArray::number(o.areaPct)
         + "|" + QByteArray::number(o.maxLines)   + "|" + QByteArray::number(o.minWeight)
         + "|" + QByteArray::number(o.maxOnScreen)
         + "|" + (o.blockScroll ? "1" : "0") + (o.blockTop ? "1" : "0")
         + (o.blockBottom ? "1" : "0") + (o.blockColour ? "1" : "0")
         + (o.blockRepeat ? "1" : "0")
         + "|" + QByteArray::number(rawCount);
}

// 0.55 over-estimates Latin slightly, which is the safe direction for overlap.
bool isWide(char32_t cp) {
    return (cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2E80 && cp <= 0x303E)
        || (cp >= 0x3041 && cp <= 0x33FF) || (cp >= 0x3400 && cp <= 0x4DBF)
        || (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0xA000 && cp <= 0xA4CF)
        || (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF)
        || (cp >= 0xFE30 && cp <= 0xFE6F) || (cp >= 0xFF00 && cp <= 0xFF60)
        || (cp >= 0xFFE0 && cp <= 0xFFE6) || (cp >= 0x20000 && cp <= 0x2FFFD)
        || (cp >= 0x30000 && cp <= 0x3FFFD);
}

double textWidth(const QString &text, double fontPx) {
    double em = 0.0;
    for (char32_t cp : text.toUcs4())
        em += isWide(cp) ? 1.0 : 0.55;
    return fontPx * em;
}

// Braces and backslashes would otherwise be read as override tags.
QString sanitize(const QString &raw) {
    QString out;
    out.reserve(raw.size());
    for (QChar ch : raw) {
        const char16_t u = ch.unicode();
        if (u == u'\n' || u == u'\r' || u == u'\t') out += u' ';
        else if (u < 0x20)                          continue;
        else if (u == u'{')                         out += QChar(0xFF5B);
        else if (u == u'}')                         out += QChar(0xFF5D);
        else if (u == u'\\')                        out += QChar(0xFF3C);
        else                                        out += ch;
    }
    return out.simplified();
}

QString assTime(double seconds) {
    if (seconds < 0) seconds = 0;
    int cs = int(qRound(seconds * 100.0));
    const int h = cs / 360000; cs %= 360000;
    const int m = cs / 6000;   cs %= 6000;
    const int s = cs / 100;    cs %= 100;
    return QStringLiteral("%1:%2:%3.%4")
        .arg(h).arg(m, 2, 10, QLatin1Char('0'))
        .arg(s, 2, 10, QLatin1Char('0')).arg(cs, 2, 10, QLatin1Char('0'));
}

struct Lane {
    double lastT        = -1e9;  // last scroller placed here
    double lastW        = 0.0;
    double blockedUntil = -1e9;  // a static comment owns the lane until this time
};

// ASS wants BBGGRR. White falls through to the style, which carries the opacity.
QString colourTag(quint32 rgb) {
    rgb &= 0xFFFFFFu;
    if (rgb == 0xFFFFFFu) return {};
    const quint32 r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    // Digits only: libass matches tag names case-sensitively and silently ignores "\C".
    const QString hex = QStringLiteral("%1%2%3")
        .arg(b, 2, 16, QLatin1Char('0')).arg(g, 2, 16, QLatin1Char('0'))
        .arg(r, 2, 16, QLatin1Char('0')).toUpper();
    QString tag = QStringLiteral("\\c&H%1&").arg(hex);
    // Dark comments vanish against dark video without a light outline.
    if (0.299 * r + 0.587 * g + 0.114 * b < 60.0)
        tag += QStringLiteral("\\3c&HFFFFFF&");
    return tag;
}

struct LaneGrid {
    double fontPx;
    int    height;
    int    count;
    double scrollSecs;
};

QStringList layOut(QList<DanmakuComment> comments, const DanmakuOptions &opt,
                   const QString &cacheKey, const LaneGrid &grid) {
    int dropMode = 0, dropText = 0, dropDup = 0, dropWeight = 0, dropDensity = 0,
        dropScreen = 0, dropLane = 0;
    const int rawCount = int(comments.size());

    QList<DanmakuComment> kept;
    kept.reserve(comments.size());
    QSet<size_t> seen;
    for (DanmakuComment &c : comments) {
        if (c.mode < 1 || c.mode > 5) { ++dropMode; continue; }
        const bool scrolling = c.mode <= 3;
        if ((scrolling && opt.blockScroll) || (c.mode == 5 && opt.blockTop)
            || (c.mode == 4 && opt.blockBottom)) { ++dropMode; continue; }
        c.text = sanitize(c.text);
        if (c.text.isEmpty() || c.text.size() > kMaxTextLen) { ++dropText; continue; }
        if (opt.blockRepeat) {
            const size_t key = qHash(c.text) ^ (size_t(c.timeMs / 1000) << 1);
            if (seen.contains(key)) { ++dropDup; continue; }
            seen.insert(key);
        }
        if (c.weight < opt.minWeight) { ++dropWeight; continue; }
        if (opt.blockColour) c.color = 0xFFFFFFu;
        kept.append(c);
    }

    // Cap by weight, not by time, so a busy episode keeps its best comments.
    if (kept.size() > kMaxComments) {
        QMap<int, int> hist;
        for (const DanmakuComment &c : kept) ++hist[c.weight];
        int running = 0, threshold = 0;
        for (auto it = hist.constEnd(); it != hist.constBegin(); ) {
            --it;
            running += it.value();
            if (running >= kMaxComments) { threshold = it.key(); break; }
        }
        const int before = int(kept.size());
        QList<DanmakuComment> filtered;
        filtered.reserve(kMaxComments);
        for (const DanmakuComment &c : kept) {
            if (c.weight < threshold) continue;
            filtered.append(c);
            if (filtered.size() >= kMaxComments) break;
        }
        kept = std::move(filtered);
        dropDensity = before - int(kept.size());
    }

    std::stable_sort(kept.begin(), kept.end(),
                     [](const DanmakuComment &a, const DanmakuComment &b) {
                         return a.timeMs < b.timeMs;
                     });

    QList<Lane> lanes(grid.count);
    QList<double> active;   // end times of what is currently on screen
    QStringList events;
    events.reserve(kept.size());

    for (const DanmakuComment &c : std::as_const(kept)) {
        const double t = c.timeMs / 1000.0;
        // Never above the base - the lane grid is built from it.
        const double sizeRatio = qBound(0.6, (c.fontSize > 0 ? c.fontSize : 25) / 25.0, 1.0);
        const double glyphPx   = grid.fontPx * sizeRatio;
        const double w = textWidth(c.text, glyphPx);
        const bool scrolling = c.mode <= 3;
        const QString sizeTag = sizeRatio < 0.999
            ? QStringLiteral("\\fs%1").arg(qMax(1, int(qRound(glyphPx))))
            : QString();

        // Only tracked when there is a cap to enforce; unbounded it would be O(n^2) over 10k comments.
        if (opt.maxOnScreen > 0) {
            active.removeIf([t](double end) { return end <= t; });
            if (active.size() >= opt.maxOnScreen) { ++dropScreen; continue; }
        }

        int placed = -1;
        if (scrolling) {
            for (int i = 0; i < grid.count; ++i) {
                const Lane &ln = lanes[i];
                if (t < ln.blockedUntil) continue;
                // The tail ahead must be fully on screen and still outrun us; the second term needs our width.
                const double gap = grid.scrollSecs * qMax(ln.lastW > 0 ? ln.lastW / (kResX + ln.lastW) : 0.0,
                                                          w / (kResX + w));
                if (t >= ln.lastT + gap) { placed = i; break; }
            }
            if (placed < 0) { ++dropLane; continue; }
            lanes[placed].lastT = t;
            lanes[placed].lastW = w;
            const int y = kTopMargin + placed * grid.height;
            events += QStringLiteral("Dialogue: 0,%1,%2,Danmaku,,0,0,0,,{%3%4\\move(%5,%6,%7,%6)}%8")
                .arg(assTime(t), assTime(t + grid.scrollSecs), colourTag(c.color), sizeTag)
                .arg(kResX).arg(y).arg(-int(qRound(w))).arg(c.text);
        } else {
            const bool top = (c.mode == 5);
            for (int n = 0; n < grid.count; ++n) {
                const int i = top ? n : grid.count - 1 - n;
                const Lane &ln = lanes[i];
                if (t < ln.blockedUntil) continue;
                if (ln.lastT > -1e8 && t < ln.lastT + grid.scrollSecs) continue;
                placed = i;
                break;
            }
            if (placed < 0) { ++dropLane; continue; }
            lanes[placed].blockedUntil = t + kStaticSecs;
            const int y = top ? kTopMargin + placed * grid.height
                              : kTopMargin + (placed + 1) * grid.height;
            events += QStringLiteral("Dialogue: 1,%1,%2,Danmaku,,0,0,0,,{%3%4\\an%5\\pos(%6,%7)}%8")
                .arg(assTime(t), assTime(t + kStaticSecs), colourTag(c.color), sizeTag)
                .arg(top ? 8 : 2).arg(kResX / 2).arg(y).arg(c.text);
        }
        if (opt.maxOnScreen > 0) active.append(t + (scrolling ? grid.scrollSecs : kStaticSecs));
    }

    if (!events.isEmpty())
        logOk() << "Danmaku" << cacheKey << "raw" << rawCount << "kept" << events.size()
               << QStringLiteral("drop{mode=%1,text=%2,dup=%3,weight=%4,density=%5,screen=%6,lane=%7}")
                      .arg(dropMode).arg(dropText).arg(dropDup).arg(dropWeight)
                      .arg(dropDensity).arg(dropScreen).arg(dropLane)
               << "lanes" << grid.count << "font" << int(grid.fontPx);
    return events;
}

// Every line carries \move or \pos, so mpv calls these signs and never applies sub-scale.
QString assDocument(const QStringList &events, const DanmakuOptions &opt, double fontPx) {
    const int alpha = qBound(0, 255 - int(qRound(opt.opacityPct / 100.0 * 255.0)), 255);
    const QString alphaHex = QStringLiteral("%1").arg(alpha, 2, 16, QLatin1Char('0')).toUpper();
    // Not arg(): a placeholder before a digit reads as two, so "&H%5000000" would mean %50.
    const QString textColour    = QLatin1String("&H") + alphaHex + QLatin1String("FFFFFF");
    const QString outlineColour = QLatin1String("&H") + alphaHex + QLatin1String("000000");

    QString out;
    out.reserve(events.size() * 80 + 1024);
    out += QStringLiteral(
        "[Script Info]\n"
        "ScriptType: v4.00+\n"
        "PlayResX: %1\n"
        "PlayResY: %2\n"
        "WrapStyle: 2\n"
        "ScaledBorderAndShadow: yes\n"
        "YCbCr Matrix: None\n\n"
        "[V4+ Styles]\n"
        "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, "
        "BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, "
        "BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
        "Style: Danmaku,%3,%4,%5,%5,%6,%6,%7,0,0,0,100,100,0,0,1,%8,%9,7,0,0,0,1\n\n"
        "[Events]\n"
        "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n")
        .arg(kResX).arg(kResY).arg(opt.font).arg(qMax(1, int(qRound(fontPx))))
        .arg(textColour).arg(outlineColour)
        .arg(opt.bold ? -1 : 0)                        // ASS booleans are 0/-1
        .arg(opt.outline == 0 ? 0 : 1)
        .arg(opt.outline == 2 ? 1 : 0);
    out += events.join(QLatin1Char('\n'));
    out += QLatin1Char('\n');
    return out;
}

}

DanmakuOptions DanmakuOptions::current() {
    QMutexLocker lock(&g_optionsMutex);
    return g_options;
}

void DanmakuOptions::set(const DanmakuOptions &options) {
    QMutexLocker lock(&g_optionsMutex);
    g_options = options;
}

QString DanmakuAss::writeFile(QList<DanmakuComment> comments, const QString &cacheKey,
                              const DanmakuOptions &opt, const QString &outDir) {
    if (!opt.enabled || comments.isEmpty() || outDir.isEmpty()) return {};
    QDir().mkpath(outDir);

    LaneGrid grid;
    grid.fontPx = kBaseFont * opt.fontScalePct / 100.0;
    grid.height = qMax(1, int(qRound(grid.fontPx * 1.15)));
    grid.count  = int((kResY * opt.areaPct / 100.0 - kTopMargin) / grid.height);
    if (opt.maxLines > 0) grid.count = qMin(grid.count, opt.maxLines);
    grid.count = qBound(1, grid.count, 64);
    grid.scrollSecs = kScrollSecs * 100.0 / qMax(1, opt.speedPct);

    const QByteArray fingerprint = layoutFingerprint(opt, int(comments.size()));
    QStringList events;
    {
        QMutexLocker lock(&g_layoutMutex);
        if (g_layout.key == cacheKey && g_layout.fingerprint == fingerprint)
            events = g_layout.events;
    }
    if (events.isEmpty()) {
        events = layOut(std::move(comments), opt, cacheKey, grid);
        if (events.isEmpty()) return {};
        QMutexLocker lock(&g_layoutMutex);
        g_layout = {cacheKey, fingerprint, events};
    }

    // Same path every time, so the player can reload the track it has open.
    const QString path = QStringLiteral("%1/danmaku_%2.ass").arg(outDir, cacheKey);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        logWarn() << "Danmaku" << "Could not write" << path;
        return {};
    }
    file.write("\xEF\xBB\xBF");   // libass sniffs encodings; the BOM removes all doubt
    file.write(assDocument(events, opt, grid.fontPx).toUtf8());
    if (!file.commit()) {
        logWarn() << "Danmaku" << "Could not commit" << path;
        return {};
    }
    return path;
}

void DanmakuAss::pruneCache(const QString &cacheDir) {
    constexpr int kMaxAgeDays = 7, kMaxFiles = 100;
    QDir dir(cacheDir);
    if (!dir.exists()) return;
    // No name filter: the subtitle cache holds .srt, not .ass, and every file here is ours.
    const auto files = dir.entryInfoList(QDir::Files, QDir::Time);
    const QDateTime cutoff = QDateTime::currentDateTime().addDays(-kMaxAgeDays);
    int kept = 0;
    for (const QFileInfo &fi : files) {
        if (++kept > kMaxFiles || fi.lastModified() < cutoff)
            QFile::remove(fi.absoluteFilePath());
    }
}
