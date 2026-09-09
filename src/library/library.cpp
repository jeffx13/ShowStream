#include "library/library.h"
#include "providers/providerlist.h"
#include "providers/showprovider.h"
#include "net/client.h"
#include "ui/appshell.h"
#include "app/async.h"
#include "app/logger.h"
#include "app/settings.h"
#include "app/exception.h"
#include <QDir>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QVariant>
#include <QDateTime>
#include <algorithm>

namespace {

// Shared with the trim below, so it can never drop a row HistoryPage would still show.
constexpr int kHistoryRows = 100;
constexpr int kLocalProgressRows = 2000;

QString selectEntries(const char *whereClause) {
    return QLatin1String("SELECT link, title, cover, provider, library_type, last_watched_index, "
                         "total_episodes, show_type, progress FROM shows ")
           + QLatin1String(whereClause);
}

QSqlQuery prepared(const QSqlDatabase &db, const QString &sql, const QVariantList &binds = {}) {
    QSqlQuery query(db);
    query.prepare(sql);
    for (const QVariant &bind : binds) query.addBindValue(bind);
    return query;
}

bool runQuery(QSqlQuery &query, const char *what) {
    if (query.exec()) return true;
    logError() << "Library" << what << query.lastError().text();
    return false;
}

// Invalid when the query fails or matches nothing.
QVariant firstValue(const QSqlDatabase &db, const QString &sql, const QVariantList &binds = {}) {
    QSqlQuery query = prepared(db, sql, binds);
    return (query.exec() && query.next()) ? query.value(0) : QVariant();
}

}

Library::Library(QObject *parent)
    : QAbstractListModel(parent)
{
    initDatabase();
    refreshDisplayCache();
    m_watchedFraction = Settings::instance().watchedFraction();
    connect(&Settings::instance(), &Settings::watchedPercentChanged, this, [this]() {
        m_watchedFraction = Settings::instance().watchedFraction();
        if (!m_displayCache.isEmpty())
            emit dataChanged(index(0), index(m_displayCache.size() - 1));
        emit historyChanged();
    });
    connect(&m_fetchWatcher, &QFutureWatcher<void>::finished, this, [this]() {
        m_cancel.reset();
        if (m_pendingFetchLibraryType >= 0) {
            int lt = m_pendingFetchLibraryType;
            bool forced = m_pendingFetchForced;
            m_pendingFetchLibraryType = kNoPendingFetch;
            m_pendingFetchForced = false;
            fetchUnwatchedEpisodes(lt, forced);
        }
    });
}

int Library::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_displayCache.size();
}

QVariant Library::data(const QModelIndex &index, int role) const {
    if (index.row() < 0 || index.row() >= m_displayCache.size()) return {};
    const LibraryEntry &e = m_displayCache[index.row()];
    switch (role) {
    case Role::Title: return e.title;
    case Role::Cover: return e.cover;
    case Role::UnwatchedEpisodes: {
        if (e.totalEpisodes <= 0) return -1;   // -1 = count unknown, distinct from 0 (caught up)
        int unwatched = e.totalEpisodes - e.lastWatchedIndex - 1;
        if (e.lastWatchedIndex >= 0 && e.progress < m_watchedFraction)
            unwatched += 1;
        return qMax(0, unwatched);
    }
    case Role::ShowType: return e.showType;
    case Role::Provider: return e.provider;
    default: return {};
    }
}

QHash<int, QByteArray> Library::roleNames() const {
    return {
        {Role::Title, "title"}, {Role::Cover, "cover"},
        {Role::UnwatchedEpisodes, "unwatchedEpisodes"}, {Role::ShowType, "type"},
        {Role::Provider, "provider"},
    };
}

Library::~Library() {
    disconnect(&m_fetchWatcher, nullptr, this, nullptr);
    m_cancel.cancel();
    waitFor(m_fetchWatcher, "Library episode-count fetch");
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(QStringLiteral("AoNami_Library"));
}

