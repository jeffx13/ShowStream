#include "ui/serverlistmodel.h"
#include "providers/showprovider.h"
#include "app/logger.h"
#include <QThread>
#include <algorithm>

void ServerListModel::setServers(const QList<VideoServer> &servers, ShowProvider *provider) {
    beginResetModel();
    m_servers = servers;
    m_provider = provider;
    m_sourceCache.clear();
    m_brokenServers.clear();
    m_currentIndex = -1;
    std::stable_sort(m_servers.begin(), m_servers.end(),
                     [](const VideoServer &a, const VideoServer &b) {
                         if (a.translation != b.translation) return a.translation < b.translation;
                         // Best first - alphabetical would put 360p above 720p.
                         if (a.resolution() != b.resolution()) return a.resolution() > b.resolution();
                         return a.name < b.name;
                     });
    endResetModel();
    emit countChanged();
    emit currentIndexChanged();
}

void ServerListModel::setCurrentIndex(int index) {
    if (index == m_currentIndex || !isValidIndex(index)) return;
    m_currentIndex = index;
    emit currentIndexChanged();
}

void ServerListModel::setCurrentServer(const QString &name) {
    for (int i = 0; i < m_servers.size(); ++i)
        if (m_servers[i].name == name) { setCurrentIndex(i); break; }
    if (m_provider && !name.isEmpty()) m_provider->setPreferredServer(name);
}

void ServerListModel::resort() {
    const QString currentName = isValidIndex(m_currentIndex) ? m_servers[m_currentIndex].name : QString();
    beginResetModel();
    std::stable_sort(m_servers.begin(), m_servers.end(),
                     [this](const VideoServer &a, const VideoServer &b) {
                         const bool ab = m_brokenServers.contains(a.name);
                         const bool bb = m_brokenServers.contains(b.name);
                         if (ab != bb) return !ab;                       // working before broken
                         if (a.translation != b.translation) return a.translation < b.translation;
                         if (a.resolution() != b.resolution()) return a.resolution() > b.resolution();
                         return a.name < b.name;
                     });
    m_currentIndex = -1;
    for (int i = 0; i < m_servers.size(); ++i)
        if (m_servers[i].name == currentName) { m_currentIndex = i; break; }
    endResetModel();
    emit currentIndexChanged();
}

void ServerListModel::setPreferredServer(int index) {
    if (!m_provider || !isValidIndex(index)) return;
    m_provider->setPreferredServer(m_servers[index].name);
}

VideoServer &ServerListModel::at(int index) {
    Q_ASSERT(isValidIndex(index));
    return m_servers[index];
}

bool ServerListModel::hasDub() const {
    for (const auto &s : m_servers)
        if (s.translation == VideoServer::Dub) return true;
    return false;
}

void ServerListModel::clear() {
    beginResetModel();
    m_servers.clear();
    m_currentIndex = -1;
    m_provider = nullptr;
    m_sourceCache.clear();
    m_brokenServers.clear();
    endResetModel();
    emit countChanged();
    emit currentIndexChanged();
}

void ServerListModel::setCachedSources(QHash<QString, PlayInfo> &&cache) {
    m_sourceCache = std::move(cache);
    for (auto it = m_sourceCache.keyBegin(); it != m_sourceCache.keyEnd(); ++it)
        m_brokenServers.remove(*it);
    if (!m_servers.isEmpty())
        emit dataChanged(index(0), index(m_servers.size() - 1), {StatusRole});
}

void ServerListModel::cacheSource(const QString &name, PlayInfo info) {
    m_sourceCache.insert(name, std::move(info));
    const bool wasBroken = m_brokenServers.remove(name);
    if (wasBroken) resort();          // recovered -> move back up out of the Broken section
    else emitStatusChanged(name);
}

void ServerListModel::markBroken(const QString &name) {
    if (m_sourceCache.contains(name)) return;   // a working source wins over a stale failure
    if (!m_brokenServers.contains(name)) {
        m_brokenServers.insert(name);
        resort();
    }
}

void ServerListModel::emitStatusChanged(const QString &name) {
    for (int i = 0; i < m_servers.size(); ++i)
        if (m_servers[i].name == name) {
            emit dataChanged(index(i), index(i), {StatusRole});
            return;
        }
}

bool ServerListModel::isValidIndex(int index) const {
    return index >= 0 && index < m_servers.size();
}

int ServerListModel::rowCount(const QModelIndex &parent) const {
    Q_UNUSED(parent);
    return count();
}

QVariant ServerListModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || !isValidIndex(index.row())) return QVariant();
    const auto &server = m_servers.at(index.row());
    switch (role) {
    case NameRole:
    case Qt::DisplayRole:
        return server.name;
    case LinkRole:
        return server.link;
    case StatusRole:
        if (m_sourceCache.contains(server.name)) return Working;
        if (m_brokenServers.contains(server.name)) return Broken;
        return Unchecked;
    case TranslationRole:
        return static_cast<int>(server.translation);
    case SectionRole:
        if (m_brokenServers.contains(server.name)) return QStringLiteral("Broken");
        if (!hasDub()) return QString();
        return server.translation == VideoServer::Dub ? QStringLiteral("Dubbed")
             : server.translation == VideoServer::Sub ? QStringLiteral("Subbed") : QString();
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> ServerListModel::roleNames() const {
    return {
            {NameRole, "name"},
            {LinkRole, "link"},
            {StatusRole, "status"},
            {TranslationRole, "translation"},
            {SectionRole, "section"},
            {Qt::DisplayRole, "text"},
            };
}
