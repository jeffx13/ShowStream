#include "ui/subtitlesearch.h"
#include "app/async.h"
#include "app/exception.h"
#include "app/logger.h"
#include "app/settings.h"
#include "media/mpvplayer.h"
#include "net/client.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QSaveFile>
#include <QRegularExpression>
#include <QtConcurrent/QtConcurrentRun>

namespace {

// Providers throw, and an exception escaping a worker is std::terminate.
template <typename F>
auto guarded(const CancelToken &cancel, F &&fn) -> decltype(fn()) {
    try {
        return fn();
    } catch (const AppException &e) {
        if (!cancel.isCancelled()) e.report();
        e.log();
    } catch (const std::exception &e) {
        logWarn() << "Subtitles" << e.what();
    }
    return {};
}

QStringList releaseTags(const QString &text) {
    constexpr int kMaxTags = 4;   // more than this and the chips crowd out the name
    struct Pattern { QRegularExpression re; const char *suffix; };
    static const Pattern patterns[] = {
        {QRegularExpression(R"(\b(2160p|1440p|1080p|720p|480p)\b)", QRegularExpression::CaseInsensitiveOption), ""},
        {QRegularExpression(R"(\b(BluRay|BDRip|BRRip|WEB-?DL|WEBRip|HDTV|DVDRip|REMUX)\b)", QRegularExpression::CaseInsensitiveOption), ""},
        {QRegularExpression(R"(\b(x265|x264|HEVC|AVC)\b)", QRegularExpression::CaseInsensitiveOption), ""},
        {QRegularExpression(R"(\b(\d{2}(?:\.\d{1,3})?)\s*FPS\b)", QRegularExpression::CaseInsensitiveOption), " fps"},
    };

    QStringList tags;
    for (const Pattern &p : patterns) {
        auto it = p.re.globalMatch(text);
        while (it.hasNext() && tags.size() < kMaxTags) {
            const QString tag = it.next().captured(1) + QLatin1String(p.suffix);
            if (!tags.contains(tag, Qt::CaseInsensitive)) tags.append(tag);
        }
    }
    return tags;
}

QString prettyName(const QString &name) {
    static const QRegularExpression extension(R"(\.(srt|ass|ssa|vtt|sub|txt)$)", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression langSuffix(R"([.\s_-]+(en|eng|english)$)", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression separators(R"([._]+)");
    QString s = name;
    s.remove(extension);
    s.remove(langSuffix);
    s.replace(separators, QStringLiteral(" "));
    return s.simplified();
}

}

SubtitleSearch::SubtitleSearch(QObject *parent) : QAbstractListModel(parent) {
    connect(&m_searchWatcher, &QFutureWatcher<QList<SubDl::Result>>::finished, this, [this]() {
        if (!m_cancel.isCancelled()) {
            setResults(m_searchWatcher.result());
            m_searchedQuery = m_query;
        }
        m_cancel.reset();
        emit isLoadingChanged();
    });
}

MpvPlayer *SubtitleSearch::mpv() {
    auto *player = MpvPlayer::instance();
    if (player && !m_mpvConnected) {
        m_mpvConnected = true;
        for (auto signal : {&MpvPlayer::primarySubIdChanged, &MpvPlayer::secondarySubIdChanged,
                            &MpvPlayer::externalSubsChanged})
            connect(player, signal, this, &SubtitleSearch::refreshSlots);
    }
    return player;
}

SubtitleSearch::~SubtitleSearch() {
    m_cancel.cancel();
    // The workers capture `this`; without waiting they can touch a destroyed model at shutdown.
    waitFor(m_searchWatcher, "SubtitleSearch search");
    waitFor(m_fetchWatcher,  "SubtitleSearch fetch");
}

int SubtitleSearch::rowForFileId(const QString &fileId) const {
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows[i].result.fileId == fileId) return i;
    return -1;
}

int SubtitleSearch::slotFor(const QString &localPath) {
    auto *player = mpv();
    if (!player || localPath.isEmpty()) return 0;
    const qint64 id = player->externalSubId(localPath);
    if (id == 0) return 0;
    return id == player->primarySubId()   ? 1
         : id == player->secondarySubId() ? 2 : 0;
}

void SubtitleSearch::refreshSlots() {
    bool changed = false;
    for (Row &row : m_rows) {
        const int slot = slotFor(row.localPath);
        if (slot != row.slot) { row.slot = slot; changed = true; }
    }
    if (changed)
        emit dataChanged(index(0), index(m_rows.size() - 1), {SlotRole});
}

int SubtitleSearch::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant SubtitleSearch::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_rows.size()) return {};
    const Row &row = m_rows[index.row()];
    switch (role) {
    case DisplayNameRole: return row.displayName;
    case ReleaseRole:     return row.result.releaseName;
    case LanguageRole:    return row.result.language;
    case AuthorRole:      return row.result.author;
    case EpisodeRole:     return row.result.episode > 0 ? QString("E%1").arg(row.result.episode) : QString();
    case HiRole:          return row.result.hearingImpaired;
    case TagsRole:        return row.tags;
    case SlotRole:        return row.slot;
    case FetchingRole:    return row.fetching;
    default:              return {};
    }
}

