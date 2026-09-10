#pragma once
#include <QList>
#include <QSharedPointer>
#include <QString>

class PlaylistItem;
class ShowProvider;

struct ShowData
{
    ShowData(const QString &title = "", const QString &link = "", const QString &coverUrl = "",
             ShowProvider *provider = nullptr, const QString &latestTxt = "", int type = 0)
        : title(title), link(link), coverUrl(coverUrl), provider(provider), latestTxt(latestTxt), type(type) {}

    ShowData(const ShowData &) = default;
    ShowData(ShowData &&) noexcept = default;
    ShowData &operator=(const ShowData &) = default;
    ShowData &operator=(ShowData &&) noexcept = default;
    ~ShowData() = default;

    QString title;
    QString link;
    QString coverUrl;
    QString latestTxt;
    ShowProvider *provider = nullptr;
    QString description;
    QString releaseDate;
    QString status;
    QList<QString> genres;
    QString updateTime;
    QString score;
    QString views;
    int type = None;

    enum ShowType { None = 0, Anime = 1, Movie = 2, TvSeries = 3, Variety = 4, Documentary = 5 };

    struct WatchState {
        int libraryType = -1;
        int lastWatchedIndex = -1;
        double progress = 0.0;
        QSharedPointer<PlaylistItem> playlist;
    };

    void setPlaylist(QSharedPointer<PlaylistItem> playlist);
    QSharedPointer<PlaylistItem> playlist() const { return m_playlist; }

    void addEpisode(int seasonNumber, float number, const QString &link, const QString &name, bool preview = false);
    // Keeps the label only when it is not itself the number ("第12話", "12").
    void addNumberedEpisode(int seasonNumber, const QString &link, QString title);

private:
    QSharedPointer<PlaylistItem> m_playlist;
};
