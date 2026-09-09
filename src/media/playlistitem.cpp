#include "media/playlistitem.h"
#include <QtGlobal>
#include <QFileInfo>
#include <cmath>
#include <algorithm>

PlaylistItem::PlaylistItem(const QString& name, ShowProvider* provider, const QString &link)
    : name(name), link(link), type(List), m_provider(provider) {}

PlaylistItem::PlaylistItem(int seasonNumber, float number, const QString &link, const QString &name,
                           QSharedPointer<PlaylistItem> parent, bool isLocal, bool preview)
    : name(name), link(link), season(seasonNumber), number(number),
    type(isLocal ? Local : Online), preview(preview), m_parent(parent)
{
    if (number > -1) {
        bool isInt = floorf(number) == number;
        QString seasonStr = seasonNumber != 0 ? QString("S%1").arg(seasonNumber, 2, 10, QChar('0')) : "";
        displayName = seasonStr
                      + (isInt ? QString("E%1").arg(int(number), 2, 10, QChar('0'))
                               : QString::number(number, 'f', 1));
        if (!name.isEmpty())
            displayName += QString("\n%1").arg(name);
    } else {
        displayName = name.isEmpty() ? "[Unnamed Episode]" : name;
    }
    m_row = -1;
}

PlaylistItem::~PlaylistItem() {
    clear();
}

void PlaylistItem::emplaceBack(int season, float number, const QString &link, const QString &name, bool isLocal, bool preview) {
    m_children.push_back(QSharedPointer<PlaylistItem>::create(season, number, link, name, sharedFromThis(), isLocal, preview));
    updateRowIndices(m_children.size() - 1);
}

void PlaylistItem::append(QSharedPointer<PlaylistItem> value) {
    if (!value) return;
    value->m_parent = sharedFromThis();
    m_children.push_back(value);
    updateRowIndices(m_children.size() - 1);
}

void PlaylistItem::insert(int index, QSharedPointer<PlaylistItem> value) {
    if (!value) return;
    if (index >= 0 && index <= count()) {
        value->m_parent = sharedFromThis();
        m_children.insert(index, value);
        updateRowIndices(index);
    }
}

void PlaylistItem::removeAt(int index) {
    if (!isValidIndex(index)) return;
    if (index == m_currentIndex)       m_currentIndex = -1;
    else if (index < m_currentIndex)   m_currentIndex--;   // keep pointing at the same item
    m_children[index]->m_parent.clear();
    m_children.removeAt(index);
    updateRowIndices(index);
}

void PlaylistItem::removeOne(const QSharedPointer<PlaylistItem> &value) {
    if (value) removeAt(indexOf(value));
}

void PlaylistItem::clear() {
    for (const auto &child : std::as_const(m_children)) {
        child->m_parent.clear();
        child->m_row = -1;
    }
    m_children.clear();
}

void PlaylistItem::sort() {
    if (m_children.isEmpty()) return;
    auto current = currentItem();
    std::stable_sort(m_children.begin(), m_children.end(),
                     [](const QSharedPointer<PlaylistItem>& a, const QSharedPointer<PlaylistItem>& b) {
                         if (a->isList() && !b->isList()) return true;
                         if (!a->isList() && b->isList()) return false;
                         if (!a->isList() && !b->isList()) {
                             if (a->season == b->season) return a->number < b->number;
                             return a->season < b->season;
                         }
                         return a->name < b->name;
                     });
    if (current)
        setCurrentIndex(indexOf(current));
    updateRowIndices(0);
}

int PlaylistItem::indexOf(const QString &link) const {
    for (int i = 0; i < m_children.size(); ++i)
        if (m_children[i]->link == link) return i;
    return -1;
}

bool PlaylistItem::isValidIndex(int index) const {
    if (m_children.isEmpty()) return false;
    return index >= 0 && index < m_children.size();
}

bool PlaylistItem::setCurrentIndex(int index) {
    if (index != -1 && !isValidIndex(index)) return false;
    m_currentIndex = index;
    return true;
}

void PlaylistItem::setProgress(double fraction) {
    if (type & List) return;
    m_progress = qBound(0.0, fraction, 1.0);
}

double PlaylistItem::progress() const {
    return (type & List) ? 0.0 : m_progress;
}


void PlaylistItem::updateRowIndices(int startIndex) {
    for (int i = startIndex; i < m_children.size(); ++i) {
        m_children[i]->m_row = i;
    }
}