void Library::initDatabase() {
    QString dbPath = QDir::cleanPath(QCoreApplication::applicationDirPath() + "/library.db");
    m_db = QSqlDatabase::addDatabase("QSQLITE", QStringLiteral("AoNami_Library"));
    m_db.setDatabaseName(dbPath);

    if (!m_db.open()) {
        const QString err = m_db.lastError().text();
        logError() << "Library" << "Failed to open SQLite DB:" << err;
        // Queued: the notifier doesn't exist yet during construction.
        QMetaObject::invokeMethod(qApp, [err]() {
            AppShell::instance().reportError("Library database could not be opened:\n" + err +
                                             "\nYour library and history will not be saved this session.",
                                             "Library");
        }, Qt::QueuedConnection);
        return;
    }

    QSqlQuery query(m_db);
    query.exec("PRAGMA journal_mode=WAL");
    query.exec("PRAGMA synchronous=NORMAL");
    if (!query.exec(R"(
        CREATE TABLE IF NOT EXISTS shows (
            link TEXT PRIMARY KEY,
            title TEXT,
            cover TEXT,
            provider TEXT,
            library_type INTEGER,
            last_watched_index INTEGER,
            total_episodes INTEGER,
            sort_order INTEGER,
            show_type INTEGER DEFAULT 0,
            progress REAL DEFAULT 0
        )
    )")) {
        logError() << "Library" << "Failed to create table:" << query.lastError().text();
        return;
    }

    query.exec("ALTER TABLE shows ADD COLUMN show_type INTEGER DEFAULT 0");

    auto hasColumn = [this](const QString &table, const QString &column) {
        QSqlQuery cols(m_db);
        if (!cols.exec("PRAGMA table_info(" + table + ")")) return true;
        while (cols.next())
            if (cols.value(1).toString() == column) return true;
        return false;
    };

    if (!hasColumn("shows", "progress"))
        query.exec("ALTER TABLE shows ADD COLUMN progress REAL DEFAULT 0");

    // Watch history is independent of the library so shows you never add still get tracked.
    query.exec(R"(
        CREATE TABLE IF NOT EXISTS history (
            link TEXT PRIMARY KEY,
            title TEXT,
            cover TEXT,
            provider TEXT,
            last_watched_index INTEGER,
            total_episodes INTEGER,
            last_played_at INTEGER DEFAULT 0,
            progress REAL DEFAULT 0
        )
    )");

    if (!hasColumn("history", "progress"))
        query.exec("ALTER TABLE history ADD COLUMN progress REAL DEFAULT 0");

    // Still holding the column is the once-only guard; those values were seconds. Reset only
    // once the drop has cleared it - hasColumn reports the column present when its PRAGMA
    // fails, so an unconditional reset wipes every resume position, on every launch.
    for (const QString &table : {QStringLiteral("shows"), QStringLiteral("history")}) {
        if (!hasColumn(table, "timestamp")) continue;
        if (query.exec("ALTER TABLE " + table + " DROP COLUMN timestamp"))
            query.exec("UPDATE " + table + " SET progress = 0");
        else
            logError() << "Library" << "Could not drop" << table << "timestamp:" << query.lastError().text();
    }

    // Superseded by last_watched_index long ago; nothing has read or written it since.
    if (hasColumn("shows", "watched_index"))
        query.exec("ALTER TABLE shows DROP COLUMN watched_index");

    // finished stored progress against whatever the threshold was that day; derived, it follows it.
    for (const QString &table : {QStringLiteral("shows"), QStringLiteral("history")})
        if (hasColumn(table, "finished"))
            query.exec("ALTER TABLE " + table + " DROP COLUMN finished");

    query.exec("CREATE INDEX IF NOT EXISTS idx_shows_library ON shows(library_type, sort_order)");

    // Kept out of shows/history: those rows are provider-backed, and HistoryPage would try to
    // reopen a file path through a provider that does not exist.
    query.exec(R"(
        CREATE TABLE IF NOT EXISTS local_progress (
            path TEXT PRIMARY KEY,
            folder TEXT NOT NULL,
            progress REAL DEFAULT 0,
            last_played_at INTEGER DEFAULT 0
        )
    )");
    query.exec("CREATE INDEX IF NOT EXISTS idx_local_progress_folder "
               "ON local_progress(folder, last_played_at)");

    pruneDatabase();
}

