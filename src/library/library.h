#pragma once
#include <QAbstractListModel>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QtConcurrent/QtConcurrentRun>
#include <QFutureWatcher>
#include <QVariantList>
#include <QHash>
#include "net/canceltoken.h"
#include "providers/showdata.h"
#include <qqmlintegration.h>

struct LibraryEntry {
    Q_GADGET
    QML_ANONYMOUS
    Q_PROPERTY(QString title MEMBER title CONSTANT)
    Q_PROPERTY(QString link MEMBER link CONSTANT)
    Q_PROPERTY(QString cover MEMBER cover CONSTANT)
    Q_PROPERTY(QString provider MEMBER provider CONSTANT)
    Q_PROPERTY(int libraryType MEMBER libraryType CONSTANT)
    Q_PROPERTY(int lastWatchedIndex MEMBER lastWatchedIndex CONSTANT)
    Q_PROPERTY(double progress MEMBER progress CONSTANT)
    Q_PROPERTY(int totalEpisodes MEMBER totalEpisodes CONSTANT)
    Q_PROPERTY(bool valid MEMBER valid CONSTANT)
public:
    QString title;
    QString link;
    QString cover;
    QString provider;
    int libraryType = -1;
    int lastWatchedIndex = -1;
    double progress = 0.0;       // how far into that episode, 0..1
    int totalEpisodes = 0;
    int showType = 0;            // ShowData::ShowType
    bool valid = false;
};

class Library : public QAbstractListModel
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(int libraryType READ displayLibraryType WRITE setDisplayLibraryType NOTIFY libraryTypeChanged)
    Q_PROPERTY(QStringList typeNames READ typeNames CONSTANT)
public:
    enum LibraryType { Watching, Planned, Paused, Dropped, Completed };
    enum Role { Title = Qt::UserRole, Cover, UnwatchedEpisodes, ShowType, Provider };

    static QStringList typeNames() {
        return {QStringLiteral("Watching"), QStringLiteral("Planned"), QStringLiteral("Paused"),
                QStringLiteral("Dropped"),  QStringLiteral("Completed")};
    }

    explicit Library(QObject *parent = nullptr);
    ~Library();

    Q_INVOKABLE int  count(int libraryType = -1) const;
    Q_INVOKABLE int  libraryTypeOf(const QString &link) const;
    bool linkExists(const QString &link) const;
    ShowData::WatchState watchState(const QString &showLink) const;

    Q_INVOKABLE LibraryEntry entryAt(int index) const;
    LibraryEntry entryForLink(const QString &link) const;

    bool migrate(const QString &oldLink, const QString &newLink, const QString &title,
                 const QString &cover, const QString &provider, int showType,
                 int lastWatchedIndex, int totalEpisodes);

    Q_INVOKABLE QVariantList history() const;
    Q_INVOKABLE void clearHistory();
    Q_INVOKABLE void removeFromHistory(const QString &link);

    // Stamped on episode start, not on position saves, or clearing history undoes itself.
    void recordHistory(const QString &link, int lastWatchedIndex);

    // Cover and title only exist in ShowData, so cache them at load - shows outside the library get tracked too.
    void cacheHistoryMeta(const QString &link, const QString &title, const QString &cover,
                          const QString &provider, int total);

    struct HistoryRow {
        QString link, title, cover, provider;
        int lastWatchedIndex = -1, totalEpisodes = 0;
        double progress = 0.0;
        bool valid = false;
    };
    HistoryRow historyEntry(const QString &link) const;

    // Resume points for local files, keyed by absolute path. Rows come back most recent first,
    // so row 0 is the file a folder should reopen on.
    QList<QPair<QString, double>> localFolderProgress(const QString &folder) const;
    void updateLocalProgress(const QString &path, const QString &folder, double progress);

    Q_INVOKABLE bool add(const ShowData &show, int libraryType);
    Q_INVOKABLE void removeAt(int index, int libraryType = -1);
    Q_INVOKABLE void remove(const QString &link);
    Q_INVOKABLE void move(int from, int to);
    Q_INVOKABLE void changeLibraryTypeAt(int index, int newLibraryType, int oldLibraryType = -1);
    void changeLibraryType(const QString &link, int newLibraryType);
    Q_INVOKABLE void cycleDisplayLibraryType() { setDisplayLibraryType((m_displayLibraryType + 1) % (Completed + 1)); }

    Q_INVOKABLE void updateProgress(const QString &link, int lastWatchedIndex, double progress);
    void updateShowCover(const QString &link, const QString &cover);

    Q_INVOKABLE void fetchUnwatchedEpisodes(int libraryType, bool force = false);

    int displayLibraryType() const { return m_displayLibraryType; }
    void setDisplayLibraryType(int newLibraryType);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void fetchedAllEpCounts();
    void libraryTypeChanged();
    void libraryChanged();
    void historyChanged();

private:
    void initDatabase();
    void persistEpisodeCounts(const QList<QPair<QString, int>> &counts);
    int indexOf(const QString &link) const;
    QString linkAtIndex(int index, int libraryType) const;

    void refreshDisplayCache();
    static LibraryEntry entryFromQuery(const QSqlQuery &query);
    QList<LibraryEntry> m_displayCache;

    static constexpr int kNoPendingFetch = -2;
    static constexpr qint64 kFetchDebounceMs = 60'000;

    QSqlDatabase m_db;
    int m_displayLibraryType = Watching;
    QFutureWatcher<void> m_fetchWatcher;
    int  m_pendingFetchLibraryType = kNoPendingFetch;
    bool m_pendingFetchForced = false;
    QHash<int, qint64> m_lastFetchMs;   // library type -> epoch ms of last completed fetch

    struct HistoryMeta { QString title, cover, provider; int total = 0; };
    QHash<QString, HistoryMeta> m_historyMeta;   // link -> display metadata, populated at show load
    double m_watchedFraction = 0.8;
    CancelToken m_cancel;
};
