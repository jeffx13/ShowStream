#pragma once
#include <QAbstractListModel>
#include <QHash>
#include <algorithm>
#include "media/playinfo.h"
#include <qqmlintegration.h>

// Two keys per track: Index (position here) and ID (mpv's gappy track id).
class TrackListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(int secondaryIndex READ secondaryIndex WRITE setSecondaryIndex NOTIFY secondaryIndexChanged)
    Q_PROPERTY(int count        READ count                                NOTIFY countChanged)
public:
    Track *at(int index) {
        return isValidIndex(index) ? &m_rows[index].track : nullptr;
    }
    const Track *at(int index) const {
        return isValidIndex(index) ? &m_rows[index].track : nullptr;
    }

    int count() const { return m_rows.size(); }
    bool isValidIndex(int index) const { return index >= 0 && index < m_rows.size(); }

    int currentIndex() const { return m_currentIndex; }

    int secondaryIndex() const { return m_secondaryIndex; }
    void setSecondaryIndex(int index) {
        if (index == m_secondaryIndex) return;
        if (index >= 0 && !isValidIndex(index)) return;
        m_secondaryIndex = index;
        emit secondaryIndexChanged();
    }
    void setCurrentIndex(int index) {
        if (index == m_currentIndex) return;
        if (index >= 0 && !isValidIndex(index)) return;   // -1 clears the selection
        m_currentIndex = index;
        emit currentIndexChanged();
    }
    void setCurrentId(int64_t id) {
        m_currentId = id;
        int idx = indexForId(id);
        if (idx >= 0) setCurrentIndex(idx);
    }

    [[nodiscard]] int64_t idForIndex(int index) const { return isValidIndex(index) ? m_rows[index].id : -1; }
    [[nodiscard]] int indexForId(int64_t id) const { return m_indexById.value(id, -1); }

    void setId(const QUrl &url, int64_t id) {
        int idx = indexOf(url);
        if (idx < 0) return;
        const int64_t oldId = m_rows[idx].id;
        if (oldId != -1 && oldId != id) m_indexById.remove(oldId);
        m_rows[idx].id = id;
        m_indexById[id] = idx;
        if (id == m_currentId) setCurrentIndex(idx);
    }

    // A label we built from stream properties can be refreshed as mpv learns more; a title from
    // the container or the provider is final and must never be overwritten.
    bool hasFinalTitle(int64_t id) const {
        const Row *row = rowForId(id);
        return row && !row->track.title.isEmpty() && !row->derived;
    }

    bool isDerivedTitle(int64_t id) const {
        const Row *row = rowForId(id);
        return row && row->derived;
    }

    // False if the url is already present.
    bool append(const QUrl &url, const QString &title, const QString &lang = "", int height = 0) {
        if (indexOf(url) >= 0) return false;
        const int row = m_rows.size();
        beginInsertRows(QModelIndex(), row, row);
        Track track(url, title, lang);
        track.height = height;
        const int64_t provisionalId = row + 1;   // overwritten by setId when mpv reports the real one
        m_rows.append({track, provisionalId, title.isEmpty()});
        m_indexById[provisionalId] = row;
        m_indexByUrl[url] = row;
        endInsertRows();
        emit countChanged();
        if (provisionalId == m_currentId) setCurrentIndex(row);
        return true;
    }

    void append(int64_t id, const QString &title, const QString &lang = "", bool derived = false) {
        const int row = m_rows.size();
        beginInsertRows(QModelIndex(), row, row);
        m_rows.append({Track(QUrl(), title, lang), id, derived});
        m_indexById[id] = row;
        endInsertRows();
        emit countChanged();
        if (id == m_currentId) setCurrentIndex(row);
    }

    void updateById(int64_t id, const QString &title, bool derived = false) {
        int idx = indexForId(id);
        if (idx < 0) return;
        Row &row = m_rows[idx];
        // track-list fires repeatedly; without this the view churns once a second.
        if (row.track.title == title && row.derived == derived) return;
        row.derived = derived;
        row.track.title = title;
        const auto modelIdx = index(idx);
        emit dataChanged(modelIdx, modelIdx);
    }

    void setStats(int64_t id, int height, double fps, int bitrate) {
        int idx = indexForId(id);
        if (idx < 0) return;
        Track &track = m_rows[idx].track;
        track.height  = height;
        track.fps     = fps;
        track.bitrate = bitrate;
    }

    void sortByQuality(bool video) {
        if (m_rows.size() < 2) return;
        QList<Row> sorted = m_rows;
        std::stable_sort(sorted.begin(), sorted.end(), [video](const Row &a, const Row &b) {
            if (video) {
                if (a.track.height != b.track.height) return a.track.height > b.track.height;
                if (a.track.fps    != b.track.fps)    return a.track.fps    > b.track.fps;
            }
            return a.track.bitrate > b.track.bitrate;
        });
        bool changed = false;
        for (int i = 0; i < sorted.size() && !changed; ++i)
            changed = sorted.at(i).id != m_rows.at(i).id;
        if (!changed) return;

        const int64_t currentId   = idForIndex(m_currentIndex);
        const int64_t secondaryId = idForIndex(m_secondaryIndex);
        beginResetModel();
        m_rows = std::move(sorted);
        reindex();
        m_currentIndex = currentId >= 0 ? m_indexById.value(currentId, m_currentIndex) : m_currentIndex;
        // The secondary slot has to follow the rows too, or its highlight points at a stale one.
        m_secondaryIndex = secondaryId >= 0 ? m_indexById.value(secondaryId, m_secondaryIndex) : m_secondaryIndex;
        endResetModel();
        emit currentIndexChanged();
        emit secondaryIndexChanged();
    }

    QList<int> heights() const {
        QList<int> out;
        out.reserve(m_rows.size());
        for (const Row &row : m_rows) out.append(row.track.height);
        return out;
    }

    void clear() {
        beginResetModel();
        m_rows.clear();
        m_indexById.clear();
        m_indexByUrl.clear();
        m_currentIndex = -1;
        m_secondaryIndex = -1;
        m_currentId = -1;
        endResetModel();
        emit currentIndexChanged();
        emit secondaryIndexChanged();
        emit countChanged();
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override {
        return parent.isValid() ? 0 : m_rows.size();
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override {
        if (role != Qt::DisplayRole || !isValidIndex(index.row())) return {};
        return m_rows.at(index.row()).track.title;
    }

    QHash<int, QByteArray> roleNames() const override {
        return {{Qt::DisplayRole, "name"}};
    }

    int indexOf(const QUrl &url) const {
        return url.isEmpty() ? -1 : m_indexByUrl.value(url, -1);
    }

signals:
    void currentIndexChanged();
    void secondaryIndexChanged();
    void countChanged();

private:
    struct Row {
        Track   track;
        int64_t id = -1;
        bool    derived = false;
    };

    const Row *rowForId(int64_t id) const {
        const int idx = indexForId(id);
        return idx >= 0 ? &m_rows[idx] : nullptr;
    }

    void reindex() {
        m_indexById.clear();
        m_indexByUrl.clear();
        for (int i = 0; i < m_rows.size(); ++i) {
            m_indexById[m_rows[i].id] = i;
            if (!m_rows[i].track.url.isEmpty()) m_indexByUrl[m_rows[i].track.url] = i;
        }
    }

    QList<Row> m_rows;
    QHash<int64_t, int> m_indexById;
    QHash<QUrl, int>    m_indexByUrl;
    int m_currentIndex = -1;
    int m_secondaryIndex = -1;
    int64_t m_currentId = -1;   // desired selection; its track may be added after mpv reports it
};