// rowid, not the key: SQLite allows NULL in a TEXT PRIMARY KEY, and NOT IN over NULL matches
// nothing at all.
void Library::trimToNewest(const char *table, int limit) {
    QSqlQuery query(m_db);
    if (!query.exec(QString("DELETE FROM %1 WHERE rowid NOT IN "
                            "(SELECT rowid FROM %1 ORDER BY last_played_at DESC LIMIT %2)")
                        .arg(QLatin1String(table)).arg(limit)))
        return;
    if (const int removed = query.numRowsAffected(); removed > 0)
        logInfo() << "Library" << "Dropped" << removed << "stale" << table << "rows";
}

void Library::pruneDatabase() {
    trimToNewest("history", kHistoryRows);
    trimToNewest("local_progress", kLocalProgressRows);

    // Deleted pages are reused but never returned, and VACUUM rewrites the whole file - only
    // worth it once the waste is.
    const int freePages  = firstValue(m_db, "PRAGMA freelist_count").toInt();
    const int totalPages = firstValue(m_db, "PRAGMA page_count").toInt();
    if (freePages > 256 && freePages * 2 > totalPages) {
        QSqlQuery vacuum(m_db);
        if (vacuum.exec("VACUUM")) logInfo() << "Library" << "Compacted the database";
    }
}

LibraryEntry Library::entryFromQuery(const QSqlQuery &query) {
    LibraryEntry entry;
    entry.link             = query.value(0).toString();
    entry.title            = query.value(1).toString();
    entry.cover            = query.value(2).toString();
    entry.provider         = query.value(3).toString();
    entry.libraryType      = query.value(4).toInt();
    entry.lastWatchedIndex = query.value(5).toInt();
    entry.totalEpisodes    = query.value(6).toInt();
    entry.showType         = query.value(7).toInt();
    entry.progress         = query.value(8).toDouble();
    entry.valid            = true;
    return entry;
}

void Library::refreshDisplayCache() {
    m_displayCache.clear();
    QSqlQuery query = prepared(m_db, selectEntries("WHERE library_type = ? ORDER BY sort_order"),
                               {m_displayLibraryType});
    if (!runQuery(query, "Failed to load display cache:")) return;
    while (query.next())
        m_displayCache.push_back(entryFromQuery(query));
}

LibraryEntry Library::entryForLink(const QString &link) const {
    QSqlQuery query = prepared(m_db, selectEntries("WHERE link = ?"), {link});
    if (query.exec() && query.next())
        return entryFromQuery(query);
    return {};
}

bool Library::migrate(const QString &oldLink, const QString &newLink, const QString &title,
                             const QString &cover, const QString &provider, int showType,
                             int lastWatchedIndex, int totalEpisodes) {
    if (oldLink.isEmpty() || newLink.isEmpty()) return false;
    if (newLink != oldLink && linkExists(newLink)) return false;

    if (!m_db.transaction()) return false;
    QSqlQuery shows = prepared(m_db, "UPDATE shows SET link=?, title=?, cover=?, provider=?, "
                                     "show_type=?, last_watched_index=?, total_episodes=? WHERE link=?",
                               {newLink, title, cover, provider, showType,
                                lastWatchedIndex, totalEpisodes, oldLink});
    if (!shows.exec() || shows.numRowsAffected() == 0) { m_db.rollback(); return false; }

    QSqlQuery dropStale = prepared(m_db, "DELETE FROM history WHERE link = ?", {newLink});
    if (!dropStale.exec()) { m_db.rollback(); return false; }

    QSqlQuery history = prepared(m_db, "UPDATE history SET link=?, title=?, cover=?, provider=?, "
                                       "last_watched_index=?, total_episodes=? WHERE link=?",
                                 {newLink, title, cover, provider,
                                  lastWatchedIndex, totalEpisodes, oldLink});
    if (!history.exec()) { m_db.rollback(); return false; }
    if (!m_db.commit()) { m_db.rollback(); return false; }

    m_historyMeta.remove(oldLink);
    beginResetModel();
    refreshDisplayCache();
    endResetModel();
    emit libraryChanged();
    emit historyChanged();
    return true;
}