QHash<int, QByteArray> SubtitleSearch::roleNames() const {
    return {{DisplayNameRole, "displayName"}, {ReleaseRole,   "release"},
            {LanguageRole,    "language"},    {AuthorRole,    "author"},
            {EpisodeRole,     "episodeLabel"},{HiRole,        "hearingImpaired"},
            {TagsRole,        "tags"},        {SlotRole,      "slot"},
            {FetchingRole,    "fetching"}};
}

void SubtitleSearch::setResults(const QList<SubDl::Result> &results) {
    QList<Row> rows;
    rows.reserve(results.size());
    for (const SubDl::Result &r : results) {
        Row row{r, prettyName(r.name), releaseTags(r.releaseName + QChar(' ') + r.name)};
        // An earlier result may still be in a slot; without this there is no row to unpick it from.
        if (const QString cached = SubDl::cachePath(r.fileId); QFileInfo(cached).size() > 0) {
            row.localPath = cached;
            row.slot = slotFor(cached);
        }
        rows.append(std::move(row));
    }

    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
    emit countChanged();
}

void SubtitleSearch::search(const QString &query) {
    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty() || m_searchWatcher.isRunning()) return;

    m_query = trimmed;
    emit queryChanged();

    // Read here, on the GUI thread; the worker must not touch QSettings.
    const QString apiKey = Settings::instance().subdlApiKey();
    const QString languages = Settings::instance().subdlLanguages();

    m_cancel.reset();
    m_searchWatcher.setFuture(QtConcurrent::run([this, trimmed, apiKey, languages] {
        Client client(m_cancel);
        return guarded(m_cancel, [&] { return SubDl::search(&client, trimmed, apiKey, languages); });
    }));
    emit isLoadingChanged();
}

void SubtitleSearch::use(int row, bool secondary) {
    if (row < 0 || row >= m_rows.size()) return;
    auto *player = mpv();
    if (!player) return;

    const SubDl::Result result = m_rows[row].result;
    if (!m_rows[row].localPath.isEmpty()) {   // cached: nothing to wait on
        player->useExternalSubtitle(m_rows[row].localPath, result.name, result.language, secondary);
        return;
    }
    if (m_fetchWatcher.isRunning()) return;   // one download at a time

    m_rows[row].fetching = true;
    emit dataChanged(index(row), index(row), {FetchingRole});

    m_fetchWatcher.setFuture(QtConcurrent::run([this, result, secondary] {
        Client client(m_cancel);
        const QString path = guarded(m_cancel, [&] { return SubDl::fetch(&client, result); });
        QMetaObject::invokeMethod(this, [this, result, path, secondary] {
            const int row = rowForFileId(result.fileId);
            if (row < 0) return;   // a new search replaced the list while this was in flight
            m_rows[row].fetching = false;
            m_rows[row].localPath = path;
            emit dataChanged(index(row), index(row), {FetchingRole, SlotRole});
            if (path.isEmpty()) return;
            if (auto *player = mpv())
                player->useExternalSubtitle(path, result.name, result.language, secondary);
        }, Qt::QueuedConnection);
    }));
}

