#pragma once
#include <QAbstractListModel>
#include <QFutureWatcher>
#include <QVariantMap>
#include "app/async.h"
#include "net/client.h"
#include "providers/showdata.h"
#include "net/canceltoken.h"
#include <qqmlintegration.h>

class SearchResults : public QAbstractListModel
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(bool isLoading READ isLoading NOTIFY isLoadingChanged)
    Q_PROPERTY(int  count     READ count     NOTIFY countChanged)
public:
    enum { TitleRole = Qt::UserRole, CoverRole, LinkRole, LatestTxtRole };

    explicit SearchResults(QObject *parent = nullptr);
    ~SearchResults() {
        m_cancel.cancel();
        waitFor(m_watcher, "SearchResults search");
    }

    void search(const QString &query, int page, int type, ShowProvider *provider);
    void latest(int page, int type, ShowProvider *provider);
    void popular(int page, int type, ShowProvider *provider);

    bool isLoading() const { return m_watcher.isRunning(); }

    // QML-facing manual pagination (Qt Quick views don't auto-call fetchMore).
    Q_INVOKABLE bool canFetchMore() const { return !m_watcher.isRunning() && m_hasMore; }
    Q_INVOKABLE void fetchMore();
    Q_INVOKABLE void reload();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void reset() { beginResetModel(); endResetModel(); }

    // An out-of-range index yields a show with no provider, which every caller already checks for.
    ShowData resultAt(int index) const { return m_list.value(index); }
    int count() const { return m_list.count(); }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_SIGNAL void countChanged(int count);
    Q_SIGNAL void isLoadingChanged();

private:
    using SearchFunc = std::function<QList<ShowData>()>;
    void runSearch(int page, SearchFunc &&func);
    void onSearchFinished();

    CancelToken m_cancel;
    QFutureWatcher<QList<ShowData>> m_watcher;
    QList<ShowData> m_list;
    SearchFunc m_lastSearch;
    bool m_hasMore = false;
    int m_currentPage = 1;
};
