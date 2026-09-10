#include "ui/searchresults.h"
#include "providers/showprovider.h"
#include "ui/appshell.h"
#include "app/logger.h"
#include <QtConcurrent/QtConcurrentRun>
#include "app/exception.h"

SearchResults::SearchResults(QObject *parent)
    : QAbstractListModel(parent)
{
    connect(&m_watcher, &QFutureWatcher<QList<ShowData>>::finished,
            this, &SearchResults::onSearchFinished);
    connect(&m_watcher, &QFutureWatcher<QList<ShowData>>::started,
            this, &SearchResults::isLoadingChanged);
    connect(&m_watcher, &QFutureWatcher<QList<ShowData>>::finished,
            this, &SearchResults::isLoadingChanged);
}

int SearchResults::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_list.count();
}

QVariant SearchResults::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_list.size()) return {};
    const ShowData &show = m_list.at(index.row());
    switch (role) {
    case TitleRole:     return show.title;
    case CoverRole:     return show.coverUrl;
    case LinkRole:      return show.link;
    case LatestTxtRole: return show.latestTxt;
    default:            return {};
    }
}

QHash<int, QByteArray> SearchResults::roleNames() const {
    return {
        {TitleRole,     "title"},
        {CoverRole,     "cover"},
        {LinkRole,      "link"},
        {LatestTxtRole, "latestTxt"},
    };
}

void SearchResults::onSearchFinished() {
    bool wasCancelled = m_cancel.isCancelled();
    m_cancel.reset();

    if (!m_watcher.future().isValid() || wasCancelled)
        return;

    QList<ShowData> results;
    try {
        results = m_watcher.result();
    } catch (const AppException &ex) {
        ex.report();
        return;
    } catch (const std::exception &ex) {
        AppShell::instance().reportError(ex.what(), "Explorer Error");
        return;
    } catch (...) {
        AppShell::instance().reportError("Something went wrong", "Explorer Error");
        return;
    }

    m_hasMore = !results.isEmpty();
    // Page 1 with nothing found must still clear the previous results, or they look like a hit.
    if (!m_hasMore) {
        if (m_currentPage <= 1 && !m_list.isEmpty()) {
            beginResetModel();
            m_list.clear();
            endResetModel();
            emit countChanged(0);
        }
        return;
    }

    if (m_currentPage > 1) {
        int first = m_list.count();
        int last = first + results.count() - 1;
        beginInsertRows(QModelIndex(), first, last);
        m_list.reserve(first + results.count());
        m_list.append(std::move(results));
        endInsertRows();
    } else {
        beginResetModel();
        m_list = std::move(results);
        endResetModel();
    }
    emit countChanged(m_list.count());
}

void SearchResults::runSearch(int page, SearchFunc &&func) {
    if (m_watcher.isRunning()) return;
    m_currentPage = page;
    m_lastSearch = std::move(func);
    m_watcher.setFuture(QtConcurrent::run(m_lastSearch));
}

void SearchResults::search(const QString &query, int page, int type, ShowProvider *provider) {
    runSearch(page, [this, query, type, provider]() {
        Client client(m_cancel);
        return provider->search(&client, query, m_currentPage, type);
    });
}

void SearchResults::latest(int page, int type, ShowProvider *provider) {
    runSearch(page, [this, type, provider]() {
        Client client(m_cancel);
        return provider->latest(&client, m_currentPage, type);
    });
}

void SearchResults::popular(int page, int type, ShowProvider *provider) {
    runSearch(page, [this, type, provider]() {
        Client client(m_cancel);
        return provider->popular(&client, m_currentPage, type);
    });
}

void SearchResults::cancel() {
    if (m_watcher.isRunning()) {
        logWarn() << "Search" << "Cancelling operation";
        m_cancel.cancel();
    }
}

void SearchResults::fetchMore() {
    if (!canFetchMore()) return;
    m_cancel.reset();
    ++m_currentPage;
    m_watcher.setFuture(QtConcurrent::run(m_lastSearch));
}

void SearchResults::reload() {
    if (m_watcher.isRunning() || !m_lastSearch) return;
    m_cancel.reset();
    m_currentPage = 1;   // pages after 1 append, so reloading from page N would duplicate them
    m_watcher.setFuture(QtConcurrent::run(m_lastSearch));
}