QVariantList Library::history() const {
    QVariantList list;
    QSqlQuery query = prepared(m_db, QString("SELECT link, title, cover, last_watched_index, "
                                             "total_episodes, progress FROM history "
                                             "ORDER BY last_played_at DESC LIMIT %1").arg(kHistoryRows));
    if (!query.exec()) return list;
    while (query.next()) {
        const int lwi   = query.value(3).toInt();
        const int total = query.value(4).toInt();
        // Counting the in-progress episode whole read one episode ahead.
        const double raw = qBound(0.0, query.value(5).toDouble(), 1.0);
        const double watched = raw >= m_watchedFraction ? 1.0 : raw;
        QVariantMap m;
        m["link"]     = query.value(0).toString();
        m["title"]    = query.value(1).toString();
        m["cover"]    = query.value(2).toString();
        m["episode"]  = lwi >= 0 ? lwi + 1 : 0;
        m["total"]    = total;
        m["progress"] = (total > 0 && lwi >= 0) ? qBound(0.0, (lwi + watched) / total, 1.0) : 0.0;
        m["episodeProgress"] = lwi >= 0 ? watched : 0.0;
        list.append(m);
    }
    return list;
}

int Library::indexOf(const QString &link) const {
    if (link.isEmpty()) return -1;
    for (int i = 0; i < m_displayCache.size(); ++i)
        if (m_displayCache[i].link == link)
            return i;
    return -1;
}

bool Library::add(const ShowData& show, int libraryType) {
    QSqlQuery check = prepared(m_db, "SELECT library_type FROM shows WHERE link = ?", {show.link});
    if (!runQuery(check, "DB check error:")) return false;

    if (check.next()) {
        int oldLibraryType = check.value(0).toInt();
        if (oldLibraryType == libraryType) {
            return true;
        }
        changeLibraryType(show.link, libraryType);
    } else {
        auto playlist = show.playlist();
        auto lastWatchedIndex = playlist ? playlist->currentIndex() : -1;
        auto totalEpisodes = playlist ? playlist->count() : 0;
        auto currentItem = (lastWatchedIndex != -1 && playlist) ? playlist->currentItem() : nullptr;
        auto progress = currentItem ? currentItem->progress() : 0.0;

        bool affectsDisplay = (libraryType == m_displayLibraryType);

        const QString providerName = show.provider ? show.provider->name() : QString();
        m_db.transaction();
        QSqlQuery insert = prepared(m_db, R"(
            INSERT INTO shows (link, title, provider, cover, library_type, last_watched_index, progress, total_episodes, show_type, sort_order)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, (
                SELECT IFNULL(MAX(sort_order), -1) + 1 FROM shows WHERE library_type = ?
            ))
        )", {show.link, show.title, providerName, show.coverUrl, libraryType,
             lastWatchedIndex, progress, totalEpisodes, show.type, libraryType});

        if (!insert.exec() || !m_db.commit()) {
            logError() << "Library" << "Failed to insert show to DB:" << insert.lastError().text();
            m_db.rollback();
            return false;
        }
        if (affectsDisplay) {
            LibraryEntry e;
            e.link             = show.link;
            e.title            = show.title;
            e.cover            = show.coverUrl;
            e.provider         = providerName;
            e.libraryType      = libraryType;
            e.lastWatchedIndex = lastWatchedIndex;
            e.progress         = progress;
            e.totalEpisodes    = totalEpisodes;
            e.showType         = show.type;
            e.valid            = true;
            int insertIndex = m_displayCache.size();
            beginInsertRows(QModelIndex(), insertIndex, insertIndex);
            m_displayCache.push_back(e);
            endInsertRows();
        }
        // Search-added shows have no playlist yet - fetch the count so the badge appears now.
        if (totalEpisodes <= 0)
            fetchUnwatchedEpisodes(libraryType, true);
        emit libraryChanged();
    }

    return true;
}

bool Library::linkExists(const QString &link) const {
    return !link.isEmpty()
           && firstValue(m_db, "SELECT 1 FROM shows WHERE link = ? LIMIT 1", {link}).isValid();
}

