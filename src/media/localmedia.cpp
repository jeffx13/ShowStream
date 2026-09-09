#include "media/localmedia.h"
#include "media/playlistitem.h"
#include "app/logger.h"
#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QRegularExpression>

namespace {

const QStringList kPlayableExtensions{
    "*.mp4", "*.mkv", "*.avi", "*.mp3", "*.flac", "*.wav", "*.ogg", "*.webm", "*.m3u8", "*.mov"
};

// "curl 'url' -H 'a: b' -H 'c: d'" -> "url|a: b|c: d", the pasted-link format the playlist parses.
LocalMedia::ParsedUrl parseCurlCommand(const QString &command) {
    static const QRegularExpression urlRe(R"(curl\s+'([^']+)')");
    static const QRegularExpression headerRe(R"(-H\s+'([^']+)')");

    LocalMedia::ParsedUrl parsed;
    const auto urlMatch = urlRe.match(command);
    if (!urlMatch.hasMatch()) return parsed;

    QStringList parts{urlMatch.captured(1)};
    for (auto it = headerRe.globalMatch(command); it.hasNext(); )
        parts << it.next().captured(1);
    parsed.raw = parts.join('|');
    parsed.url = QUrl::fromUserInput(urlMatch.captured(1));
    return parsed;
}

}

namespace LocalMedia {

ParsedUrl parse(QUrl url) {
    if (!url.isEmpty())
        return {url, url.toString(), url.isValid()};

    QString clipboard = QGuiApplication::clipboard()->text().trimmed();
    ParsedUrl parsed;
    if (clipboard.startsWith("curl")) {
        parsed = parseCurlCommand(clipboard);
    } else {
        if (clipboard.startsWith('"')) clipboard.remove(0, 1);
        if (clipboard.endsWith('"')) clipboard.chop(1);
        parsed.url = QUrl::fromUserInput(clipboard);
        parsed.raw = clipboard;
    }
    parsed.valid = parsed.url.isValid();
    return parsed;
}

bool loadFolder(const QUrl &pathUrl, const QSharedPointer<PlaylistItem> &playlist,
                const std::function<bool(const QString &)> &isRegistered,
                int curDepth, int maxDepth) {
    if (curDepth == maxDepth)
        return true;

    QUrl url = !pathUrl.isEmpty() ? pathUrl : QUrl::fromUserInput(playlist->link);
    if (!url.isValid() || !url.isLocalFile()) return false;

    QFileInfo pathInfo(url.toLocalFile());
    if (!pathInfo.exists()) {
        logWarn() << "Playlist" << pathInfo.absoluteFilePath() << "doesn't exist";
        return false;
    }

    QDir playlistDir = pathInfo.isDir() ? QDir(url.toLocalFile()) : pathInfo.dir();
    QFileInfoList fileEntries = playlistDir.entryInfoList(kPlayableExtensions, QDir::Files | QDir::NoDotAndDotDot);
    QFileInfoList dirEntries = playlistDir.entryInfoList(QStringList(), QDir::Dirs | QDir::NoDotAndDotDot);

    playlist->name = playlistDir.dirName();
    playlist->displayName = playlistDir.dirName();
    playlist->link = playlistDir.absolutePath();
    playlist->type |= PlaylistItem::Type::Local;
    playlist->clear();

    if (fileEntries.isEmpty() && dirEntries.isEmpty()) return false;

    // Resume points live in library.db. clear() above dropped the children but kept the old
    // index, which now points at nothing; only an explicitly opened file pins one here, and
    // Playlist applies the stored positions once the tree is built and sorted.
    playlist->setCurrentIndex(-1);
    const QString requestedFile = (!pathInfo.isDir() && fileEntries.contains(pathInfo))
                                      ? pathInfo.absoluteFilePath() : QString();

    static const QRegularExpression fileNameRegex{
        R"((?:[Ss](?<S>\d{1,2})[Ee](?<E>\d{1,3})[\s\-\.]*| (?<episode>\d{2,3}) ?[\s\-]*)(?<title>[^\(\)]+\w)?.*?\.\w{3,4}$)"};

    for (int i = 0; i < fileEntries.count(); ++i) {
        const QFileInfo &fileInfo = fileEntries[i];
        if (!fileInfo.isFile()) continue;

        const QRegularExpressionMatch match = fileNameRegex.match(fileInfo.fileName());
        QString title;
        int season = 0;
        float episodeNumber = -1;
        bool ok = false;

        if (match.hasMatch()) {
            title = match.captured("title").simplified();
            season = match.captured("S").toInt();
            const QString episodeStr = match.hasCaptured("E") ? match.captured("E").simplified()
                                                              : match.captured("episode").simplified();
            const float ep = episodeStr.toFloat(&ok);
            episodeNumber = ok ? ep : i;
        } else {
            title = fileInfo.baseName().simplified();
            const float ep = title.toFloat(&ok);
            if (ok) {
                episodeNumber = ep;
                title.clear();
            }
        }

        playlist->emplaceBack(season, episodeNumber, fileInfo.absoluteFilePath(), title, true);

        if (!requestedFile.isEmpty() && fileInfo.absoluteFilePath() == requestedFile)
            playlist->setCurrentIndex(playlist->count() - 1);
    }

    if (curDepth + 1 < maxDepth) {
        for (const QFileInfo &dirInfo : std::as_const(dirEntries)) {
            const QString path = dirInfo.absoluteFilePath();
            if (isRegistered(path)) continue;
            auto subPlaylist = QSharedPointer<PlaylistItem>::create();
            if (loadFolder(QUrl::fromLocalFile(path), subPlaylist, isRegistered, curDepth + 1, maxDepth)
                && !subPlaylist->isEmpty())
                playlist->append(subPlaylist);
        }
    }

    if (playlist->isEmpty()) return false;
    playlist->sort();
    return true;
}

}
