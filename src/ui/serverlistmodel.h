#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QSet>
#include "net/client.h"
#include "media/playinfo.h"
#include <qqmlintegration.h>

class ShowProvider;

class ServerListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by Playlist; QML uses it for Status.")
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(int count        READ count                                NOTIFY countChanged)
public:
    // Per-server state; broken servers stay visible (greyed) rather than vanish.
    enum Status { Unchecked, Working, Broken };
    Q_ENUM(Status)

    ServerListModel() = default;
    ~ServerListModel() = default;

    void setServers(const QList<VideoServer> &servers, ShowProvider *provider);
    void setCurrentIndex(int index);
    void setCurrentServer(const QString &name);
    void setPreferredServer(int index);
    void clear();

    VideoServer& at(int index);
    int count() const { return m_servers.count(); }
    int currentIndex() const { return m_currentIndex; }
    bool isValidIndex(int index) const;

    ShowProvider *provider() const { return m_provider; }

    void setCachedSources(QHash<QString, PlayInfo> &&cache);
    void cacheSource(const QString &name, PlayInfo info);
    void markBroken(const QString &name);
    const PlayInfo *cachedSource(const QString &name) const { auto it = m_sourceCache.constFind(name); return it != m_sourceCache.constEnd() ? &*it : nullptr; }

signals:
    void currentIndexChanged();
    void countChanged();

private:
    bool hasDub() const;

    int m_currentIndex = -1;
    QList<VideoServer> m_servers;
    ShowProvider *m_provider = nullptr;
    QHash<QString, PlayInfo> m_sourceCache;
    QSet<QString> m_brokenServers;

    void emitStatusChanged(const QString &name);
    void resort();

    enum { NameRole = Qt::UserRole, LinkRole, StatusRole, TranslationRole, SectionRole };
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
};