QString Library::linkAtIndex(int index, int libraryType) const {
    if (libraryType == m_displayLibraryType)
        return (index >= 0 && index < m_displayCache.size()) ? m_displayCache[index].link : QString();
    return firstValue(m_db, "SELECT link FROM shows WHERE library_type = ? "
                            "ORDER BY sort_order, link LIMIT 1 OFFSET ?",
                      {libraryType, index}).toString();
}

int Library::count(int libraryType) const {
    if (libraryType == -1) libraryType = m_displayLibraryType;
    if (libraryType == m_displayLibraryType)
        return m_displayCache.size();
    return firstValue(m_db, "SELECT COUNT(*) FROM shows WHERE library_type = ?",
                      {libraryType}).toInt();
}

void Library::remove(const QString &link) {
    m_db.transaction();
    QSqlQuery query = prepared(m_db, "DELETE FROM shows WHERE link = ?", {link});
    if (!query.exec() || !m_db.commit()) {
        m_db.rollback();
        return;
    }

    // Trust the cache, not the row's claimed type: they disagree after a type change without a refresh.
    if (const int row = indexOf(link); row >= 0) {
        beginRemoveRows(QModelIndex(), row, row);
        m_displayCache.removeAt(row);
        endRemoveRows();
    }
    emit libraryChanged();
}

void Library::removeAt(int index, int libraryType) {
    if (libraryType == -1) libraryType = m_displayLibraryType;
    QString link = linkAtIndex(index, libraryType);
    if (!link.isEmpty())
        remove(link);
}

void Library::move(int from, int to) {
    if (from == to || from < 0 || to < 0) return;
    if (from >= m_displayCache.size() || to >= m_displayCache.size()) return;

    // Persist first: a failed write used to leave the cache reordered and the DB untouched.
    QList<LibraryEntry> reordered = m_displayCache;
    reordered.move(from, to);

    if (!m_db.transaction()) return;
    QSqlQuery update(m_db);
    update.prepare("UPDATE shows SET sort_order = ? WHERE link = ?");
    for (int i = 0; i < reordered.size(); ++i) {
        update.addBindValue(i);
        update.addBindValue(reordered[i].link);
        if (!update.exec()) {
            logError() << "Library" << "Failed to persist move:" << update.lastError().text();
            m_db.rollback();
            return;
        }
    }
    if (!m_db.commit()) {
        logError() << "Library" << "Failed to commit move";
        m_db.rollback();
        return;
    }

    beginMoveRows(QModelIndex(), from, from, QModelIndex(), to + (to > from ? 1 : 0));
    m_displayCache.move(from, to);
    endMoveRows();
}

void Library::updateProgress(const QString &link, int lastWatchedIndex, double progress) {
    const QVariantList binds{lastWatchedIndex, progress, link};
    QSqlQuery query = prepared(m_db, "UPDATE shows SET last_watched_index = ?, progress = ? "
                                     "WHERE link = ?", binds);
    query.exec();

    // last_played_at stays put: this is progress within a play, not a new one.
    QSqlQuery hq = prepared(m_db, "UPDATE history SET last_watched_index = ?, progress = ? "
                                  "WHERE link = ?", binds);
    if (hq.exec() && hq.numRowsAffected() > 0) emit historyChanged();

    int idx = indexOf(link);
    if (idx >= 0) {
        auto &e = m_displayCache[idx];
        e.lastWatchedIndex = lastWatchedIndex;
        e.progress = progress;
        emit dataChanged(index(idx), index(idx));
    }
}

QList<QPair<QString, double>> Library::localFolderProgress(const QString &folder) const {
    QList<QPair<QString, double>> rows;
    if (folder.isEmpty()) return rows;
    // Most recent first, so the caller reads row 0 as the file to reopen on.
    QSqlQuery query = prepared(m_db, "SELECT path, progress FROM local_progress WHERE folder = ? "
                                     "ORDER BY last_played_at DESC, rowid DESC", {folder});
    if (!runQuery(query, "Failed to read local progress:")) return rows;
    while (query.next())
        rows.append({query.value(0).toString(), qBound(0.0, query.value(1).toDouble(), 1.0)});
    return rows;
}