void SubtitleSearch::searchIfNew(const QString &query) {
    if (query.trimmed() != m_searchedQuery) search(query);
}

void SubtitleSearch::cancel() {
    m_cancel.cancel();   // covers a fetch too, which isLoading does not report
}

namespace {

constexpr const char *kApi = "https://api.subdl.com/api/v1/subtitles";
constexpr const char *kFiles = "https://dl.subdl.com";

SubDl::Result fileToResult(const QJsonObject &file, const QJsonObject &release) {
    SubDl::Result r;
    r.fileId          = file["file_n_id"].toString();
    r.name            = file["name"].toString();
    r.releaseName     = release["release_name"].toString();
    r.language        = file["language"].toString();
    r.author          = release["author"].toString();
    r.season          = file["season"].toInt();
    r.episode         = file["episode"].toInt();
    r.size            = file["size"].toInteger();
    r.hearingImpaired = file["hi"].toBool();
    r.url             = QUrl(QLatin1String(kFiles) + file["url"].toString());
    return r;
}

}

QList<SubDl::Result> SubDl::search(Client *client, const QString &query,
                                   const QString &apiKey, const QString &languages) {
    if (apiKey.isEmpty())
        throw AppException("No SubDL API key set. Add subtitles/subdlApiKey to settings.ini.", "Subtitles");

    const QMap<QString, QString> params = {
        {"api_key", apiKey},
        {"film_name", query},
        {"languages", languages},
        {"subs_per_page", "30"},
        {"unpack", "1"},   // yields per-file .srt urls, so no archive to unpack
    };

    const auto response = client->get(kApi, {}, params);
    if (response.code != 200)
        throw AppException(QString("SubDL returned %1.").arg(response.code), "Subtitles");

    const QJsonObject json = response.toJsonObject();
    if (!json["status"].toBool()) {
        const QString error = json["error"].toString();
        throw AppException(error.isEmpty() ? QStringLiteral("SubDL rejected the search.") : error, "Subtitles");
    }

    QList<Result> results;
    const QJsonArray releases = json["subtitles"].toArray();
    for (const QJsonValue &value : releases) {
        const QJsonObject release = value.toObject();
        for (const QJsonValue &file : release["unpack_files"].toArray()) {
            Result r = fileToResult(file.toObject(), release);
            if (!r.fileId.isEmpty() && !r.url.isEmpty())
                results.append(r);
        }
    }
    return results;
}

QString SubDl::cachePath(const QString &fileId) {
    return QDir::cleanPath(Settings::tempDir() + QStringLiteral("/subtitles/") + fileId + QStringLiteral(".srt"));
}

QString SubDl::fetch(Client *client, const Result &result) {
    const QString path = cachePath(result.fileId);
    // A half-written file from an interrupted run is re-fetched, not served.
    if (const qint64 cached = QFileInfo(path).size();
        cached > 0 && (result.size <= 0 || cached == result.size))
        return path;

    const auto response = client->getBytes(result.url.toString());
    if (response.code != 200 || response.bytes.isEmpty())
        throw AppException(QString("Could not download %1.").arg(result.name), "Subtitles");

    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        throw AppException("Could not write the subtitle to the cache.", "Subtitles");
    file.write(response.bytes);
    if (!file.commit())
        throw AppException("Could not write the subtitle to the cache.", "Subtitles");
    return path;
}
