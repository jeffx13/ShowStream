#include "ui/episodelistmodel.h"
#include "media/playlistitem.h"
#include <cmath>

void EpisodeListModel::setPlaylist(const QSharedPointer<PlaylistItem> &playlist) {
    beginResetModel();
    m_playlist = playlist;
    rebuildFilteredIndices();
    endResetModel();
}

void EpisodeListModel::setReversed(bool isReversed) {
    if (m_isReversed == isReversed) return;
    beginResetModel();
    m_isReversed = isReversed;
    endResetModel();
    emit reversedChanged();
}

void EpisodeListModel::setFilterText(const QString &text) {
    if (m_filterText == text) return;
    beginResetModel();
    m_filterText = text;
    rebuildFilteredIndices();
    endResetModel();
    emit filterTextChanged();
}

void EpisodeListModel::rebuildFilteredIndices() {
    m_filteredIndices.clear();
    if (m_filterText.isEmpty()) return;
    auto playlist = m_playlist.toStrongRef();
    if (!playlist) return;
    const int count = playlist->count();
    for (int i = 0; i < count; i++) {
        auto ep = playlist->at(i);
        if (!ep) continue;
        const double n = ep->number;
        const QString numStr = (std::floor(n) == n)
            ? QString::number(static_cast<long long>(n))
            : QString::number(n);
        if (ep->name.contains(m_filterText, Qt::CaseInsensitive) ||
            numStr.contains(m_filterText))
            m_filteredIndices.append(i);
    }
}

int EpisodeListModel::visibleCount() const {
    if (!m_filterText.isEmpty()) return m_filteredIndices.size();
    auto playlist = m_playlist.toStrongRef();
    return playlist ? playlist->count() : 0;
}

int EpisodeListModel::sourceIndex(int visibleRow) const {
    const int total = visibleCount();
    const int row = m_isReversed ? total - 1 - visibleRow : visibleRow;
    if (row < 0 || row >= total) return -1;
    return m_filterText.isEmpty() ? row : m_filteredIndices.at(row);
}

int EpisodeListModel::visibleIndex(int sourceIdx) const {
    if (sourceIdx < 0) return -1;
    const int total = visibleCount();
    int row = sourceIdx;
    if (!m_filterText.isEmpty())
        row = m_filteredIndices.indexOf(sourceIdx);
    if (row < 0 || row >= total) return -1;
    return m_isReversed ? total - 1 - row : row;
}

int EpisodeListModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : visibleCount();
}

QVariant EpisodeListModel::data(const QModelIndex &index, int role) const {
    auto playlist = m_playlist.toStrongRef();
    if (!playlist) return {};
    auto episode = playlist->at(sourceIndex(index.row()));
    if (!episode) return {};

    switch (role) {
    case TitleRole:         return episode->name;
    case EpisodeNumberRole: return episode->number;
    case SeasonNumberRole:  return episode->season;
    default:                return {};
    }
}

QHash<int, QByteArray> EpisodeListModel::roleNames() const {
    return {
            {TitleRole, "title"},
            {EpisodeNumberRole, "episodeNumber"},
            {SeasonNumberRole, "seasonNumber"},
            };
}