void Library::updateLocalProgress(const QString &path, const QString &folder, double progress) {
    if (path.isEmpty() || folder.isEmpty()) return;
    QSqlQuery query = prepared(m_db,
        "INSERT INTO local_progress (path, folder, progress, last_played_at) "
        "VALUES (?, ?, ?, strftime('%s','now')) "
        "ON CONFLICT(path) DO UPDATE SET folder = excluded.folder, progress = excluded.progress, "
        "                                last_played_at = excluded.last_played_at",
        {path, folder, qBound(0.0, progress, 1.0)});
    runQuery(query, "Failed to save local progress:");
}

void Library::forgetLocalProgress(const QStringList &paths) {
    if (paths.isEmpty()) return;
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM local_progress WHERE path = ?");
    query.addBindValue(QVariantList(paths.cbegin(), paths.cend()));
    if (!query.execBatch()) {
        logError() << "Library" << "Failed to forget local progress:" << query.lastError().text();
        return;
    }
    logInfo() << "Library" << "Forgot" << paths.size() << "deleted files";
}

void Library::cacheHistoryMeta(const QString &link, const QString &title,
                                      const QString &cover, const QString &provider, int total) {
    if (link.isEmpty()) return;
    m_historyMeta[link] = { title, cover, provider, total };
}

void Library::recordHistory(const QString &link, int lastWatchedIndex) {
    // Only shows loaded this session, so clearing history isn't undone by the outgoing show's last save.
    if (!m_historyMeta.contains(link)) return;
    const HistoryMeta meta = m_historyMeta.value(link);
    QSqlQuery q = prepared(m_db,
        "INSERT INTO history "
        "(link, title, cover, provider, last_watched_index, total_episodes, last_played_at) "
        "VALUES (?, ?, ?, ?, ?, ?, strftime('%s','now')) "
        "ON CONFLICT(link) DO UPDATE SET "
        "  title = CASE WHEN excluded.title != '' THEN excluded.title ELSE title END,"
        "  cover = CASE WHEN excluded.cover != '' THEN excluded.cover ELSE cover END,"
        "  provider = CASE WHEN excluded.provider != '' THEN excluded.provider ELSE provider END,"
        "  total_episodes = CASE WHEN excluded.total_episodes > 0 THEN excluded.total_episodes ELSE total_episodes END,"
        "  last_watched_index = excluded.last_watched_index,"
        "  last_played_at = excluded.last_played_at",
        {link, meta.title, meta.cover, meta.provider, lastWatchedIndex, meta.total});
    if (q.exec()) emit historyChanged();
}

void Library::clearHistory() {
    QSqlQuery q(m_db);
    if (!q.exec("DELETE FROM history")) return;
    m_historyMeta.clear();   // otherwise the next progress save re-inserts what was just cleared
    emit historyChanged();
}

void Library::removeFromHistory(const QString &link) {
    if (link.isEmpty()) return;
    QSqlQuery q = prepared(m_db, "DELETE FROM history WHERE link = ?", {link});
    if (!q.exec()) return;
    m_historyMeta.remove(link);
    emit historyChanged();
}

Library::HistoryRow Library::historyEntry(const QString &link) const {
    HistoryRow e;
    QSqlQuery q = prepared(m_db, "SELECT title, cover, provider, last_watched_index, "
                                 "total_episodes, progress FROM history WHERE link = ?", {link});
    if (q.exec() && q.next()) {
        e.link = link;
        e.title = q.value(0).toString();
        e.cover = q.value(1).toString();
        e.provider = q.value(2).toString();
        e.lastWatchedIndex = q.value(3).toInt();
        e.totalEpisodes = q.value(4).toInt();
        e.progress = q.value(5).toDouble();
        e.valid = true;
    }
    return e;
}

void Library::updateShowCover(const QString &link, const QString &cover) {
    QSqlQuery query = prepared(m_db, "UPDATE shows SET cover = ? WHERE link = ?", {cover, link});
    if (!runQuery(query, "Failed to update show cover:")) return;
    int idx = indexOf(link);
    if (idx >= 0) {
        m_displayCache[idx].cover = cover;
        emit dataChanged(index(idx), index(idx));
    }
}

