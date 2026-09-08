#pragma once
#include <QAbstractListModel>
#include <QFutureWatcher>
#include <qqmlintegration.h>
#include "net/canceltoken.h"
#include <QUrl>

class Client;
class MpvPlayer;

namespace SubDl {

// SubDL wraps files in a release even when there is only one, so results are flattened.
struct Result {
    QString fileId;        // stable per file; doubles as the cache key
    QString name;
    QString releaseName;
    QString language;
    QString author;
    QUrl    url;
    int     season = 0;
    int     episode = 0;
    qint64  size = 0;      // SubDL reports it exactly; used to spot a half-written cache entry
    bool    hearingImpaired = false;
};

// Key and languages passed in: this runs on a worker thread, which must not touch QSettings.
QList<Result> search(Client *client, const QString &query,
                     const QString &apiKey, const QString &languages);

QString fetch(Client *client, const Result &result);

// Deterministic, so an already-downloaded result is recognisable.
QString cachePath(const QString &fileId);

}

class SubtitleSearch : public QAbstractListModel
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(bool    isLoading READ isLoading NOTIFY isLoadingChanged)
    Q_PROPERTY(int     count     READ count     NOTIFY countChanged)
    Q_PROPERTY(QString query     READ query     NOTIFY queryChanged)
public:
    enum { DisplayNameRole = Qt::UserRole, ReleaseRole, LanguageRole, AuthorRole,
           EpisodeRole, HiRole, TagsRole, SlotRole, FetchingRole };

    explicit SubtitleSearch(QObject *parent = nullptr);
    ~SubtitleSearch();

    Q_INVOKABLE void search(const QString &query);
    Q_INVOKABLE void searchIfNew(const QString &query);
    Q_INVOKABLE void use(int row, bool secondary = false);
    Q_INVOKABLE void cancel();

    int  count() const { return m_rows.size(); }
    bool isLoading() const { return m_searchWatcher.isRunning(); }
    QString query() const { return m_query; }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_SIGNAL void isLoadingChanged();
    Q_SIGNAL void countChanged();
    Q_SIGNAL void queryChanged();

private:
    struct Row {
        SubDl::Result result;
        QString       displayName;
        QStringList   tags;
        QString       localPath;      // set once downloaded
        int           slot = 0;       // 0 none, 1 primary, 2 secondary
        bool          fetching = false;
    };

    void setResults(const QList<SubDl::Result> &results);
    int  slotFor(const QString &localPath);
    void refreshSlots();
    int  rowForFileId(const QString &fileId) const;
    // MpvPlayer is built by QML, so it does not exist yet when this is constructed.
    MpvPlayer *mpv();

    CancelToken                          m_cancel;
    QList<Row>                           m_rows;
    QString                              m_query;
    QString                              m_searchedQuery;   // last query that finished
    QFutureWatcher<QList<SubDl::Result>> m_searchWatcher;
    QFutureWatcher<void>                 m_fetchWatcher;
    bool                                 m_mpvConnected = false;
};
