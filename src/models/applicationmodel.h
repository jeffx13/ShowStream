#ifndef APPLICATIONMODEL_H
#define APPLICATIONMODEL_H

#include "downloadmodel.h"
#include "episodelistmodel.h"
#include "playlistmodel.h"
#include "searchresultsmodel.h"

#include <QAbstractListModel>
#include "watchlistmodel.h"

class ApplicationModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(PlaylistModel* playlistModel READ playlistModel CONSTANT)
    Q_PROPERTY(EpisodeListModel* episodeListModel READ episodeListModel CONSTANT)
    Q_PROPERTY(SearchResultsModel* searchResultsModel READ searchResultsModel CONSTANT)
    Q_PROPERTY(WatchListModel* watchList READ watchListModel CONSTANT)
//    Q_PROPERTY(WatchListModel* watchList READ watchListModel CONSTANT)

    EpisodeListModel m_episodeListModel{this};
    PlaylistModel m_playlistModel{this};
    SearchResultsModel m_searchResultsModel{this};
    WatchListModel m_watchListModel{this};
    DownloadModel m_downloadModel{this};
public:
    static ApplicationModel& instance()
    {
        static ApplicationModel s_instance;
        return s_instance;
    }
    Q_INVOKABLE void loadSourceFromList(int index){
        emit loadingStart();
        int watchListIndex = -1;
        m_playlistModel.syncList (m_watchListModel.getShowInList (Global::instance().currentShowObject ()->getShow ()));
        m_playlistModel.loadSource (index);
        emit loadingEnd ();
    }

    inline PlaylistModel* playlistModel(){return &m_playlistModel;}

    inline EpisodeListModel* episodeListModel(){return &m_episodeListModel;}

    inline SearchResultsModel* searchResultsModel(){return &m_searchResultsModel;}

    inline WatchListModel* watchListModel(){return &m_watchListModel;}


signals:
    void loadingStart();
    void loadingEnd();
private:
    explicit ApplicationModel(QObject *parent = nullptr): QObject(parent){
        connect(&m_watchListModel,&WatchListModel::detailsRequested,&m_searchResultsModel,&SearchResultsModel::getDetails);

        connect(&m_playlistModel,&PlaylistModel::updatedLastWatchedIndex,&m_watchListModel,&WatchListModel::save);

        connect(&m_watchListModel,&WatchListModel::indexMoved,&m_playlistModel,&PlaylistModel::changeWatchListIndex);

        connect(Global::instance ().currentShowObject (), &ShowResponseObject::showChanged,&m_watchListModel,&WatchListModel::checkCurrentShowInList);
        connect(Global::instance ().currentShowObject (), &ShowResponseObject::showChanged,[&](){
            m_episodeListModel.setIsReversed(false);
        });
//        m_downloadModel.downloadM3u8 ("lmao","D:\\TV\\temp\\尖峰时刻","https://ngzx.vizcloud.co/simple/EqPFIf8QWADtjDlGha7rC5QuqFxVu_T7SkR7rqk+wYMnU94US2El_Po4w1ynT_yP+tyVRt8p/br/H2/v.m3u8","https://ngzx.vizcloud.co");

    };

    ~ApplicationModel() {} // Private destructor to prevent external deletion.
    ApplicationModel(const ApplicationModel&) = delete; // Disable copy constructor.
    ApplicationModel& operator=(const ApplicationModel&) = delete; // Disable copy assignment.

private:
};

#endif // APPLICATIONMODEL_H