ShowData::WatchState Library::watchState(const QString &showLink) const {
    ShowData::WatchState watch;
    QSqlQuery query = prepared(m_db, "SELECT library_type, last_watched_index, progress "
                                     "FROM shows WHERE link = ?", {showLink});
    if (query.exec() && query.next()) {
        watch.libraryType = query.value(0).toInt();
        watch.lastWatchedIndex = query.value(1).toInt();
        watch.progress = query.value(2).toDouble();
    }
    return watch;
}

LibraryEntry Library::entryAt(int index) const {
    return (index >= 0 && index < m_displayCache.size()) ? m_displayCache[index] : LibraryEntry{};
}

void Library::changeLibraryTypeAt(int index, int newLibraryType, int oldLibraryType) {
    if (oldLibraryType == -1) oldLibraryType = m_displayLibraryType;
    QString link = linkAtIndex(index, oldLibraryType);
    changeLibraryType(link, newLibraryType);
}

void Library::changeLibraryType(const QString &link, int libraryType) {
    if (!linkExists(link)) return;

    int oldLibraryType = libraryTypeOf(link);
    if (oldLibraryType == libraryType) return;

    const int oldIndex = indexOf(link);

    m_db.transaction();
    const QVariant nextSortOrder =
        firstValue(m_db, "SELECT IFNULL(MAX(sort_order), -1) + 1 FROM shows WHERE library_type = ?",
                   {libraryType});
    if (!nextSortOrder.isValid()) {
        logError() << "Library" << "Failed to get next sort_order";
        m_db.rollback();
        return;
    }

    QSqlQuery update = prepared(m_db, "UPDATE shows SET library_type = ?, sort_order = ? "
                                      "WHERE link = ?", {libraryType, nextSortOrder, link});
    if (!update.exec() || !m_db.commit()) {
        logError() << "Library" << "Failed to update libraryType:" << update.lastError().text();
        m_db.rollback();
        return;
    }

    if (oldIndex >= 0) {
        beginRemoveRows(QModelIndex(), oldIndex, oldIndex);
        m_displayCache.removeAt(oldIndex);
        endRemoveRows();
    }
    if (libraryType == m_displayLibraryType) {
        LibraryEntry e = entryForLink(link);
        if (e.valid) {
            int insertIndex = m_displayCache.size();
            beginInsertRows(QModelIndex(), insertIndex, insertIndex);
            m_displayCache.push_back(e);
            endInsertRows();
            if (e.totalEpisodes <= 0)
                fetchUnwatchedEpisodes(libraryType, true);
        }
    }
    emit libraryChanged();
}

int Library::libraryTypeOf(const QString &link) const {
    const QVariant type = firstValue(m_db, "SELECT library_type FROM shows WHERE link = ?", {link});
    return type.isValid() ? type.toInt() : -1;
}

