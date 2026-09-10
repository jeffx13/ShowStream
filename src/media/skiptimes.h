#pragma once
#include <QObject>
#include <QHash>
#include <QList>
#include <QStringList>
#include <QFuture>
#include "net/canceltoken.h"
#include <qqmlintegration.h>

class PlaylistItem;
class Client;

class SkipTimes : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QString     searchQuery          READ searchQuery          WRITE setSearchQuery          NOTIFY searchQueryChanged)
    Q_PROPERTY(QStringList showTitles           READ showTitles                                         NOTIFY candidatesChanged)
    Q_PROPERTY(int         selectedShowIndex    READ selectedShowIndex    WRITE setSelectedShowIndex    NOTIFY selectedShowIndexChanged)
    Q_PROPERTY(int         episodeCount         READ episodeCount                                       NOTIFY episodeCountChanged)
    Q_PROPERTY(int         selectedEpisodeIndex READ selectedEpisodeIndex WRITE setSelectedEpisodeIndex NOTIFY selectedEpisodeIndexChanged)
    Q_PROPERTY(QString     status               READ status                                             NOTIFY statusChanged)
    Q_PROPERTY(bool        busy                 READ busy                                               NOTIFY statusChanged)
    Q_PROPERTY(QString     introRange           READ introRange                                         NOTIFY skipTimesChanged)
    Q_PROPERTY(QString     outroRange           READ outroRange                                         NOTIFY skipTimesChanged)
public:
    explicit SkipTimes(QObject *parent = nullptr);
    ~SkipTimes();

    void onCurrentItemChanged(PlaylistItem *item);

    QString searchQuery() const { return m_searchQuery; }
    void setSearchQuery(const QString &q);

    QStringList showTitles() const { return m_showTitles; }
    int selectedShowIndex() const { return m_selectedShowIndex; }
    void setSelectedShowIndex(int i);

    int episodeCount() const { return m_episodeCount; }
    int selectedEpisodeIndex() const { return m_selectedEpisodeIndex; }
    void setSelectedEpisodeIndex(int e);

    QString status() const { return m_status; }
    bool busy() const { return m_busy; }

    QString introRange() const { return m_introRange; }
    QString outroRange() const { return m_outroRange; }

    Q_INVOKABLE void rematch();

signals:
    void searchQueryChanged();
    void candidatesChanged();
    void selectedShowIndexChanged();
    void episodeCountChanged();
    void selectedEpisodeIndexChanged();
    void statusChanged();
    void skipTimesChanged();

private:
    struct Candidate { int malId = 0; QString title; int episodes = 0; };

    void ensureConnections();
    void onDurationChanged();
    void onAniskipToggled();

    void runSearch();                                       // AniList -> candidates (worker)
    static QList<Candidate> searchCandidates(Client &client, const QString &title);
    void setCandidates(const QList<Candidate> &list, int preferMalId);
    void rebuildEpisodeCount();
    void queryAniSkip();                                    // AniSkip for selection (worker)

    void applyTimes(int opStart, int opEnd, int edStart, int edEnd, int duration);
    void applyReset();
    void clearCandidates();
    void setSkipTimes(int opStart, int opEnd, int edStart, int edEnd);
    static QString formatTime(int seconds);
    void setStatus(const QString &text, bool busy);
    int  currentMalId() const;
    bool aniskipEnabled() const;

    // Superseding a search abandons its future, so every one is kept until it finishes -
    // the destructor has to wait on all of them, not just the newest.
    void track(QFuture<void> future);

    void loadProfile(const QString &showLink);
    void saveProfile();
    void loadFallback();
    void saveFallback();

    QString m_showTitle, m_showLink;
    int  m_episode         = -1;
    int  m_playlistEpisodes = 0;
    bool m_isOnline        = false;
    int  m_duration        = 0;

    QString          m_searchQuery;
    QList<Candidate> m_candidates;
    QStringList      m_showTitles;
    int              m_episodeCount    = 0;
    int              m_selectedShowIndex    = -1;
    int              m_selectedEpisodeIndex = -1;
    QString          m_status;
    bool             m_busy = false;
    QString          m_introRange, m_outroRange;

    bool m_connected = false;
    bool m_applying  = false;   // suppress profile save during programmatic apply

    QHash<QString, int> m_malIdCache;   // showLink -> chosen MAL id
    CancelToken m_searchCancel;
    CancelToken m_skipCancel;
    QList<QFuture<void>> m_pending;
};
