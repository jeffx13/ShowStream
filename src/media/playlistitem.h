#pragma once
#include <QSharedPointer>
#include <QWeakPointer>
#include <QEnableSharedFromThis>
#include <QScopedPointer>
#include <QFile>
#include <QRegularExpression>
#include <QUrl>
#include <QHash>

class ShowProvider;
class Video;
class Playlist;

class PlaylistItem : public QEnableSharedFromThis<PlaylistItem> {
public:
    enum Type { List = 1, Online = 2, Local = 4, Pasted = 8 };

    PlaylistItem(const QString& name = "", ShowProvider* provider = nullptr, const QString &link = "");
    PlaylistItem(int seasonNumber, float number, const QString &link, const QString &name,
                 QSharedPointer<PlaylistItem> parent, bool isLocal = false, bool preview = false);
    ~PlaylistItem();

    PlaylistItem(const PlaylistItem&) = delete;
    PlaylistItem& operator=(const PlaylistItem&) = delete;

    QString name;
    QString displayName;
    QString link;
    int     season = 0;
    float   number = -1;
    int     type;
    bool    preview = false;

    bool isList()     const { return type & Type::List; }
    bool isLocalDir() const { return (type & Type::Local) && (type & Type::List); }

    QSharedPointer<PlaylistItem> at(int i)     const { return isValidIndex(i) ? m_children.at(i) : nullptr; }
    QSharedPointer<PlaylistItem> first()       const { return at(0); }
    QSharedPointer<PlaylistItem> last()        const { return at(count() - 1); }
    QSharedPointer<PlaylistItem> parent()      const { return m_parent.toStrongRef(); }
    bool  isEmpty()       const { return m_children.isEmpty(); }
    int   count()         const { return m_children.size(); }
    int   row()           const { return m_row; }
    bool  isValidIndex(int index) const;
    int   indexOf(const QString &link) const;
    int   indexOf(const QSharedPointer<PlaylistItem> &child) const { return m_children.indexOf(child); }
    const QList<QSharedPointer<PlaylistItem>> &children() const { return m_children; }

    void emplaceBack(int season, float number, const QString &link, const QString &name, bool isLocal = false, bool preview = false);
    void append(QSharedPointer<PlaylistItem> value);
    void insert(int index, QSharedPointer<PlaylistItem> value);
    void removeAt(int index);
    void removeOne(const QSharedPointer<PlaylistItem> &value);
    void reserve(int n) { m_children.reserve(n); }
    void clear();
    void sort();

    bool setCurrentIndex(int index);
    int  currentIndex() const { return m_currentIndex; }
    QSharedPointer<PlaylistItem> currentItem() const { return at(m_currentIndex); }

    // Fraction watched, 0..1 - the only resume point kept.
    void   setProgress(double fraction);
    double progress() const;

    // Only meaningful for List nodes
    ShowProvider *provider() const { return m_provider; }


private:
    void updateRowIndices(int startIndex = 0);

    ShowProvider* m_provider = nullptr;
    QWeakPointer<PlaylistItem> m_parent;
    QList<QSharedPointer<PlaylistItem>> m_children;
    int m_currentIndex = -1;
    double m_progress = 0.0;
    int m_row = -1;
};