void Library::fetchUnwatchedEpisodes(int libraryType, bool force) {
    if (libraryType < 0 || libraryType > LibraryType::Completed) return;

    if (!force) {
        const qint64 last = m_lastFetchMs.value(libraryType, 0);
        if (last != 0 && QDateTime::currentMSecsSinceEpoch() - last < kFetchDebounceMs)
            return;
    }

    if (m_fetchWatcher.isRunning()) {
        m_cancel.cancel();
        m_pendingFetchLibraryType = libraryType;
        m_pendingFetchForced = force;
        return;
    }
    m_pendingFetchLibraryType = kNoPendingFetch;
    m_cancel.reset();

    QSqlQuery query = prepared(m_db, "SELECT link, provider FROM shows WHERE library_type = ?",
                               {libraryType});
    if (!query.exec()) return;
    QList<QPair<QString, ShowProvider*>> shows;
    while (query.next()) {
        QString providerName = query.value(1).toString();
        ShowProvider *provider = ProviderList::byName(providerName);
        if (!provider) continue;
        shows.emplaceBack(query.value(0).toString(), provider);
    }

    m_fetchWatcher.setFuture(QtConcurrent::run([this, shows, libraryType] {
        constexpr int batchSize = 6;
        QList<QPair<QString,int>> results;
        results.reserve(shows.size());

        for (int start = 0; start < shows.size() && !m_cancel.isCancelled(); start += batchSize) {
            const int end = std::min(static_cast<int>(shows.size()), start + batchSize);
            QList<QFuture<QPair<QString, int>>> jobs;
            jobs.reserve(end - start);
            for (int i = start; i < end; ++i) {
                const auto show = shows[i];
                jobs.push_back(QtConcurrent::run([this, show]() mutable {
                    auto client = Client(m_cancel, false);
                    auto dummyShow = ShowData("", show.first);
                    int totalEpisodes = 0;
                    try {
                        totalEpisodes = show.second->fetchEpisodeCount(&client, dummyShow);
                    } catch (AppException &e) {
                        e.log();
                    } catch (const std::exception &e) {
                        logWarn() << "Library" << show.first << e.what();
                    } catch (...) {
                        logWarn() << "Library" << show.first << "unknown error";
                    }
                    return QPair<QString, int>(show.first, totalEpisodes);
                }));
            }
            for (auto &job : jobs) {
                job.waitForFinished();
                auto result = job.result();
                if (!result.first.isEmpty()) results.append(result);
            }
        }

        QMetaObject::invokeMethod(this, [this, results, libraryType, partial = m_cancel.isCancelled()]() {
            // Stamping a cancelled run would debounce away the refetch that replaced it.
            if (!partial) m_lastFetchMs[libraryType] = QDateTime::currentMSecsSinceEpoch();

            // Drop count-0 results so a transient provider error doesn't wipe a known count.
            QList<QPair<QString, int>> valid;
            valid.reserve(results.size());
            for (const auto &p : std::as_const(results))
                if (p.second > 0) valid.append(p);

            if (valid.isEmpty()) {
                emit fetchedAllEpCounts();
                return;
            }

            persistEpisodeCounts(valid);

            for (const auto &p : std::as_const(valid)) {
                int idx = indexOf(p.first);
                if (idx >= 0 && m_displayCache[idx].totalEpisodes != p.second) {
                    m_displayCache[idx].totalEpisodes = p.second;
                    emit dataChanged(index(idx), index(idx));
                }
            }

            emit fetchedAllEpCounts();
        }, Qt::QueuedConnection);
    }));
}

void Library::persistEpisodeCounts(const QList<QPair<QString, int>> &counts) {
    static const QLatin1String sql("UPDATE shows SET total_episodes = ? WHERE link = ?");
    if (!m_db.transaction()) {
        logError() << "Library" << "Could not open a transaction for episode counts:" << m_db.lastError().text();
        return;
    }

    QVariantList totals, links;
    totals.reserve(counts.size());
    links.reserve(counts.size());
    for (const auto &[link, total] : counts) {
        links.append(link);
        totals.append(total);
    }

    QSqlQuery batch(m_db);
    batch.prepare(sql);
    batch.addBindValue(totals);
    batch.addBindValue(links);

    // Some drivers refuse execBatch; the same rows one at a time reach the identical end state.
    if (!batch.execBatch()) {
        logError() << "Library" << "execBatch failed, updating one row at a time:" << batch.lastError().text();
        QSqlQuery single(m_db);
        single.prepare(sql);
        for (const auto &[link, total] : counts) {
            single.bindValue(0, total);
            single.bindValue(1, link);
            if (!single.exec())
                logError() << "Library" << "Could not set total_episodes for" << link << ":" << single.lastError().text();
        }
    }

    if (!m_db.commit()) {
        logError() << "Library" << "Could not commit episode counts:" << m_db.lastError().text();
        m_db.rollback();
    }
}

void Library::setDisplayLibraryType(int newLibraryType) {
    // Written through a QML property, and an out-of-range one would just show an empty library.
    newLibraryType = qBound<int>(Watching, newLibraryType, Completed);
    if (m_displayLibraryType != newLibraryType) {
        beginResetModel();
        m_displayLibraryType = newLibraryType;
        refreshDisplayCache();
        endResetModel();
        emit libraryTypeChanged();
        fetchUnwatchedEpisodes(newLibraryType);
    }
}
